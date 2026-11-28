// SPDX-License-Identifier: MIT
// Ordered GEMM FP8->FP32 and quantization FP32->FP8 on ONE Vulkan scheduler.
// Descriptor sets and all buffers are owned by the caller for GPU lifetime.
#pragma once
#include "video_core/renderer_vulkan/nr_rdna2_projection.h"
#include "video_core/renderer_vulkan/nr_rdna2_float_quantize.h"
#include "rdna2_nr_projection_quantized_policy.h"
namespace Vulkan {
class Device;
class Scheduler;
class NrRdna2ProjectionQuantized final {
public:
    NrRdna2ProjectionQuantized(const Device&,Scheduler&);
    ~NrRdna2ProjectionQuantized();
    [[nodiscard]] bool Record(
        VkDescriptorSet gemm_set,VkDescriptorSet quantize_set,
        VkDescriptorBufferInfo activations,VkDescriptorBufferInfo weights,
        VkDescriptorBufferInfo fp32_transient,VkDescriptorBufferInfo fp8_output,
        const Eden::Graphics::NeuralRDNA2::Projection& shape,
        std::uint64_t free_transient_bytes,
        std::uint32_t fp8_output_byte_offset=0u);
    // For ViT FFN matrices too wide for a single guarded GEMM: record
    // sequential token slices using the SAME immutable descriptors.
    // The FP32 transient is reused, but E4M3FN output spans do not overlap.
    [[nodiscard]] bool RecordBatched(
        VkDescriptorSet gemm_set,VkDescriptorSet quantize_set,
        VkDescriptorBufferInfo activations,VkDescriptorBufferInfo weights,
        VkDescriptorBufferInfo fp32_transient,VkDescriptorBufferInfo fp8_output,
        const Eden::Graphics::NeuralRDNA2::Projection& full_shape,
        std::uint64_t free_transient_bytes,
        std::uint32_t tokens_per_batch=1024u);
    [[nodiscard]] VkDescriptorSetLayout GemmLayout() const noexcept{
        return projection.DescriptorLayout();
    }
    [[nodiscard]] VkDescriptorSetLayout QuantizeLayout() const noexcept{
        return quantizer.DescriptorLayout();
    }
private:
    NrRdna2Projection projection;
    NrRdna2FloatQuantize quantizer;
};
} // namespace Vulkan
