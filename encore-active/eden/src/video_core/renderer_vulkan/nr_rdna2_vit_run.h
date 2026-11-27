// SPDX-License-Identifier: MIT
// Eight native ViT blocks in model dependency order, all on one Vulkan
// scheduler. Input from FinalHead and decoder output remain caller-owned.
#pragma once
#include "rdna2_nr_vit_native_route_policy.h"
#include "video_core/renderer_vulkan/nr_rdna2_vit_block.h"
#include "video_core/renderer_vulkan/nr_rdna2_vit_boundary_repack.h"
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
        std::span<const NrRdna2VitBlockBuffers>,
        VkDescriptorBufferInfo source_split_tiles={},
        VkDescriptorBufferInfo destination_decoder_tiles={});
    [[nodiscard]] bool Record();
    [[nodiscard]] VkDescriptorBufferInfo Result() const noexcept {
        return ready?result:VkDescriptorBufferInfo{};
    }
    [[nodiscard]] bool Ready() const noexcept{return ready;}
    [[nodiscard]] Scheduler* SchedulerIdentity() const noexcept {
        return &scheduler;
    }
    [[nodiscard]] VkDescriptorBufferInfo SourceSplitTiles() const noexcept {
        return ready&&has_incoming?incoming_source:VkDescriptorBufferInfo{};
    }
    [[nodiscard]] VkDescriptorBufferInfo DecoderTiles() const noexcept {
        return ready&&has_outgoing?outgoing_target:VkDescriptorBufferInfo{};
    }
    [[nodiscard]] std::uint32_t BlockCount() const noexcept{
        return static_cast<std::uint32_t>(blocks.size());
    }
private:
    const Device& device;
    MemoryAllocator& allocator;
    Scheduler& scheduler;
    std::vector<std::unique_ptr<NrRdna2VitBlock>> blocks;
    NrRdna2VitBoundaryRepack boundary;
    vk::DescriptorPool boundary_pool;
    vk::DescriptorSets boundary_sets;
    Eden::Graphics::NeuralRDNA2::VitBoundaryRepack incoming_shape{},outgoing_shape{};
    Eden::Graphics::NeuralRDNA2::VitBoundaryStorage incoming_storage{},outgoing_storage{};
    VkDescriptorBufferInfo incoming_source{},incoming_target{};
    VkDescriptorBufferInfo outgoing_source{},outgoing_target{};
    VkDescriptorBufferInfo result{};
    bool ready{},has_incoming{},has_outgoing{};
};
} // namespace Vulkan
