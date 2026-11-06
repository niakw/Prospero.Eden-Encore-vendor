// SPDX-License-Identifier: MIT
// Execute one native Split-Swin16H run (8 Original / 2 Compact blocks).
// Strictly validates predecessor buffers and preserves all model allocations.
// Input/output boundary 2D feature-view transfers are still external.
#pragma once
#include "rdna2_nr_split_native_routes_policy.h"
#include "video_core/renderer_vulkan/nr_rdna2_split_swin_block.h"
#include <cstdint>
#include <memory>
#include <span>
#include <vector>
namespace Vulkan {
class Device;
class MemoryAllocator;
class Scheduler;
class NrRdna2SplitSwinRun final {
public:
    NrRdna2SplitSwinRun(const Device&,MemoryAllocator&,Scheduler&);
    ~NrRdna2SplitSwinRun();
    NrRdna2SplitSwinRun(const NrRdna2SplitSwinRun&)=delete;
    NrRdna2SplitSwinRun& operator=(const NrRdna2SplitSwinRun&)=delete;
    [[nodiscard]] bool InitializeWithModel(
        const Eden::Graphics::NeuralRDNA2::NeuralModelFile&,
        const Eden::Graphics::NeuralRDNA2::NativeSplitRoutes&,
        std::uint32_t run_index,
        std::span<const NrRdna2SplitSwinBlockBuffers> buffers);
    [[nodiscard]] bool Record();
    [[nodiscard]] std::uint32_t BlockCount() const noexcept {
        return static_cast<std::uint32_t>(blocks.size());
    }
    [[nodiscard]] VkDescriptorBufferInfo Result() const noexcept {
        return ready?result:VkDescriptorBufferInfo{};
    }
    [[nodiscard]] bool RequiresInputView() const noexcept {
        return ready&&input_view;
    }
    [[nodiscard]] bool RequiresOutputView() const noexcept {
        return ready&&output_view;
    }
private:
    const Device& device;
    MemoryAllocator& allocator;
    Scheduler& scheduler;
    std::vector<std::unique_ptr<NrRdna2SplitSwinBlock>> blocks;
    VkDescriptorBufferInfo result{};
    bool ready{},input_view{},output_view{};
};
} // namespace Vulkan
