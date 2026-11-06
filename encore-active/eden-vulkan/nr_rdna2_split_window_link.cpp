// SPDX-License-Identifier: MIT
#include "video_core/renderer_vulkan/nr_rdna2_split_window_link.h"
#include "video_core/renderer_vulkan/present/util.h"
#include "video_core/vulkan_common/vulkan_device.h"
#include <array>
#include <cstddef>
#include <cstdint>
#include <utility>

namespace Vulkan {
namespace NR=Eden::Graphics::NeuralRDNA2;
namespace {
[[nodiscard]] bool CanBind(const NrRdna2SplitWindowBuffers& b,
                           VkDeviceSize max_range) {
    const std::array<VkDescriptorBufferInfo,8> all{
        b.input,b.weights,b.scales,b.query,b.key,b.value,b.bias,b.context};
    for(std::size_t i=0;i<all.size();++i) {
        const auto& info=all[i];
        // All base offsets are relative to the descriptor's own range.
        if(!info.buffer||info.offset!=0u||!info.range||
           info.range==VK_WHOLE_SIZE||info.range>max_range)return false;
        // No two writable tensors, model resources or output can alias.
        for(std::size_t j=0;j<i;++j)
            if(all[j].buffer==info.buffer)return false;
    }
    return true;
}
}
NrRdna2SplitWindowLink::NrRdna2SplitWindowLink(
    const Device& d,Scheduler& s)
    :device{d},qkv{d,s},attention{d,s}{}
NrRdna2SplitWindowLink::~NrRdna2SplitWindowLink()=default;

bool NrRdna2SplitWindowLink::Initialize(
    const NR::SplitWindowLink& p,const NrRdna2SplitWindowBuffers& b) {
    if(initialized||!CanBind(b,device.GetMaxStorageBufferRange()))
        return false;
    const NR::SplitWindowLinkStorage sizes{
        {b.input.range,b.weights.range,b.scales.range,
         b.query.range,b.key.range,b.value.range},
        {b.query.range,b.key.range,b.value.range,
         b.context.range,b.bias.range}};
    if(!NR::CanSplitWindowLink(p,sizes))return false;

    // Both sets are built here, not trusted as externally bound handles.
    // The three intermediate tensors are literally the SAME buffers in
    // producer and consumer bindings.
    auto new_pool=CreateWrappedDescriptorPool(
        device,11u,2u,{VK_DESCRIPTOR_TYPE_STORAGE_BUFFER});
    std::array<VkDescriptorSetLayout,2> layouts{
        qkv.DescriptorLayout(),attention.DescriptorLayout()};
    auto new_sets=CreateWrappedDescriptorSets(
        new_pool,vk::Span<VkDescriptorSetLayout>{layouts.data(),layouts.size()});
    if(new_sets.IsOutOfPoolMemory()||new_sets.size()!=2u)return false;

    const std::array<VkDescriptorBufferInfo,11> infos{
        b.input,b.weights,b.scales,b.query,b.key,b.value,
        b.query,b.key,b.value,b.bias,b.context};
    std::array<VkWriteDescriptorSet,11> writes{};
    for(std::uint32_t i=0;i<writes.size();++i) {
        writes[i]=VkWriteDescriptorSet{
            .sType=VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,.pNext=nullptr,
            .dstSet=i<6u?new_sets[0]:new_sets[1],
            .dstBinding=i<6u?i:i-6u,.dstArrayElement=0,
            .descriptorCount=1,
            .descriptorType=VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
            .pImageInfo=nullptr,.pBufferInfo=&infos[i],
            .pTexelBufferView=nullptr};
    }
    device.GetLogical().UpdateDescriptorSets(
        vk::Span<VkWriteDescriptorSet>{writes.data(),writes.size()},{});
    pool=std::move(new_pool);
    allocations=std::move(new_sets);
    plan=p;
    storage=sizes;
    initialized=true;
    return true;
}
bool NrRdna2SplitWindowLink::Record(){
    if(!initialized||allocations.size()!=2u||
       !NR::CanSplitWindowLink(plan,storage))return false;
    // Each kernel inserts a compute->compute memory barrier, including
    // the Q/K/V write -> attention read dependency.
    if(!qkv.Record(allocations[0],plan.qkv,storage.qkv))return false;
    return attention.Record(allocations[1],plan.attention,storage.attention);
}
} // namespace Vulkan
