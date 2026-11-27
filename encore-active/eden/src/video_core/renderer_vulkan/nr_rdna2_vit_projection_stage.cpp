// SPDX-License-Identifier: MIT
#include "video_core/renderer_vulkan/nr_rdna2_vit_projection_stage.h"
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
[[nodiscard]] bool ValidBuffers(
    NR::VitProjectionKind kind,VkDeviceSize limit,
    const VkDescriptorBufferInfo& input,
    const VkDescriptorBufferInfo& scratch,
    const VkDescriptorBufferInfo& output,
    const VkDescriptorBufferInfo& residual) noexcept {
    const auto valid=[limit](const VkDescriptorBufferInfo& b) {
        return b.buffer&&b.offset==0u&&b.range>0u&&
               b.range!=VK_WHOLE_SIZE&&b.range<=limit;
    };
    if(!valid(input)||!valid(scratch)||!valid(output)||
       input.buffer==scratch.buffer||input.buffer==output.buffer||
       scratch.buffer==output.buffer)return false;
    if(kind==NR::VitProjectionKind::Expand)
        return !residual.buffer;
    return valid(residual)&&
           residual.buffer!=input.buffer&&
           residual.buffer!=scratch.buffer&&
           residual.buffer!=output.buffer;
}
}
NrRdna2VitProjectionStage::NrRdna2VitProjectionStage(
    const Device& d,MemoryAllocator& a,Scheduler& s,
    NR::VitProjectionKind kind_)
    :device{d},allocator{a},kind{kind_},gemm{d,s},epilogue{d,s}{}
NrRdna2VitProjectionStage::~NrRdna2VitProjectionStage()=default;
bool NrRdna2VitProjectionStage::InitializeWithModel(
    const NR::NeuralModelFile& model,
    std::uint32_t block,std::uint32_t tokens,
    VkDescriptorBufferInfo input,VkDescriptorBufferInfo scratch,
    VkDescriptorBufferInfo output,VkDescriptorBufferInfo residual){
    if(ready||weights||gain||
       !ValidBuffers(kind,device.GetMaxStorageBufferRange(),
                     input,scratch,output,residual))return false;
    NR::VitProjectionLayerWeights loaded;
    if(!NR::PrepareVitProjectionLayerWeights(model,block,kind,loaded))
        return false;
    auto proposed_weights=std::make_unique<NrRdna2GpuWeights>(
        device,allocator);
    if(!proposed_weights->Initialize(loaded.matrix))return false;
    std::unique_ptr<NrRdna2GpuWeights> proposed_gain;
    if(kind!=NR::VitProjectionKind::Expand){
        proposed_gain=std::make_unique<NrRdna2GpuWeights>(
            device,allocator);
        if(!proposed_gain->Initialize(loaded.gain_f16))return false;
    }
    NR::VitProjectionStageBatches prepared{};
    const NR::VitProjectionStageCapacity cap{
        input.range,proposed_weights->Size(),scratch.range,
        residual.range,proposed_gain?proposed_gain->Size():0u,
        output.range};
    if(!NR::BuildVitProjectionStage(kind,tokens,cap,prepared)||
       !NR::ValidateVitProjectionStage(prepared))return false;
    auto new_pool=CreateWrappedDescriptorPool(
        device,7u,2u,{VK_DESCRIPTOR_TYPE_STORAGE_BUFFER});
    std::array<VkDescriptorSetLayout,2> layouts{
        gemm.DescriptorLayout(),
        kind==NR::VitProjectionKind::Expand?
            epilogue.ActivationLayout():epilogue.ResidualLayout()};
    auto sets=CreateWrappedDescriptorSets(new_pool,
        vk::Span<VkDescriptorSetLayout>{layouts.data(),layouts.size()});
    if(sets.IsOutOfPoolMemory()||sets.size()!=2u)return false;
    const std::array<VkDescriptorBufferInfo,7> infos{
        input,proposed_weights->Descriptor(),scratch,
        scratch,kind==NR::VitProjectionKind::Expand?output:residual,
        proposed_gain?proposed_gain->Descriptor():VkDescriptorBufferInfo{},
        output};
    const std::uint32_t count=kind==NR::VitProjectionKind::Expand?5u:7u;
    std::array<VkWriteDescriptorSet,7> writes{};
    for(std::uint32_t i=0u;i<count;++i){
        writes[i]=VkWriteDescriptorSet{
            .sType=VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,.pNext=nullptr,
            .dstSet=i<3u?sets[0]:sets[1],
            .dstBinding=i<3u?i:i-3u,.dstArrayElement=0u,
            .descriptorCount=1u,
            .descriptorType=VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
            .pImageInfo=nullptr,.pBufferInfo=&infos[i],
            .pTexelBufferView=nullptr};
    }
    device.GetLogical().UpdateDescriptorSets(
        vk::Span<VkWriteDescriptorSet>{writes.data(),count},{});
    descriptor_pool=std::move(new_pool);
    descriptor_sets=std::move(sets);
    weights=std::move(proposed_weights);
    gain=std::move(proposed_gain);
    batches=prepared;
    scratch_info=scratch;
    residual_info=residual;
    output_info=output;
    gain_info=gain?gain->Descriptor():VkDescriptorBufferInfo{};
    ready=true;
    return true;
}
bool NrRdna2VitProjectionStage::Record(){
    if(!ready||descriptor_sets.size()!=2u||
       !NR::ValidateVitProjectionStage(batches))return false;
    // The full batch schedule was preflighted before publishing this stage.
    // Every GEMM barrier exposes the reused FP32 scratch to the epilogue;
    // epilogue barriers protect each disjoint FP8 output segment.
    for(std::uint32_t i=0u;i<batches.count;++i){
        const auto& s=batches.slices[i];
        if(!gemm.Record(descriptor_sets[0],s.gemm,s.gemm_storage))
            return false;
        if(kind==NR::VitProjectionKind::Expand){
            if(!epilogue.RecordActivation(
                  descriptor_sets[1],scratch_info,output_info,
                  s.activation))return false;
        } else if(!epilogue.RecordResidual(
                      descriptor_sets[1],scratch_info,residual_info,
                      gain_info,output_info,s.residual))return false;
    }
    return true;
}
} // namespace Vulkan
