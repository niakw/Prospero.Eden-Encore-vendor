// SPDX-License-Identifier: MIT
#include "video_core/renderer_vulkan/nr_rdna2_split_swin_block.h"
#include "video_core/renderer_vulkan/nr_rdna2_split_ffwd_layer.h"
#include "video_core/renderer_vulkan/nr_rdna2_split_residual_projection.h"
#include "video_core/renderer_vulkan/nr_rdna2_split_window_link.h"
#include "video_core/renderer_vulkan/nr_rdna2_split_context_repack.h"
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
    const NrRdna2SplitSwinBlockBuffers& b,VkDeviceSize limit){
    const std::array<VkDescriptorBufferInfo,10> all{
        b.input,b.ffwd,b.ffwd_projected,b.context_heads,b.context_tokens,
        b.projected,b.query,b.key,b.value,b.fp32_scratch};
    for(std::size_t i=0u;i<all.size();++i){
        const auto& a=all[i];
        if(!a.buffer||a.offset||!a.range||
           a.range==VK_WHOLE_SIZE||a.range>limit)return false;
        // Scratch is reused sequentially by two projections, not aliased.
        for(std::size_t j=0u;j<i;++j)
            if(a.buffer==all[j].buffer)return false;
    }
    return true;
}
NR::SplitBlockCapacity Capacities(
    const NrRdna2SplitSwinBlockBuffers& b) {
    return {b.input.range,b.ffwd.range,b.ffwd_projected.range,
            b.context_heads.range,b.context_tokens.range,b.projected.range,
            b.query.range,b.key.range,b.value.range,b.fp32_scratch.range};
}
}
struct NrRdna2SplitSwinBlock::State {
    NrRdna2SplitFfwdLayer ffwd;
    NrRdna2SplitResidualProjection ffwd_proj;
    NrRdna2SplitWindowLink attention;
    NrRdna2SplitContextRepack transpose;
    NrRdna2SplitResidualProjection proj;
    vk::DescriptorPool pool;
    vk::DescriptorSets sets;
    NR::SplitContextRepack transpose_shape{};
    VkDescriptorBufferInfo transpose_input{},transpose_output{};
    State(const Device& d,MemoryAllocator& a,Scheduler& s)
      :ffwd{d,a,s},ffwd_proj{d,a,s},attention{d,s},
       transpose{d,s},proj{d,a,s}{}
};
NrRdna2SplitSwinBlock::NrRdna2SplitSwinBlock(
    const Device& d,MemoryAllocator& a,Scheduler& s)
    :device{d},allocator{a},scheduler{s}{}
NrRdna2SplitSwinBlock::~NrRdna2SplitSwinBlock()=default;
bool NrRdna2SplitSwinBlock::InitializeWithModel(
    const NR::NeuralModelFile& model,std::uint32_t block,
    const NR::SplitBlockPlan& plan,
    const NrRdna2SplitSwinBlockBuffers& b){
    if(state||!model.IsOpen()||!ValidResources(
           b,device.GetMaxStorageBufferRange())||
       !NR::CanSplitBlock(plan,Capacities(b)))return false;
    // Fail-closed for the native ProjPool layer: its pooled output
    // is a distinct required dependency, not a free side effect.
    if(plan.has_pooled_projection)return false;
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
    const NR::SplitWindowLink window{
        {0u,0u,0u,0u,0u,0u,plan.windows,16u},
        {0u,0u,0u,0u,0u,plan.windows,16u}};
    const NrRdna2SplitWindowBuffers wb{
        b.ffwd_projected,{},{},b.query,b.key,b.value,
        b.context_heads,{}};
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
           model,layer(3u),tokens,b.context_tokens,b.ffwd_projected,
           b.fp32_scratch,b.projected))return false;
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
    if(!state->ffwd.Record())return false;
    if(!state->ffwd_proj.Record())return false;
    if(!state->attention.Record())return false;
    if(!state->transpose.Record(
          state->sets[0],state->transpose_input,
          state->transpose_output,state->transpose_shape))return false;
    return state->proj.Record();
}
} // namespace Vulkan
