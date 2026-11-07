// SPDX-License-Identifier: MIT
#include "video_core/renderer_vulkan/nr_rdna2_preblock_run.h"
#include "video_core/renderer_vulkan/nr_rdna2_unet_skip_map.h"
#include <array>
#include <cstdint>
#include <string>
#include <vector>
namespace Vulkan {
namespace NR=Eden::Graphics::NeuralRDNA2;
NrRdna2PreblockRun::NrRdna2PreblockRun(
    const Device& d,MemoryAllocator& a,Scheduler& s)
    :scheduler{s},lift{d,a,s},body{d,a,s}{}
bool NrRdna2PreblockRun::Prepare(
    const NR::NeuralModelFile& model,std::string_view plan,
    VkDescriptorBufferInfo prepared,VkDescriptorBufferInfo lifted,
    VkDescriptorBufferInfo full_skip){
    if(attempted)return false;
    attempted=true;
    if(!model.IsOpen())return false;
    const std::array<VkDescriptorBufferInfo,3> buffers{
        prepared,lifted,full_skip};
    for(std::size_t i=0u;i<buffers.size();++i){
        const auto& view=buffers[i];
        if(!view.buffer||view.offset||!view.range||
           view.range==VK_WHOLE_SIZE)return false;
        for(std::size_t j=0u;j<i;++j)
            if(view.buffer==buffers[j].buffer)return false;
    }
    std::vector<NR::NativePlanStep> rows;
    NR::NativeUNetSkipMap graph{};
    if(NR::ParseNativePlan(plan,rows)!=NR::NativePlanError::Ok||
       NR::BuildNativeUNetSkipMap(rows,graph)!=NR::NativeUNetSkipError::Ok||
       rows.empty())return false;
    const auto& pre=rows.front();
    // The native plan is transposed relative to a GPU feature raster:
    // logical 1920x1080 => physical width 1080, height 1920.
    const std::uint32_t width=pre.height,height=pre.width;
    if(!NR::CanNativePreblockBoundary(
           rows,graph,std::uint64_t(rows[1].width)*rows[1].height*32u))
        return false;
    const std::uint64_t nwin=std::uint64_t(pre.grid_x)*pre.grid_y;
    if(!nwin||nwin>65535u)return false;
    const std::string prefix=
        "unpacked-preblock/block0.layer0.layer";
    const std::array<FSwinLayerSpec,1> spec{FSwinLayerSpec{
        prefix,32u,static_cast<std::uint32_t>(nwin),
        false,{pre.grid_x,pre.grid_y,pre.shift_x,pre.shift_y},
        false,true,false}};
    if(!lift.InitializeWithModel(
           model,NR::ImageFeatureDirection::Lift,0u,
           {width,height,0u},prepared,lifted)||
       !body.Initialize(model,spec)||
       !body.BindRasterInput(lift.Result(),width,height)||
       !body.BindRasterOutput(full_skip,width,height))
        return false;
    if(lift.SchedulerIdentity()!=&scheduler||
       body.SchedulerIdentity()!=&scheduler)return false;
    ready=true;
    return true;
}
bool NrRdna2PreblockRun::Record(){
    if(!ready||record_attempted||!lift.Ready()||!body.Ready()||
       lift.SchedulerIdentity()!=&scheduler||
       body.SchedulerIdentity()!=&scheduler)return false;
    record_attempted=true;
    if(!lift.Record()||!body.Record())return false;
    recorded=true;
    return true;
}
} // namespace Vulkan
