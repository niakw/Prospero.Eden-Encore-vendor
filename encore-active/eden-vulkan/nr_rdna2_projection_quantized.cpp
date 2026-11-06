// SPDX-License-Identifier: MIT
#include "video_core/renderer_vulkan/nr_rdna2_projection_quantized.h"
#include "video_core/vulkan_common/vulkan_device.h"
#include <array>
namespace Vulkan {
namespace NR=Eden::Graphics::NeuralRDNA2;
NrRdna2ProjectionQuantized::NrRdna2ProjectionQuantized(
    const Device& device,Scheduler& scheduler)
    :projection{device,scheduler},quantizer{device,scheduler}{}
NrRdna2ProjectionQuantized::~NrRdna2ProjectionQuantized()=default;
bool NrRdna2ProjectionQuantized::Record(
    VkDescriptorSet gemm_set,VkDescriptorSet quantize_set,
    VkDescriptorBufferInfo activations,VkDescriptorBufferInfo weights,
    VkDescriptorBufferInfo fp32_transient,VkDescriptorBufferInfo fp8_output,
    const NR::Projection& shape,std::uint64_t free_transient_bytes){
    if(!gemm_set||!quantize_set)return false;
    const std::array<VkDescriptorBufferInfo,4> buffers{
        activations,weights,fp32_transient,fp8_output};
    for(std::size_t i=0u;i<buffers.size();++i){
        if(!buffers[i].buffer||buffers[i].range==0u||
           buffers[i].range==VK_WHOLE_SIZE)return false;
        for(std::size_t j=0u;j<i;++j)
            if(buffers[i].buffer==buffers[j].buffer)return false;
    }
    const NR::Storage storage{
        activations.range,weights.range,fp32_transient.range,
        free_transient_bytes};
    NR::QuantizedProjectionPlan plan{};
    if(!NR::PrepareQuantizedProjection(
           shape,storage,fp8_output.range,plan))return false;
    // Both descriptor sets MUST be written by the caller using exactly the
    // same buffer descriptions. Preflight every stage before recording any.
    if(!projection.Record(gemm_set,shape,storage))return false;
    // GEMM's compute->compute barrier makes the FP32 transient visible.
    // The quantizer's barrier exposes the packed E4M3FN output downstream.
    return quantizer.Record(quantize_set,fp32_transient,fp8_output,plan.quant);
}
} // namespace Vulkan
