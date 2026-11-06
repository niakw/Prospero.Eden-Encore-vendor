// SPDX-License-Identifier: MIT
#include "video_core/renderer_vulkan/nr_rdna2_vit_qkv_attention_link.h"
#include "video_core/renderer_vulkan/nr_rdna2_vit_model_buffers.h"
#include "video_core/renderer_vulkan/present/util.h"
#include "video_core/vulkan_common/vulkan_device.h"
#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <utility>
namespace Vulkan {
namespace NR=Eden::Graphics::NeuralRDNA2;
namespace {
[[nodiscard]] bool DistinctBuffers(
    const NrRdna2VitQkvAttentionBuffers& b,VkDeviceSize max_range){
    const std::array<VkDescriptorBufferInfo,8> all{
        b.input,b.weights,b.fp32,b.temperatures,
        b.query,b.key,b.value,b.context};
    for(std::size_t i=0u;i<all.size();++i){
        const auto& x=all[i];
        if(!x.buffer||x.offset||!x.range||
           x.range==VK_WHOLE_SIZE||x.range>max_range)return false;
        for(std::size_t j=0u;j<i;++j)
            if(all[j].buffer==x.buffer)return false;
    }
    return true;
}
}
NrRdna2VitQkvAttentionLink::NrRdna2VitQkvAttentionLink(
    const Device& d,Scheduler& s)
    :device{d},gemm{d,s},normalize{d,s},attention{d,s}{}
NrRdna2VitQkvAttentionLink::~NrRdna2VitQkvAttentionLink()=default;
bool NrRdna2VitQkvAttentionLink::Initialize(
    std::uint32_t tokens,const NrRdna2VitQkvAttentionBuffers& b){
    if(ready||!DistinctBuffers(b,device.GetMaxStorageBufferRange()))
        return false;
    const NR::VitQkvBuffersCapacity capacity{
        b.input.range,b.weights.range,b.fp32.range,b.temperatures.range,
        b.query.range,b.key.range,b.value.range,b.context.range};
    NR::VitQkvBatches prepared{};
    if(!NR::BuildVitQkvBatches(tokens,capacity,prepared))return false;
    // GEMM, normalize and attention each own their descriptor layout.
    // Every producer buffer is the identical consumer descriptor.
    auto new_pool=CreateWrappedDescriptorPool(
        device,12u,3u,{VK_DESCRIPTOR_TYPE_STORAGE_BUFFER});
    std::array<VkDescriptorSetLayout,3> layouts{
        gemm.DescriptorLayout(),normalize.DescriptorLayout(),
        attention.DescriptorLayout()};
    auto allocated=CreateWrappedDescriptorSets(new_pool,
        vk::Span<VkDescriptorSetLayout>{layouts.data(),layouts.size()});
    if(allocated.IsOutOfPoolMemory()||allocated.size()!=3u)return false;
    const std::array<VkDescriptorBufferInfo,12> infos{
        b.input,b.weights,b.fp32,
        b.fp32,b.temperatures,b.query,b.key,b.value,
        b.query,b.key,b.value,b.context};
    std::array<VkWriteDescriptorSet,12> writes{};
    for(std::uint32_t i=0u;i<writes.size();++i){
        const std::uint32_t group=i<3u?0u:i<8u?1u:2u;
        const std::uint32_t binding=group==0u?i:
            group==1u?i-3u:i-8u;
        writes[i]=VkWriteDescriptorSet{
            .sType=VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,.pNext=nullptr,
            .dstSet=allocated[group],.dstBinding=binding,
            .dstArrayElement=0,.descriptorCount=1u,
            .descriptorType=VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
            .pImageInfo=nullptr,.pBufferInfo=&infos[i],
            .pTexelBufferView=nullptr};
    }
    device.GetLogical().UpdateDescriptorSets(
        vk::Span<VkWriteDescriptorSet>{writes.data(),writes.size()},{});
    pool=std::move(new_pool);
    sets=std::move(allocated);
    batch_plan=prepared;
    ready=true;
    return true;
}
bool NrRdna2VitQkvAttentionLink::InitializeWithModel(
    std::uint32_t tokens,
    const NR::NeuralModelFile& model,std::string_view prefix,
    MemoryAllocator& allocator,NrRdna2VitQkvAttentionBuffers buffers){
    if(ready||model_buffers)return false;
    auto proposed=std::make_unique<NrRdna2VitModelBuffers>(
        device,allocator);
    if(!proposed->Initialize(model,prefix))return false;
    buffers.weights=proposed->WeightDescriptor();
    buffers.temperatures=proposed->TemperatureDescriptor();
    if(!Initialize(tokens,buffers))return false;
    model_buffers=std::move(proposed);
    return true;
}
bool NrRdna2VitQkvAttentionLink::Record(){
    if(!ready||sets.size()!=3u||
       !NR::ValidVitQkvBatches(batch_plan))return false;
    // Each GEMM write is visible to QKV normalization, and each FP8 QKV
    // output is visible before the next pair or the global attention.
    // No CPU readback; transient FP32 allocation is reused for all slices.
    for(std::uint32_t i=0u;i<batch_plan.count;++i){
        const auto& slice=batch_plan.slices[i];
        if(!gemm.Record(sets[0],slice.gemm,slice.gemm_storage))
            return false;
        if(!normalize.Record(sets[1],slice.normalize,
                             slice.normalize_storage))return false;
    }
    return attention.Record(sets[2],batch_plan.attention,
                            batch_plan.attention_storage);
}
} // namespace Vulkan
