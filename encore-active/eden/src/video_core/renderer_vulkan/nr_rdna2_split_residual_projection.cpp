// SPDX-License-Identifier: MIT
#include "video_core/renderer_vulkan/nr_rdna2_split_residual_projection.h"
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
    VkDescriptorBufferInfo input,VkDescriptorBufferInfo residual,
    VkDescriptorBufferInfo scratch,VkDescriptorBufferInfo output,
    VkDeviceSize max_range){
    const std::array<VkDescriptorBufferInfo,4> all{
        input,residual,scratch,output};
    for(const auto& x:all)if(!x.buffer||x.offset||!x.range||
        x.range==VK_WHOLE_SIZE||x.range>max_range)return false;
    // Input and residual are read-only and may intentionally share a buffer.
    // All writable resources must be distinct from the sources and each other.
    return input.buffer!=scratch.buffer&&
        input.buffer!=output.buffer&&
        residual.buffer!=scratch.buffer&&
        residual.buffer!=output.buffer&&
        scratch.buffer!=output.buffer;
}
}
NrRdna2SplitResidualProjection::NrRdna2SplitResidualProjection(
    const Device& d,MemoryAllocator& a,Scheduler& s)
    :device{d},allocator{a},gemm{d,s},epilogue{d,s}{}
NrRdna2SplitResidualProjection::~NrRdna2SplitResidualProjection()=default;
bool NrRdna2SplitResidualProjection::InitializeWithModel(
    const NR::NeuralModelFile& model,std::string_view prefix,
    std::uint32_t tokens,VkDescriptorBufferInfo activations,
    VkDescriptorBufferInfo residual,VkDescriptorBufferInfo scratch,
    VkDescriptorBufferInfo output) {
    if(ready||matrix||gain||!ValidExternal(
          activations,residual,scratch,output,
          device.GetMaxStorageBufferRange()))return false;
    NR::SplitResidualWeights loaded;
    if(!NR::PrepareSplitResidualWeights(model,prefix,loaded))return false;
    auto new_matrix=std::make_unique<NrRdna2GpuWeights>(device,allocator);
    auto new_gain=std::make_unique<NrRdna2GpuWeights>(device,allocator);
    if(!new_matrix->Initialize(loaded.matrix)||
       !new_gain->Initialize(loaded.gain_f16))return false;
    const NR::Projection p{tokens,512u,512u,512u,512u,0u,0u};
    const NR::Storage ps{
        activations.range,new_matrix->Size(),scratch.range,scratch.range};
    const NR::VitResidual r{tokens,512u,0u,0u,0u,0u};
    const NR::VitEpilogueStorage rs{
        scratch.range,residual.range,new_gain->Size(),output.range};
    if(!NR::CanDispatch(p,ps)||!NR::CanVitResidual(r,rs))
        return false;
    auto new_pool=CreateWrappedDescriptorPool(
        device,7u,2u,{VK_DESCRIPTOR_TYPE_STORAGE_BUFFER});
    std::array<VkDescriptorSetLayout,2> layouts{
        gemm.DescriptorLayout(),epilogue.ResidualLayout()};
    auto allocated=CreateWrappedDescriptorSets(
        new_pool,vk::Span<VkDescriptorSetLayout>{
            layouts.data(),layouts.size()});
    if(allocated.IsOutOfPoolMemory()||allocated.size()!=2u)return false;
    const std::array<VkDescriptorBufferInfo,7> info{
        activations,new_matrix->Descriptor(),scratch,
        scratch,residual,new_gain->Descriptor(),output};
    std::array<VkWriteDescriptorSet,7> writes{};
    for(std::uint32_t i=0u;i<7u;++i){
        writes[i]=VkWriteDescriptorSet{
            .sType=VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,.pNext=nullptr,
            .dstSet=i<3u?allocated[0]:allocated[1],
            .dstBinding=i<3u?i:i-3u,.dstArrayElement=0,
            .descriptorCount=1u,.descriptorType=VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
            .pImageInfo=nullptr,.pBufferInfo=&info[i],
            .pTexelBufferView=nullptr};
    }
    device.GetLogical().UpdateDescriptorSets(
        vk::Span<VkWriteDescriptorSet>{writes.data(),writes.size()},{});
    pool=std::move(new_pool);
    sets=std::move(allocated);
    matrix=std::move(new_matrix);
    gain=std::move(new_gain);
    projection_shape=p;
    projection_storage=ps;
    residual_shape=r;
    residual_storage=rs;
    scratch_info=scratch;
    residual_info=residual;
    gain_info=gain->Descriptor();
    out_info=output;
    ready=true;
    return true;
}
bool NrRdna2SplitResidualProjection::Record(){
    if(!ready||sets.size()!=2u||
       !NR::CanDispatch(projection_shape,projection_storage)||
       !NR::CanVitResidual(residual_shape,residual_storage))return false;
    if(!gemm.Record(sets[0],projection_shape,projection_storage))
        return false;
    // GEMM's compute->compute barrier protects the FP32 scratch read.
    return epilogue.RecordResidual(
        sets[1],scratch_info,residual_info,gain_info,out_info,
        residual_shape);
}
} // namespace Vulkan
