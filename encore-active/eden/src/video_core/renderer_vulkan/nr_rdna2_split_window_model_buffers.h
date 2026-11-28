// SPDX-License-Identifier: MIT
// Three immutable VMA upload buffers for user Split-Swin QKV model.
#pragma once
#include "video_core/renderer_vulkan/nr_rdna2_gpu_weights.h"
#include "video_core/renderer_vulkan/nr_rdna2_split_window_weight_loader.h"
#include <memory>
#include <string_view>
namespace Vulkan {
class Device;
class MemoryAllocator;
class NrRdna2SplitWindowModelBuffers final {
public:
    NrRdna2SplitWindowModelBuffers(const Device&,MemoryAllocator&);
    ~NrRdna2SplitWindowModelBuffers();
    [[nodiscard]] bool Initialize(
        const Eden::Graphics::NeuralRDNA2::NeuralModelFile&,
        std::string_view layer_prefix);
    [[nodiscard]] bool Ready() const noexcept{
        return bool(weights)&&bool(scales)&&bool(bias);
    }
    [[nodiscard]] VkDescriptorBufferInfo Weights() const noexcept{
        return Ready()?weights->Descriptor():VkDescriptorBufferInfo{};
    }
    [[nodiscard]] VkDescriptorBufferInfo Scales() const noexcept{
        return Ready()?scales->Descriptor():VkDescriptorBufferInfo{};
    }
    [[nodiscard]] VkDescriptorBufferInfo Bias() const noexcept{
        return Ready()?bias->Descriptor():VkDescriptorBufferInfo{};
    }
private:
    const Device& device;
    MemoryAllocator& allocator;
    std::unique_ptr<NrRdna2GpuWeights> weights,scales,bias;
};
}
