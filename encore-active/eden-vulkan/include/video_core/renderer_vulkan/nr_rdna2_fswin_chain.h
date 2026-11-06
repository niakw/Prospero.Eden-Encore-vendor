// SPDX-License-Identifier: MIT
// Sequential same-shape FSwin network section (unshifted token windows).
// Each block owns separate VMA resources; output remains a Vulkan buffer.
// Cross-resolution transfers, shifted windows and image preprocessing
// intentionally require explicit adapters not included in this class.
#pragma once
#include "video_core/renderer_vulkan/nr_rdna2_fswin_layer_runtime.h"
#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <vector>
namespace Vulkan {
class Device;
class Scheduler;
class MemoryAllocator;
struct FSwinLayerSpec {
    std::string prefix;
    std::uint32_t channels{};
    std::uint32_t windows{};
    bool residual_last{};
};
class NrRdna2FSwinChain final {
public:
    NrRdna2FSwinChain(
        const Device& device,MemoryAllocator& allocator,Scheduler& scheduler);
    ~NrRdna2FSwinChain();
    NrRdna2FSwinChain(const NrRdna2FSwinChain&)=delete;
    NrRdna2FSwinChain& operator=(const NrRdna2FSwinChain&)=delete;
    [[nodiscard]] bool Initialize(
        const Eden::Graphics::NeuralRDNA2::NeuralModelFile& model,
        std::span<const FSwinLayerSpec> layers);
    // Input is E4M3FN [window,64,C] and must be coherent with layer model.
    [[nodiscard]] bool UploadInput(
        std::span<const std::uint8_t> tokens);
    [[nodiscard]] bool Record();
    [[nodiscard]] bool Ready() const noexcept{return ready;}
    [[nodiscard]] std::size_t LayerCount() const noexcept {
        return layers.size();
    }
    [[nodiscard]] VkDescriptorBufferInfo Output() const noexcept {
        return layers.empty()?VkDescriptorBufferInfo{}:layers.back()->Output();
    }
private:
    const Device& device;
    MemoryAllocator& allocator;
    Scheduler& scheduler;
    std::vector<std::unique_ptr<NrRdna2FSwinLayerRuntime>> layers;
    bool ready{};
};
} // namespace Vulkan
