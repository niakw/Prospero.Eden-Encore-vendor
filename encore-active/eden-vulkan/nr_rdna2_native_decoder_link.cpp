// SPDX-License-Identifier: MIT
#include "video_core/renderer_vulkan/nr_rdna2_native_decoder_link.h"
#include "video_core/renderer_vulkan/present/util.h"
#include "video_core/vulkan_common/vulkan_device.h"
#include "video_core/vulkan_common/vulkan_memory_allocator.h"
#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <utility>
namespace Vulkan {
namespace NR=Eden::Graphics::NeuralRDNA2;
namespace {
[[nodiscard]] bool DistinctBuffers(
    const Device& device,VkDescriptorBufferInfo a,
    VkDescriptorBufferInfo b,VkDescriptorBufferInfo c,
    VkDescriptorBufferInfo d,VkDescriptorBufferInfo e,
    VkDescriptorBufferInfo f,VkDescriptorBufferInfo g){
    const std::array<VkDescriptorBufferInfo,7> items{a,b,c,d,e,f,g};
    for(std::size_t i=0;i<items.size();++i){
        const auto& item=items[i];
        if(!item.buffer||item.offset||!item.range||
           item.range==VK_WHOLE_SIZE||
           item.range>device.GetMaxStorageBufferRange())
            return false;
        for(std::size_t j=0;j<i;++j)
            if(items[j].buffer==item.buffer)return false;
    }
    return true;
}
}
NrRdna2NativeDecoderLink::NrRdna2NativeDecoderLink(
    const Device& d,MemoryAllocator& a,Scheduler& s)
    :device{d},allocator{a},scheduler{s},
     gemm{d,s},tile_to_linear{d,s},fp16{d,s},upsample{d,s}{}
NrRdna2NativeDecoderLink::~NrRdna2NativeDecoderLink()=default;
bool NrRdna2NativeDecoderLink::InitializeWithModel(
    const NR::NeuralModelFile& model,
    std::uint32_t decoder_block,std::uint32_t width,std::uint32_t height,
    VkDescriptorBufferInfo input,VkDescriptorBufferInfo linear_input,
    VkDescriptorBufferInfo transient,
    VkDescriptorBufferInfo fp16_projected,VkDescriptorBufferInfo raw_skip,
    VkDescriptorBufferInfo tiled_skip,VkDescriptorBufferInfo target){
    if(ready||weights||gain||skip_repack||!model.IsOpen()||
       !DistinctBuffers(device,input,linear_input,transient,
                        fp16_projected,raw_skip,tiled_skip,target))
        return false;
    // Without this layout bridge, scalar GEMM silently multiplies
    // unrelated raster positions from the ViT's padded tile sequence.
    if(!tile_to_linear.Initialize(
           {width,height},input,linear_input))return false;
    NR::NativeDecoderModelWeights local;
    if(!NR::PrepareNativeDecoderModelWeights(
        model,decoder_block,local))return false;
    auto owned_weights=std::make_unique<NrRdna2GpuWeights>(
        device,allocator);
    auto owned_gain=std::make_unique<NrRdna2GpuWeights>(
        device,allocator);
    if(!owned_weights->Initialize(local.matrix)||
       !owned_gain->Initialize(local.skip_gain))return false;
    NR::DecoderProjectionBatches prepared{};
    if(!NR::BuildDecoderProjectionBatches(
        width,height,{
            linear_input.range,owned_weights->Size(),
            transient.range,fp16_projected.range},prepared)||
       !NR::ValidDecoderProjectionBatches(prepared))return false;
    // Native decoder input is the ViT's tiled C1024 tensor. The exact
    // source need only be 1980 valid tokens (33x60) although the
    // zero-padded tile envelope contains 2160 positions.
    // The ProjPool primary skip is [window][64][512] row-major; the
    // decoder expects packed 4x4 token tiles with 16x16 channel blocks.
    // No CPU transfer; zero padded tokens before every decoder launch.
    const NR::SplitDecoderSkipRepack skip_shape{
        width,height,(width+7u)/8u,(height+7u)/8u,512u};
    auto prepared_skip=std::make_unique<NrRdna2SplitDecoderSkipRepack>(
        device,scheduler);
    if(!prepared_skip->Initialize(skip_shape,raw_skip,tiled_skip))
        return false;
    const NR::DecoderUpsample request{
        0u,0u,0u,0u,width,width,height};
    const NR::DecoderStorage storage{
        fp16_projected.range,tiled_skip.range,target.range,
        owned_gain->Size()};
    if(!NR::CanDecoderUpsample(request,storage))return false;
    auto pool=CreateWrappedDescriptorPool(
        device,9u,3u,{VK_DESCRIPTOR_TYPE_STORAGE_BUFFER});
    std::array<VkDescriptorSetLayout,3> layouts{
        gemm.DescriptorLayout(),fp16.DescriptorLayout(),
        upsample.DescriptorLayout()};
    auto sets=CreateWrappedDescriptorSets(pool,
        vk::Span<VkDescriptorSetLayout>{
            layouts.data(),layouts.size()});
    if(sets.IsOutOfPoolMemory()||sets.size()!=3u)return false;
    const std::array<VkDescriptorBufferInfo,9> infos{
        linear_input,owned_weights->Descriptor(),transient,
        transient,fp16_projected,
        fp16_projected,tiled_skip,target,owned_gain->Descriptor()};
    std::array<VkWriteDescriptorSet,9> writes{};
    for(std::uint32_t i=0;i<9u;++i){
        const auto which=i<3u?0u:i<5u?1u:2u;
        const auto binding=i<3u?i:i<5u?i-3u:i-5u;
        writes[i]=VkWriteDescriptorSet{
            .sType=VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
            .pNext=nullptr,
            .dstSet=sets[which],.dstBinding=binding,
            .dstArrayElement=0u,.descriptorCount=1u,
            .descriptorType=VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
            .pImageInfo=nullptr,.pBufferInfo=&infos[i],
            .pTexelBufferView=nullptr};
    }
    device.GetLogical().UpdateDescriptorSets(
        vk::Span<VkWriteDescriptorSet>{
            writes.data(),writes.size()},{});
    descriptor_pool=std::move(pool);
    descriptor_sets=std::move(sets);
    weights=std::move(owned_weights);
    gain=std::move(owned_gain);
    skip_repack=std::move(prepared_skip);
    projection_plan=prepared;
    upsample_plan=request;
    upsample_storage=storage;
    source=input;linear=linear_input;scratch=transient;
    projected=fp16_projected;
    skip=tiled_skip;output=target;
    ready=true;
    return true;
}
bool NrRdna2NativeDecoderLink::Record(){
    if(!ready||descriptor_sets.size()!=3u||!skip_repack||
       !NR::ValidDecoderProjectionBatches(projection_plan)||
       !NR::CanDecoderUpsample(upsample_plan,upsample_storage))
        return false;
    if(!skip_repack->Record()||!tile_to_linear.Record())return false;
    for(std::uint32_t i=0;i<projection_plan.count;++i){
        const auto& batch=projection_plan.slices[i];
        if(!gemm.Record(
              descriptor_sets[0],batch.projection,batch.storage))
            return false;
        if(!fp16.Record(
              descriptor_sets[1],scratch,projected,batch.convert))
            return false;
    }
    return upsample.Record(
        descriptor_sets[2],upsample_plan,upsample_storage);
}
} // namespace Vulkan
