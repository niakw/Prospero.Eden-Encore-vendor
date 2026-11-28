// SPDX-License-Identifier: MIT
#include "video_core/renderer_vulkan/nr_rdna2_split_ffwd_layer.h"
#include "video_core/renderer_vulkan/present/util.h"
#include "video_core/vulkan_common/vulkan_device.h"
#include "video_core/vulkan_common/vulkan_memory_allocator.h"
#include <array>
#include <cstdint>
#include <memory>
#include <utility>
namespace Vulkan {
namespace NR=Eden::Graphics::NeuralRDNA2;
namespace {
[[nodiscard]] bool ValidExternal(
    VkDescriptorBufferInfo a,VkDescriptorBufferInfo b,
    VkDeviceSize max_range) {
    return a.buffer&&b.buffer&&a.buffer!=b.buffer&&
        a.offset==0u&&b.offset==0u&&a.range>0u&&b.range>0u&&
        a.range!=VK_WHOLE_SIZE&&b.range!=VK_WHOLE_SIZE&&
        a.range<=max_range&&b.range<=max_range;
}
}
NrRdna2SplitFfwdLayer::NrRdna2SplitFfwdLayer(
    const Device& d,MemoryAllocator& a,Scheduler& s)
    :device{d},allocator{a},kernel{d,s}{}
NrRdna2SplitFfwdLayer::~NrRdna2SplitFfwdLayer()=default;
bool NrRdna2SplitFfwdLayer::InitializeWithModel(
    const NR::NeuralModelFile& model,std::string_view prefix,
    std::uint32_t tokens,VkDescriptorBufferInfo input,
    VkDescriptorBufferInfo output) {
    if(ready||weights1||weights2||weights3||
       !ValidExternal(input,output,device.GetMaxStorageBufferRange()))
        return false;
    NR::SplitFFWDWeights decoded;
    if(!NR::PrepareSplitFFWDWeights(model,prefix,decoded))return false;
    // Transactional GPU allocations, no partially-visible stage.
    auto a=std::make_unique<NrRdna2GpuWeights>(device,allocator);
    auto b=std::make_unique<NrRdna2GpuWeights>(device,allocator);
    auto c=std::make_unique<NrRdna2GpuWeights>(device,allocator);
    if(!a->Initialize(decoded.w1)||!b->Initialize(decoded.w2)||
       !c->Initialize(decoded.w3))return false;
    const NR::FFWD3 proposed{0u,0u,0u,0u,0u,tokens};
    const NR::FFWD3Storage sizes{
        input.range,a->Size(),b->Size(),c->Size(),output.range};
    if(!NR::CanFFWD3(proposed,sizes))return false;
    auto new_pool=CreateWrappedDescriptorPool(
        device,5u,1u,{VK_DESCRIPTOR_TYPE_STORAGE_BUFFER});
    std::array<VkDescriptorSetLayout,1> layouts{
        kernel.DescriptorLayout()};
    auto allocated=CreateWrappedDescriptorSets(
        new_pool,vk::Span<VkDescriptorSetLayout>{
            layouts.data(),layouts.size()});
    if(allocated.IsOutOfPoolMemory()||allocated.size()!=1u)return false;
    const std::array<VkDescriptorBufferInfo,5> infos{
        input,a->Descriptor(),b->Descriptor(),c->Descriptor(),output};
    std::array<VkWriteDescriptorSet,5> writes{};
    for(std::uint32_t i=0u;i<5u;++i){
        writes[i]=VkWriteDescriptorSet{
            .sType=VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,.pNext=nullptr,
            .dstSet=allocated[0],.dstBinding=i,.dstArrayElement=0u,
            .descriptorCount=1u,.descriptorType=VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
            .pImageInfo=nullptr,.pBufferInfo=&infos[i],
            .pTexelBufferView=nullptr};
    }
    device.GetLogical().UpdateDescriptorSets(
        vk::Span<VkWriteDescriptorSet>{writes.data(),writes.size()},{});
    pool=std::move(new_pool);
    sets=std::move(allocated);
    weights1=std::move(a);
    weights2=std::move(b);
    weights3=std::move(c);
    shape=proposed;
    storage=sizes;
    ready=true;
    return true;
}
bool NrRdna2SplitFfwdLayer::Record(){
    return ready&&sets.size()==1u&&
           NR::CanFFWD3(shape,storage)&&
           kernel.Record(sets[0],shape,storage);
}
} // namespace Vulkan
