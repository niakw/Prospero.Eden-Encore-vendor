// SPDX-License-Identifier: MIT
// Five native CCVit1D operations, four GPU subgraphs, no CPU readback.
// Sources/scratch are caller-owned; all weights are model-loaded and owned.
#pragma once
#include "video_core/renderer_vulkan/nr_rdna2_model_file.h"
#include "video_core/vulkan_common/vulkan_wrapper.h"
#include <cstdint>
#include <memory>
namespace Vulkan {
class Device;
class MemoryAllocator;
class Scheduler;
struct NrRdna2VitBlockBuffers {
    VkDescriptorBufferInfo input{},hidden{},ffwd{},attention_context{},output{};
    VkDescriptorBufferInfo scratch{},query{},key{},value{};
};
class NrRdna2VitBlock final {
public:
    NrRdna2VitBlock(const Device&,MemoryAllocator&,Scheduler&);
    ~NrRdna2VitBlock();
    NrRdna2VitBlock(const NrRdna2VitBlock&)=delete;
    NrRdna2VitBlock& operator=(const NrRdna2VitBlock&)=delete;
    [[nodiscard]] bool InitializeWithModel(
        const Eden::Graphics::NeuralRDNA2::NeuralModelFile&,
        std::uint32_t model_block,std::uint32_t tokens,
        const NrRdna2VitBlockBuffers&);
    [[nodiscard]] bool Record();
    [[nodiscard]] VkDescriptorBufferInfo Result() const noexcept {
        return ready?output_info:VkDescriptorBufferInfo{};
    }
private:
    struct State;
    const Device& device;
    MemoryAllocator& allocator;
    Scheduler& scheduler;
    std::unique_ptr<State> state;
    VkDescriptorBufferInfo output_info{};
    bool ready{};
};
} // namespace Vulkan
