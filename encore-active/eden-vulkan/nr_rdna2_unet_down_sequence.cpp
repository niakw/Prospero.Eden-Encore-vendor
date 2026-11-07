// SPDX-License-Identifier: MIT
#include "video_core/renderer_vulkan/nr_rdna2_unet_down_sequence.h"
#include <array>
#include <cstdint>
namespace Vulkan {
namespace NR=Eden::Graphics::NeuralRDNA2;
NrRdna2UNetDownSequence::NrRdna2UNetDownSequence(Scheduler& s)
    :scheduler{s}{}
bool NrRdna2UNetDownSequence::Prepare(
    const NR::NeuralModelFile& model,
    const NR::NativeUNetSkipMap& graph,
    NrRdna2UNetSkipCache& cache,
    std::array<NrRdna2FSwinChain*,4> levels,
    VkDescriptorBufferInfo input,
    std::array<VkDescriptorBufferInfo,4> downs){
    if(prepare_attempted)return false;
    // Never reuse partially prepared stages after any error.
    prepare_attempted=true;
    if(!model.IsOpen()||!cache.Ready()||
       cache.SchedulerIdentity()!=nullptr||
       !input.buffer||input.offset||input.range==0u||
       input.range==VK_WHOLE_SIZE)return false;
    std::array<NR::FSwinWindowLayout,4> inputs{},outputs{};
    std::array<std::uint64_t,4> capacities{};
    for(std::size_t i=0u;i<4u;++i){
        auto* stage=levels[i];
        if(!stage||!stage->Ready()||
           stage->SchedulerIdentity()!=&scheduler||
           !stage->RequiresDownsampleOutput()||
           stage->Channels()!=(32u<<i))return false;
        inputs[i]=stage->InputWindowLayout();
        outputs[i]=stage->OutputWindowLayout();
        const auto& dst=downs[i];
        if(!dst.buffer||dst.offset||dst.range==0u||
           dst.range==VK_WHOLE_SIZE||
           dst.buffer==input.buffer)return false;
        capacities[i]=dst.range;
        for(std::size_t j=0u;j<i;++j)
            if(levels[j]==stage||downs[j].buffer==dst.buffer)
                return false;
        // No output aliasing with any section's token windows.
        for(std::size_t j=0u;j<4u;++j)
            if(levels[j]&&(dst.buffer==levels[j]->Input().buffer||
                           dst.buffer==levels[j]->Output().buffer))
                return false;
    }
    if(!levels[0]->AllowsGpuRasterInput())return false;
    if(!NR::CanNativeUNetDownSequence(
           graph,inputs,outputs,input.range,capacities))return false;
    std::array<const NR::NativeUNetSkipEdge*,4> edges{};
    for(const auto& edge:graph.edges){
        const auto depth=edge.channels==32u?0u:
                         edge.channels==64u?1u:
                         edge.channels==128u?2u:3u;
        if(edges[depth])return false;
        edges[depth]=&edge;
    }
    // Bind the preblock-produced FP8 raster before preparing the DS
    // adapters. Note: native plan axes are transposed for Vulkan raster.
    const auto first_width=edges[0]->skip_height;
    const auto first_height=edges[0]->skip_width;
    if(!levels[0]->BindRasterInput(input,first_width,first_height))
        return false;
    for(std::size_t i=0u;i<3u;++i){
        if(!links[i].Prepare(
               *levels[i],*levels[i+1u],model,downs[i],
               edges[i]->skip_height,edges[i]->skip_width))
            return false;
    }
    if(!levels[3]->BindDownsampleOutput(
           model,downs[3],edges[3]->skip_height,
           edges[3]->skip_width))return false;
    for(std::size_t i=0u;i<4u;++i){
        if(!cache.BindProducer(edges[i]->down_block,*levels[i]))
            return false;
    }
    for(std::size_t i=0u;i<4u;++i)
        down_blocks[i]=edges[i]->down_block;
    stages=levels;
    skips=&cache;
    low=downs[3];
    ready=true;
    return true;
}
bool NrRdna2UNetDownSequence::Record(){
    if(!ready||record_attempted||!skips||
       skips->SchedulerIdentity()!=&scheduler)return false;
    // Some stages can have emitted GPU commands when an error occurs.
    // Never resubmit them implicitly.
    record_attempted=true;
    for(std::size_t i=0u;i<4u;++i){
        if(!stages[i]||!stages[i]->Ready()||
           !skips->RecordProducer(down_blocks[i],*stages[i]))
            return false;
    }
    recorded=true;
    return true;
}
} // namespace Vulkan
