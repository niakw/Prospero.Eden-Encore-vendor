// SPDX-License-Identifier: MIT
#include "video_core/renderer_vulkan/nr_rdna2_postblock_run.h"
#include <array>
#include <cstdint>
#include <string>
#include <vector>
namespace Vulkan {
namespace NR=Eden::Graphics::NeuralRDNA2;
NrRdna2PostblockRun::NrRdna2PostblockRun(
    const Device& d,MemoryAllocator& a,Scheduler& s)
    :scheduler{s},mix{d,a,s},body{d,a,s},projection{d,a,s}{}

bool NrRdna2PostblockRun::Prepare(
    const NR::NeuralModelFile& model,std::string_view plan,
    VkDescriptorBufferInfo low,VkDescriptorBufferInfo skip,
    VkDescriptorBufferInfo mixed,VkDescriptorBufferInfo swin,
    VkDescriptorBufferInfo projected){
    if(prepare_attempted)return false;
    prepare_attempted=true;
    if(!model.IsOpen())return false;
    const std::array<VkDescriptorBufferInfo,5> buffers{
        low,skip,mixed,swin,projected};
    for(std::size_t i=0u;i<buffers.size();++i){
        const auto& a=buffers[i];
        if(!a.buffer||a.offset||!a.range||
           a.range==VK_WHOLE_SIZE)return false;
        for(std::size_t j=0u;j<i;++j)
            if(a.buffer==buffers[j].buffer)return false;
    }
    std::vector<NR::NativePlanStep> rows;
    if(NR::ParseNativePlan(plan,rows)!=NR::NativePlanError::Ok||
       rows.size()<3u)return false;
    const auto& last=rows.back();
    // Native plan axes are reversed relative to the physical C32 raster:
    // native 1920x1080 -> Vulkan raster width=1080, height=1920.
    const NR::PostblockMixShape geometry{
        rows.front().height,rows.front().width};
    if(!NR::CanNativePostblockMixBoundary(rows,geometry)||
       !NR::CanPostblockMix(geometry,{
          low.range,skip.range,128u,mixed.range}))return false;
    const auto prefix=std::string("unpacked-postblock/block")+
        std::to_string(last.block)+".layer0.layer";
    const std::uint64_t windows=
        std::uint64_t(last.grid_x)*last.grid_y;
    if(windows==0u||windows>65535u)return false;
    const std::array<FSwinLayerSpec,1> spec{FSwinLayerSpec{
        prefix,32u,static_cast<std::uint32_t>(windows),
        false,{last.grid_x,last.grid_y,last.shift_x,last.shift_y},
        false,true,false}};
    // All initialization is performed before any GPU commands are enqueued.
    // There is no fallback to fabricated gains, missing weight files, or
    // a plain RGB filter.
    if(!mix.Initialize(model,last.block,geometry,low,skip,mixed)||
       !body.Initialize(model,spec)||
       !body.BindRasterInput(mix.Result(),geometry.width,geometry.height)||
       !body.BindRasterOutput(swin,geometry.width,geometry.height)||
       !projection.InitializeWithModel(
           model,NR::ImageFeatureDirection::Project,last.block,
           {geometry.width,geometry.height,1u},swin,projected))
        return false;
    if(mix.SchedulerIdentity()!=&scheduler||
       body.SchedulerIdentity()!=&scheduler||
       projection.SchedulerIdentity()!=&scheduler)return false;
    ready=true;
    return true;
}
bool NrRdna2PostblockRun::Record(){
    if(!ready||record_attempted||!mix.Ready()||
       !body.Ready()||!projection.Ready()||
       mix.SchedulerIdentity()!=&scheduler||
       body.SchedulerIdentity()!=&scheduler||
       projection.SchedulerIdentity()!=&scheduler)return false;
    record_attempted=true;
    if(!mix.Record()||!body.Record()||!projection.Record())return false;
    recorded=true;
    return true;
}
} // namespace Vulkan
