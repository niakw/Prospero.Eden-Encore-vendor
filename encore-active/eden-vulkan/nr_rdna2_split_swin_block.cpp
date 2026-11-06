// SPDX-License-Identifier: MIT
#include "video_core/renderer_vulkan/nr_rdna2_split_swin_block.h"
#include "video_core/renderer_vulkan/nr_rdna2_split_ffwd_layer.h"
#include "video_core/renderer_vulkan/nr_rdna2_split_residual_projection.h"
#include "video_core/renderer_vulkan/nr_rdna2_split_window_link.h"
#include "video_core/renderer_vulkan/nr_rdna2_split_context_repack.h"
#include "video_core/renderer_vulkan/nr_rdna2_fswin_window_repack.h"
#include "video_core/renderer_vulkan/nr_rdna2_split_projection_pool.h"
#include "video_core/renderer_vulkan/nr_rdna2_split_final_head.h"
#include "video_core/renderer_vulkan/present/util.h"
#include "video_core/vulkan_common/vulkan_device.h"
#include "video_core/vulkan_common/vulkan_memory_allocator.h"
#include <array>
#include <cstdint>
#include <memory>
#include <string>
#include <utility>
namespace Vulkan {
namespace NR=Eden::Graphics::NeuralRDNA2;
namespace {
[[nodiscard]] bool ValidResources(
    const NrRdna2SplitSwinBlockBuffers& b,VkDeviceSize limit,
    bool has_pooled_projection,bool has_final_head,bool needs_shift){
    std::array<VkDescriptorBufferInfo,15> used{};
    std::size_t count=0u;
    const auto add=[&](const VkDescriptorBufferInfo& resource){
        if(!resource.buffer||resource.offset||!resource.range||
           resource.range==VK_WHOLE_SIZE||resource.range>limit||
           count>=used.size())return false;
        for(std::size_t i=0u;i<count;++i)
            if(used[i].buffer==resource.buffer)return false;
        used[count++]=resource;
        return true;
    };
    const std::array<VkDescriptorBufferInfo,10> base{
        b.input,b.ffwd,b.ffwd_projected,b.context_heads,b.context_tokens,
        b.projected,b.query,b.key,b.value,b.fp32_scratch};
    for(const auto& resource:base)if(!add(resource))return false;
    if(needs_shift&&
       (!add(b.shifted_input)||!add(b.unshifted_context)))return false;
    if(has_pooled_projection&&!add(b.pooled))return false;
    if(has_final_head&&
       (!has_pooled_projection||
        !add(b.final_fp32)||!add(b.final_output)))return false;
    return true;
}
NR::SplitBlockCapacity Capacities(
    const NrRdna2SplitSwinBlockBuffers& b) {
    return {b.input.range,b.ffwd.range,b.ffwd_projected.range,
            b.context_heads.range,b.context_tokens.range,b.projected.range,
            b.query.range,b.key.range,b.value.range,b.fp32_scratch.range,
            b.pooled.range,b.final_fp32.range,b.final_output.range,
            b.shifted_input.range,b.unshifted_context.range};
}
}
struct NrRdna2SplitSwinBlock::State {
    NrRdna2SplitFfwdLayer ffwd;
    NrRdna2SplitResidualProjection ffwd_proj;
    NrRdna2SplitWindowLink attention;
    NrRdna2SplitContextRepack transpose;
    NrRdna2FSwinWindowRepack shift;
    vk::DescriptorPool shift_pool;
    vk::DescriptorSets shift_sets;
    NR::FSwinWindowRepack shift_forward{},shift_reverse{};
    NR::FSwinWindowRepackStorage shift_forward_storage{},shift_reverse_storage{};
    bool has_shift{};
    NrRdna2SplitResidualProjection proj;
    NrRdna2SplitProjectionPool pool_stage;
    NrRdna2SplitFinalHead final_head;
    vk::DescriptorPool pooled_set_pool;
    vk::DescriptorSets pooled_sets;
    NR::SplitProjectionPool pooled_shape{};
    std::array<VkDescriptorBufferInfo,4> pooled_buffers{};
    bool has_pool{};
    bool has_final_head{};
    vk::DescriptorPool pool;
    vk::DescriptorSets sets;
    NR::SplitContextRepack transpose_shape{};
    VkDescriptorBufferInfo transpose_input{},transpose_output{};
    State(const Device& d,MemoryAllocator& a,Scheduler& s)
      :ffwd{d,a,s},ffwd_proj{d,a,s},attention{d,s},
       transpose{d,s},shift{d,s},proj{d,a,s},pool_stage{d,s},
       final_head{d,a,s}{}
};
NrRdna2SplitSwinBlock::NrRdna2SplitSwinBlock(
    const Device& d,MemoryAllocator& a,Scheduler& s)
    :device{d},allocator{a},scheduler{s}{}
NrRdna2SplitSwinBlock::~NrRdna2SplitSwinBlock()=default;
bool NrRdna2SplitSwinBlock::InitializeWithModel(
    const NR::NeuralModelFile& model,std::uint32_t block,
    const NR::SplitBlockPlan& plan,
    const NrRdna2SplitSwinBlockBuffers& b){
    const bool need_shift=plan.attention_shift_x!=0||
                          plan.attention_shift_y!=0;
    if(state||!model.IsOpen()||!ValidResources(
           b,device.GetMaxStorageBufferRange(),
           plan.has_pooled_projection,
           plan.final_head_tokens!=0u,need_shift)||
       !NR::CanSplitBlock(plan,Capacities(b)))return false;
    // A pooled projection must produce its second, independent tensor,
    // including padded zero columns/rows and downstream tail tokens.
    const std::string prefix="unpacked-splitswin/block"+
                             std::to_string(block)+".layer";
    const auto layer=[&prefix](std::uint32_t index){
        return prefix+std::to_string(index)+".layer";
    };
    auto next=std::make_unique<State>(device,allocator,scheduler);
    const std::uint32_t tokens=plan.windows*64u;
    if(!next->ffwd.InitializeWithModel(
           model,layer(0u),tokens,b.input,b.ffwd))return false;
    if(!next->ffwd_proj.InitializeWithModel(
           model,layer(1u),tokens,b.ffwd,b.input,
           b.fp32_scratch,b.ffwd_projected))return false;
    if(need_shift){
        // Native QKV carries a signed shift in 4x4 tile units; the QKV
        // shader itself consumes plain windows. Repack before QKV and
        // reverse only AFTER the head->token context transposition.
        next->shift_forward={
            plan.window_grid_x,plan.window_grid_y,512u,0,0,
            plan.attention_shift_x,plan.attention_shift_y};
        next->shift_reverse={
            plan.window_grid_x,plan.window_grid_y,512u,
            plan.attention_shift_x,plan.attention_shift_y,0,0};
        next->shift_forward_storage={
            b.ffwd_projected.range,b.shifted_input.range};
        next->shift_reverse_storage={
            b.context_tokens.range,b.unshifted_context.range};
        if(!NR::CanFSwinWindowRepack(next->shift_forward,
                                    next->shift_forward_storage)||
           !NR::CanFSwinWindowRepack(next->shift_reverse,
                                    next->shift_reverse_storage))
            return false;
        auto shift_pool=CreateWrappedDescriptorPool(
            device,4u,2u,{VK_DESCRIPTOR_TYPE_STORAGE_BUFFER});
        std::array<VkDescriptorSetLayout,2> shift_layouts{
            next->shift.DescriptorLayout(),next->shift.DescriptorLayout()};
        auto shift_sets=CreateWrappedDescriptorSets(
            shift_pool,vk::Span<VkDescriptorSetLayout>{
                shift_layouts.data(),shift_layouts.size()});
        if(shift_sets.IsOutOfPoolMemory()||shift_sets.size()!=2u)
            return false;
        const std::array<VkDescriptorBufferInfo,4> shift_infos{
            b.ffwd_projected,b.shifted_input,
            b.context_tokens,b.unshifted_context};
        std::array<VkWriteDescriptorSet,4> shift_writes{};
        for(std::uint32_t i=0u;i<4u;++i){
            shift_writes[i]=VkWriteDescriptorSet{
                .sType=VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
                .pNext=nullptr,.dstSet=shift_sets[i/2u],
                .dstBinding=i%2u,.dstArrayElement=0u,
                .descriptorCount=1u,
                .descriptorType=VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
                .pImageInfo=nullptr,.pBufferInfo=&shift_infos[i],
                .pTexelBufferView=nullptr};
        }
        device.GetLogical().UpdateDescriptorSets(
            vk::Span<VkWriteDescriptorSet>{
                shift_writes.data(),shift_writes.size()},{});
        next->shift_pool=std::move(shift_pool);
        next->shift_sets=std::move(shift_sets);
        next->has_shift=true;
    }
    const NR::SplitWindowLink window{
        {0u,0u,0u,0u,0u,0u,plan.windows,16u},
        {0u,0u,0u,0u,0u,plan.windows,16u}};
    const NrRdna2SplitWindowBuffers wb{
        need_shift?b.shifted_input:b.ffwd_projected,
        {},{},b.query,b.key,b.value,b.context_heads,{}};
    if(!next->attention.InitializeWithModel(
           window,model,layer(2u),allocator,wb))return false;
    next->transpose_shape={plan.windows,16u,0u,0u};
    next->transpose_input=b.context_heads;
    next->transpose_output=b.context_tokens;
    auto pool=CreateWrappedDescriptorPool(
        device,2u,1u,{VK_DESCRIPTOR_TYPE_STORAGE_BUFFER});
    std::array<VkDescriptorSetLayout,1> layouts{
        next->transpose.DescriptorLayout()};
    auto sets=CreateWrappedDescriptorSets(
        pool,vk::Span<VkDescriptorSetLayout>{
            layouts.data(),layouts.size()});
    if(sets.IsOutOfPoolMemory()||sets.size()!=1u)return false;
    const std::array<VkDescriptorBufferInfo,2> info{
        b.context_heads,b.context_tokens};
    std::array<VkWriteDescriptorSet,2> writes{};
    for(std::uint32_t i=0u;i<2u;++i){
        writes[i]=VkWriteDescriptorSet{
            .sType=VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,.pNext=nullptr,
            .dstSet=sets[0],.dstBinding=i,.dstArrayElement=0,
            .descriptorCount=1u,.descriptorType=VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
            .pImageInfo=nullptr,.pBufferInfo=&info[i],
            .pTexelBufferView=nullptr};
    }
    device.GetLogical().UpdateDescriptorSets(
        vk::Span<VkWriteDescriptorSet>{writes.data(),writes.size()},{});
    next->pool=std::move(pool);
    next->sets=std::move(sets);
    if(!next->proj.InitializeWithModel(
           model,layer(3u),tokens,
           need_shift?b.unshifted_context:b.context_tokens,
           b.ffwd_projected,b.fp32_scratch,b.projected))return false;
    if(plan.has_pooled_projection){
        // The main projection's GEMM scratch is still live. The secondary
        // output must be computed from its unquantized half residual, NOT
        // by averaging the already-quantized FP8 primary output.
        next->pooled_shape={
            plan.pool_width,plan.pool_height,plan.pool_windows_x,
            plan.pool_windows_y,0u,0u,0u,0u,plan.pool_output_tokens};
        next->pooled_buffers={
            b.fp32_scratch,b.ffwd_projected,
            next->proj.GainDescriptor(),b.pooled};
        if(!NR::CanSplitProjectionPool(
              next->pooled_shape,{
                next->pooled_buffers[0].range,
                next->pooled_buffers[1].range,
                next->pooled_buffers[2].range,
                next->pooled_buffers[3].range}))return false;
        auto p=CreateWrappedDescriptorPool(
            device,4u,1u,{VK_DESCRIPTOR_TYPE_STORAGE_BUFFER});
        std::array<VkDescriptorSetLayout,1> pool_layouts{
            next->pool_stage.DescriptorLayout()};
        auto ds=CreateWrappedDescriptorSets(
            p,vk::Span<VkDescriptorSetLayout>{
                pool_layouts.data(),pool_layouts.size()});
        if(ds.IsOutOfPoolMemory()||ds.size()!=1u)return false;
        std::array<VkWriteDescriptorSet,4> writes{};
        for(std::uint32_t i=0u;i<4u;++i){
            writes[i]=VkWriteDescriptorSet{
                .sType=VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,.pNext=nullptr,
                .dstSet=ds[0],.dstBinding=i,.dstArrayElement=0u,
                .descriptorCount=1u,
                .descriptorType=VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
                .pImageInfo=nullptr,.pBufferInfo=&next->pooled_buffers[i],
                .pTexelBufferView=nullptr};
        }
        device.GetLogical().UpdateDescriptorSets(
            vk::Span<VkWriteDescriptorSet>{writes.data(),writes.size()},{});
        next->pooled_set_pool=std::move(p);
        next->pooled_sets=std::move(ds);
        next->has_pool=true;
    }
    if(plan.final_head_tokens){
        // The real final layer consumes the pooled tensor, not the
        // primary ProjPool output, and produces 1024-channel FP8.
        if(!plan.has_pooled_projection||
           !next->final_head.InitializeWithModel(
               model,layer(4u),plan.final_head_tokens,b.pooled,
               b.final_fp32,b.final_output))return false;
        next->has_final_head=true;
    }
    // No half-initialized block visible to the renderer.
    state=std::move(next);
    return true;
}
bool NrRdna2SplitSwinBlock::Record(){
    if(!state||state->sets.size()!=1u||
       !NR::CanSplitContextRepack(
          state->transpose_shape,
          {state->transpose_input.range,
           state->transpose_output.range}))return false;
    if(state->has_shift&&
       (state->shift_sets.size()!=2u||
        !NR::CanFSwinWindowRepack(
            state->shift_forward,state->shift_forward_storage)||
        !NR::CanFSwinWindowRepack(
            state->shift_reverse,state->shift_reverse_storage)))
        return false;
    if(!state->ffwd.Record())return false;
    if(!state->ffwd_proj.Record())return false;
    if(state->has_shift&&!state->shift.Record(
          state->shift_sets[0],state->shift_forward,
          state->shift_forward_storage))return false;
    if(!state->attention.Record())return false;
    if(!state->transpose.Record(
          state->sets[0],state->transpose_input,
          state->transpose_output,state->transpose_shape))return false;
    if(state->has_shift&&!state->shift.Record(
          state->shift_sets[1],state->shift_reverse,
          state->shift_reverse_storage))return false;
    if(!state->proj.Record())return false;
    if(!state->has_pool)return true;
    if(state->pooled_sets.size()!=1u||!NR::CanSplitProjectionPool(
          state->pooled_shape,{
            state->pooled_buffers[0].range,
            state->pooled_buffers[1].range,
            state->pooled_buffers[2].range,
            state->pooled_buffers[3].range}))return false;
    if(!state->pool_stage.Record(
        state->pooled_sets[0],
        state->pooled_buffers[0],state->pooled_buffers[1],
        state->pooled_buffers[2],state->pooled_buffers[3],
        state->pooled_shape))return false;
    return !state->has_final_head||state->final_head.Record();
}
} // namespace Vulkan
