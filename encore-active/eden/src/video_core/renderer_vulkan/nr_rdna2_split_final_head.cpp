// SPDX-License-Identifier: MIT
#include "video_core/renderer_vulkan/nr_rdna2_split_final_head.h"
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
[[nodiscard]] bool Nonalias(
    VkDescriptorBufferInfo input,VkDescriptorBufferInfo scratch,
    VkDescriptorBufferInfo output,VkDeviceSize max_range){
    const std::array<VkDescriptorBufferInfo,3> buffers{
        input,scratch,output};
    for(std::size_t i=0u;i<buffers.size();++i){
        const auto& b=buffers[i];
        if(!b.buffer||b.offset||!b.range||
           b.range==VK_WHOLE_SIZE||b.range>max_range)return false;
        for(std::size_t j=0u;j<i;++j)
            if(b.buffer==buffers[j].buffer)return false;
    }
    return true;
}
}
NrRdna2SplitFinalHead::NrRdna2SplitFinalHead(
    const Device& d,MemoryAllocator& a,Scheduler& s)
    :device{d},allocator{a},kernels{d,s}{}
NrRdna2SplitFinalHead::~NrRdna2SplitFinalHead()=default;
bool NrRdna2SplitFinalHead::InitializeWithModel(
    const NR::NeuralModelFile& model,std::string_view prefix,
    std::uint32_t tokens,VkDescriptorBufferInfo source,
    VkDescriptorBufferInfo transient,VkDescriptorBufferInfo dst){
    if(ready||weights||!Nonalias(source,transient,dst,
                                  device.GetMaxStorageBufferRange()))
        return false;
    std::vector<std::uint8_t> matrix;
    if(!NR::PrepareSplitFinalHeadWeights(model,prefix,matrix))
        return false;
    auto owned=std::make_unique<NrRdna2GpuWeights>(device,allocator);
    if(!owned->Initialize(matrix))return false;
    const NR::Projection shape{tokens,1024u,512u,512u,512u,0u,0u};
    const NR::Storage storage{source.range,owned->Size(),
                              transient.range,transient.range};
    NR::QuantizedProjectionPlan plan{};
    if(!NR::PrepareQuantizedProjection(
           shape,storage,dst.range,plan))return false;
    auto pool=CreateWrappedDescriptorPool(
        device,5u,2u,{VK_DESCRIPTOR_TYPE_STORAGE_BUFFER});
    std::array<VkDescriptorSetLayout,2> layouts{
        kernels.GemmLayout(),kernels.QuantizeLayout()};
    auto allocated=CreateWrappedDescriptorSets(
        pool,vk::Span<VkDescriptorSetLayout>{
            layouts.data(),layouts.size()});
    if(allocated.IsOutOfPoolMemory()||allocated.size()!=2u)return false;
    const std::array<VkDescriptorBufferInfo,5> buffers{
        source,owned->Descriptor(),transient,transient,dst};
    std::array<VkWriteDescriptorSet,5> writes{};
    for(std::uint32_t i=0u;i<5u;++i){
        writes[i]=VkWriteDescriptorSet{
            .sType=VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,.pNext=nullptr,
            .dstSet=i<3u?allocated[0]:allocated[1],
            .dstBinding=i<3u?i:i-3u,.dstArrayElement=0,
            .descriptorCount=1u,.descriptorType=VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
            .pImageInfo=nullptr,.pBufferInfo=&buffers[i],
            .pTexelBufferView=nullptr};
    }
    device.GetLogical().UpdateDescriptorSets(
        vk::Span<VkWriteDescriptorSet>{writes.data(),writes.size()},{});
    descriptor_pool=std::move(pool);
    sets=std::move(allocated);
    weights=std::move(owned);
    input=source;scratch=transient;output=dst;
    projection=shape;
    ready=true;
    return true;
}
bool NrRdna2SplitFinalHead::Record(){
    if(!ready||sets.size()!=2u)return false;
    return kernels.Record(
        sets[0],sets[1],input,weights->Descriptor(),
        scratch,output,projection,scratch.range);
}
} // namespace Vulkan
