// SPDX-License-Identifier: MIT
// Eight native ViT blocks in model dependency order, all on one Vulkan
// scheduler. Input from FinalHead and decoder output remain caller-owned.
#pragma once
#include "rdna2_nr_vit_native_route_policy.h"
#include "video_core/renderer_vulkan/nr_rdna2_vit_block.h"
#include <cstdint>
#include <memory>
#include <span>
#include <vector>
namespace Vulkan {
class Device;
class MemoryAllocator;
class Scheduler;
class NrRdna2VitRun final {
public:
    NrRdna2VitRun(const Device&,MemoryAllocator&,Scheduler&);
    ~NrRdna2VitRun();
    NrRdna2VitRun(const NrRdna2VitRun&)=delete;
    NrRdna2VitRun& operator=(const NrRdna2VitRun&)=delete;
    [[nodiscard]] bool InitializeWithModel(
        const Eden::Graphics::NeuralRDNA2::NeuralModelFile&,
        const Eden::Graphics::NeuralRDNA2::NativeVitRoute&,
        std::span<const NrRdna2VitBlockBuffers>);
    [[nodiscard]] bool Record();
    [[nodiscard]] VkDescriptorBufferInfo Result() const noexcept {
        return ready?result:VkDescriptorBufferInfo{};
    }
    [[nodiscard]] std::uint32_t BlockCount() const noexcept{
        return static_cast<std::uint32_t>(blocks.size());
    }
private:
    const Device& device;
    MemoryAllocator& allocator;
    Scheduler& scheduler;
    std::vector<std::unique_ptr<NrRdna2VitBlock>> blocks;
    VkDescriptorBufferInfo result{};
    bool ready{};
};
} // namespace Vulkan
