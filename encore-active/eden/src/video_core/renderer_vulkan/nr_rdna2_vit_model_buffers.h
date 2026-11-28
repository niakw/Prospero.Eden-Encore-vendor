// SPDX-License-Identifier: MIT
// Own two immutable VMA upload allocations for a local ViT QKV model.
// Separate buffers: GEMM [3072,1024] FP8 and 32 FP32 head temperatures.
#pragma once
#include "video_core/renderer_vulkan/nr_rdna2_gpu_weights.h"
#include "video_core/renderer_vulkan/nr_rdna2_vit_qkv_weight_loader.h"
#include <memory>
#include <string_view>
namespace Vulkan {
class Device;
class MemoryAllocator;
class NrRdna2VitModelBuffers final {
public:
    NrRdna2VitModelBuffers(const Device&,MemoryAllocator&);
    ~NrRdna2VitModelBuffers();
    NrRdna2VitModelBuffers(const NrRdna2VitModelBuffers&)=delete;
    NrRdna2VitModelBuffers& operator=(const NrRdna2VitModelBuffers&)=delete;
    [[nodiscard]] bool Initialize(
        const Eden::Graphics::NeuralRDNA2::NeuralModelFile&,
        std::string_view layer_prefix);
    [[nodiscard]] bool Ready() const noexcept {
        return bool(weights)&&bool(temperatures);
    }
    [[nodiscard]] VkDescriptorBufferInfo WeightDescriptor() const noexcept {
        return Ready()?weights->Descriptor():VkDescriptorBufferInfo{};
    }
    [[nodiscard]] VkDescriptorBufferInfo TemperatureDescriptor() const noexcept {
        return Ready()?temperatures->Descriptor():VkDescriptorBufferInfo{};
    }
private:
    const Device& device;
    MemoryAllocator& allocator;
    std::unique_ptr<NrRdna2GpuWeights> weights;
    std::unique_ptr<NrRdna2GpuWeights> temperatures;
};
}
