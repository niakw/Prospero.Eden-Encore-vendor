// SPDX-License-Identifier: MIT
#include "video_core/renderer_vulkan/nr_rdna2_unet_up_sequence.h"
#include <array>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>
namespace Vulkan {
namespace NR=Eden::Graphics::NeuralRDNA2;
NrRdna2UNetUpSequence::NrRdna2UNetUpSequence(
    const Device& device,MemoryAllocator& allocator,Scheduler& s)
    :scheduler{s}{
    for(auto& link:links)
        link=std::make_unique<NrRdna2UNetUpsampleLink>(
            device,allocator,scheduler);
}
NrRdna2UNetUpSequence::~NrRdna2UNetUpSequence()=default;
bool NrRdna2UNetUpSequence::Prepare(
    const NR::NeuralModelFile& model,std::string_view native_plan,
    const NR::NativeUNetSkipMap& graph,
    NrRdna2UNetSkipCache& cache,std::uint32_t first_low_block,
    VkDescriptorBufferInfo first_low,
    std::array<NrRdna2FSwinChain*,4> chains,
    std::array<VkDescriptorBufferInfo,4> rasters){
    if(prepare_attempted)return false;
    prepare_attempted=true;
    if(!model.IsOpen()||!cache.Ready()||
       cache.SchedulerIdentity()!=&scheduler||
       !first_low.buffer||first_low.offset||first_low.range==0u||
       first_low.range==VK_WHOLE_SIZE)return false;
    std::array<NR::FSwinWindowLayout,4> inputs{},outputs{};
    std::array<std::uint64_t,4> capacities{};
    for(std::size_t i=0u;i<4u;++i){
        auto* chain=chains[i];
        const auto& result=rasters[i];
        if(!chain||!chain->Ready()||
           chain->SchedulerIdentity()!=&scheduler||
           chain->Channels()!=(256u>>i)||
           !chain->AllowsGpuRasterInput()||
           !chain->RequiresRasterOutput()||
           !result.buffer||result.offset||result.range==0u||
           result.range==VK_WHOLE_SIZE||
           result.buffer==first_low.buffer)return false;
        for(std::size_t j=0u;j<i;++j)
            if(chains[j]==chain||rasters[j].buffer==result.buffer)
                return false;
        inputs[i]=chain->InputWindowLayout();
        outputs[i]=chain->OutputWindowLayout();
        capacities[i]=result.range;
    }
    if(!NR::CanNativeUNetUpSequence(
           graph,inputs,outputs,first_low.range,capacities))
        return false;
    std::array<const NR::NativeUNetSkipEdge*,4> edges{};
    for(const auto& e:graph.edges){
        const auto idx=e.channels==256u?0u:e.channels==128u?1u:
                       e.channels==64u?2u:3u;
        if(edges[idx])return false;
        edges[idx]=&e;
    }
    std::vector<NR::NativePlanStep> rows;
    if(NR::ParseNativePlan(native_plan,rows)!=NR::NativePlanError::Ok)
        return false;
    std::array<const NR::NativePlanStep*,4> ups{};
    for(std::size_t i=0u;i<4u;++i){
        const auto* edge=edges[i];
        for(const auto& row:rows){
            if(row.block!=edge->up_block||
               !row.kernel.ends_with("_upsample_fp8"))continue;
            if(ups[i])return false;
            ups[i]=&row;
        }
        if(!ups[i]||ups[i]->layer!=0u||
           ups[i]->channels!=edge->channels||
           ups[i]->input0<0||
           static_cast<std::uint32_t>(ups[i]->input0)!=
               edge->up_input_block)return false;
        if(i==0u){
            if(first_low_block!=edge->up_input_block)return false;
        }else{
            bool prior_outview=false;
            for(const auto& row:rows)
                if(row.block==edge->up_input_block&&
                   row.channels==(256u>>(i-1u))&&
                   row.kernel.ends_with("_outview_fp8")&&
                   row.block>edges[i-1u]->up_block)
                    prior_outview=true;
            if(!prior_outview)return false;
        }
        // A cached skip is released only once the down-path producer
        // was successfully enqueued. It need not have completed on GPU.
        if(!cache.ForUpsample(edge->up_block).buffer)return false;
    }
    // Bind terminal rasters before any up stage so a missing outview
    // cannot cause a partial command sequence.
    for(std::size_t i=0u;i<4u;++i){
        if(!chains[i]->BindRasterOutput(
               rasters[i],edges[i]->skip_height,
               edges[i]->skip_width))
            return false;
    }
    for(std::size_t i=0u;i<4u;++i){
        const auto& edge=*edges[i];
        const auto prefix=std::string("unpacked/block")+
            std::to_string(ups[i]->block)+".layer"+
            std::to_string(ups[i]->layer)+".layer";
        const auto low=i?rasters[i-1u]:first_low;
        if(!links[i]->Prepare(
               graph,cache,model,edge.up_block,
               edge.up_input_block,prefix,low,rasters[i],
               *chains[i]))return false;
    }
    skips=&cache;
    last=rasters[3];
    ready=true;
    return true;
}
bool NrRdna2UNetUpSequence::Record(){
    if(!ready||record_attempted||!skips||
       skips->SchedulerIdentity()!=&scheduler)return false;
    record_attempted=true;
    for(const auto& link:links)
        if(!link||!link->Ready()||!link->Record())return false;
    recorded=true;
    return true;
}
} // namespace Vulkan
