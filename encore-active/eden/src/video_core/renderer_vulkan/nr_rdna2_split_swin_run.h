// SPDX-License-Identifier: MIT
// Execute one native Split-Swin16H run (8 Original / 2 Compact blocks).
// Strictly validates predecessor buffers and preserves all model allocations.
// Native InputView/OutputView GPU raster transfers are recorded in order.
#pragma once
#include "rdna2_nr_split_native_routes_policy.h"
#include "video_core/renderer_vulkan/nr_rdna2_split_swin_block.h"
#include "video_core/renderer_vulkan/nr_rdna2_fswin_raster_layout.h"
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
        std::span<const NrRdna2SplitSwinBlockBuffers> buffers,
        VkDescriptorBufferInfo source_raster={},
        VkDescriptorBufferInfo destination_raster={});
    [[nodiscard]] bool Record();
    [[nodiscard]] std::uint32_t BlockCount() const noexcept {
        return static_cast<std::uint32_t>(blocks.size());
    }
    [[nodiscard]] VkDescriptorBufferInfo Result() const noexcept {
        return ready?result:VkDescriptorBufferInfo{};
    }
    [[nodiscard]] bool Ready() const noexcept {return ready;}
    [[nodiscard]] Scheduler* SchedulerIdentity() const noexcept {
        return &scheduler;
    }
    [[nodiscard]] VkDescriptorBufferInfo Input() const noexcept {
        return ready?first_input:VkDescriptorBufferInfo{};
    }
    // The raw C512 input-view raster (not the padded window buffer).
    [[nodiscard]] VkDescriptorBufferInfo SourceRaster() const noexcept {
        return ready&&input_view?input_raster:VkDescriptorBufferInfo{};
    }
    // First Split run: ProjPool primary C512 branch is retained as the
    // decoder's learned skip while FinalHead C1024 enters ViT.
    [[nodiscard]] VkDescriptorBufferInfo PrimaryResult() const noexcept {
        return ready?primary_result:VkDescriptorBufferInfo{};
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
    NrRdna2FSwinRasterLayout raster_layout;
    vk::DescriptorPool raster_pool;
    vk::DescriptorSets raster_sets;
    Eden::Graphics::NeuralRDNA2::FSwinRasterLayout input_shape{},output_shape{};
    Eden::Graphics::NeuralRDNA2::FSwinRasterLayoutStorage input_sizes{},output_sizes{};
    VkDescriptorBufferInfo first_input{},input_raster{},primary_result{},result{};
    bool ready{},input_view{},output_view{},record_attempted{},recorded{};
};
} // namespace Vulkan
