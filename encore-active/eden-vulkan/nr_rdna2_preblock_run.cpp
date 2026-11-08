// SPDX-License-Identifier: MIT
#include "video_core/renderer_vulkan/nr_rdna2_preblock_run.h"
#include "video_core/renderer_vulkan/nr_rdna2_unet_skip_map.h"
#include "video_core/renderer_vulkan/nr_rdna2_unet_down_sequence.h"
#include <array>
#include <cstdint>
#include <string>
#include <vector>
namespace Vulkan {
namespace NR=Eden::Graphics::NeuralRDNA2;
NrRdna2PreblockRun::NrRdna2PreblockRun(
    const Device& d,MemoryAllocator& a,Scheduler& s)
    :scheduler{s},lift{d,a,s},body{d,a,s},pool{d,s}{}
bool NrRdna2PreblockRun::Prepare(
    const NR::NeuralModelFile& model,std::string_view plan,
    VkDescriptorBufferInfo prepared,VkDescriptorBufferInfo lifted,
    VkDescriptorBufferInfo full_skip){
    if(attempted)return false;
    attempted=true;
    prepare_phase="preblock_model_open";
    if(!model.IsOpen())return false;
    const std::array<VkDescriptorBufferInfo,3> buffers{
        prepared,lifted,full_skip};
    prepare_phase="preblock_distinct_buffer_contract";
    for(std::size_t i=0u;i<buffers.size();++i){
        const auto& view=buffers[i];
        if(!view.buffer||view.offset||!view.range||
           view.range==VK_WHOLE_SIZE)return false;
        for(std::size_t j=0u;j<i;++j)
            if(view.buffer==buffers[j].buffer)return false;
    }
    std::vector<NR::NativePlanStep> rows;
    NR::NativeUNetSkipMap graph{};
    prepare_phase="preblock_native_plan";
    if(NR::ParseNativePlan(plan,rows)!=NR::NativePlanError::Ok||
       NR::BuildNativeUNetSkipMap(rows,graph)!=NR::NativeUNetSkipError::Ok||
       rows.size()<2u)return false;
    const auto& pre=rows.front();
    // The native plan is transposed relative to a GPU feature raster:
    // logical 1920x1080 => physical width 1080, height 1920.
    const std::uint32_t width=pre.height,height=pre.width;
    prepare_phase="preblock_boundary_contract";
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
    prepare_phase="preblock_model_lift";
    if(!lift.InitializeWithModel(
           model,NR::ImageFeatureDirection::Lift,0u,
           {width,height,0u},prepared,lifted))return false;
    prepare_phase="preblock_fswin_model_body";
    if(!body.Initialize(model,spec))return false;
    prepare_phase="preblock_fswin_input_binding";
    if(!body.BindRasterInput(lift.Result(),width,height))return false;
    prepare_phase="preblock_fswin_output_binding";
    if(!body.BindRasterOutput(full_skip,width,height))return false;
    prepare_phase="preblock_scheduler_identity";
    if(lift.SchedulerIdentity()!=&scheduler||
       body.SchedulerIdentity()!=&scheduler)return false;
    prepared_input=prepared;
    full_geometry={width,height};
    ready=true;
    return true;
}
bool NrRdna2PreblockRun::PrepareWithPool(
    const NR::NeuralModelFile& model,std::string_view plan,
    VkDescriptorBufferInfo prepared,VkDescriptorBufferInfo lifted,
    VkDescriptorBufferInfo full_skip,VkDescriptorBufferInfo raw_full,
    VkDescriptorBufferInfo pooled_half){
    if(!Prepare(model,plan,prepared,lifted,full_skip))return false;
    prepare_phase="preblock_pool_distinct_buffers";
    const std::array<VkDescriptorBufferInfo,5> buffers{
        prepared,lifted,full_skip,raw_full,pooled_half};
    for(std::size_t i=0u;i<buffers.size();++i){
        const auto& v=buffers[i];
        if(!v.buffer||v.offset||!v.range||v.range==VK_WHOLE_SIZE){
            ready=false;
            return false;
        }
        for(std::size_t j=0u;j<i;++j)
            if(v.buffer==buffers[j].buffer){
                ready=false;
                return false;
            }
    }
    prepare_phase="preblock_pool_plan_parse";
    std::vector<NR::NativePlanStep> rows;
    if(NR::ParseNativePlan(plan,rows)!=NR::NativePlanError::Ok||
       rows.size()<2u){
        ready=false;
        return false;
    }
    const NR::PreblockPoolShape shape{
        rows.front().height,rows.front().width};
    prepare_phase="preblock_pool_storage_contract";
    if(!NR::CanPreblockPool(shape,{raw_full.range,pooled_half.range})){
        ready=false;
        return false;
    }
    prepare_phase="preblock_raw_raster_binding";
    if(!body.BindRawRasterOutput(raw_full,shape.width,shape.height)){
        ready=false;
        return false;
    }
    prepare_phase="preblock_pool_descriptor";
    if(!pool.Initialize(shape,raw_full,pooled_half)){
        ready=false;
        return false;
    }
    prepare_phase="preblock_pool_scheduler_identity";
    if(pool.SchedulerIdentity()!=&scheduler){
        ready=false;
        return false;
    }
    has_pool=true;
    prepare_phase="completed";
    return true;
}
bool NrRdna2PreblockRun::Record(){
    if(!ready||record_attempted||!lift.Ready()||!body.Ready()||
       lift.SchedulerIdentity()!=&scheduler||
       body.SchedulerIdentity()!=&scheduler||
       (has_pool&&(!pool.Ready()||
                   pool.SchedulerIdentity()!=&scheduler)))return false;
    record_attempted=true;
    if(!lift.Record()||!body.Record()||
       (has_pool&&!pool.Record()))return false;
    recorded=true;
    return true;
}
bool NrRdna2PreblockRun::RecordWithDownSequence(
    NrRdna2UNetDownSequence& down){
    const auto pooled=PreparedHalfC32();
    const auto input=down.PreparedInput();
    if(!ready||!has_pool||record_attempted||!down.Ready()||
       down.SchedulerIdentity()!=&scheduler||
       !pooled.buffer||!input.buffer||
       pooled.buffer!=input.buffer||
       pooled.offset!=input.offset||
       pooled.range!=input.range)
        return false;
    // Record may partially enqueue on a failure; both owners are
    // single-use. Never replay on the same Vulkan scheduler.
    return Record()&&down.Record();
}
} // namespace Vulkan
