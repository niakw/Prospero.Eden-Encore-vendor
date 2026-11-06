// SPDX-License-Identifier: MIT
// One native Split-Swin 16-head GPU block, optional pooled branch from local NRMODEL1.
// Requires already-normalized row-major FP8 input and externally-owned
// scratch/intermediate buffers. Pool stage is mandatory for ProjPool.
#pragma once
#include "rdna2_nr_split_block_policy.h"
#include "video_core/renderer_vulkan/nr_rdna2_model_file.h"
#include "video_core/vulkan_common/vulkan_wrapper.h"
#include <cstdint>
#include <memory>
namespace Vulkan {
class Device;
class Scheduler;
class MemoryAllocator;
struct NrRdna2SplitSwinBlockBuffers {
    VkDescriptorBufferInfo input{},ffwd{},ffwd_projected{};
    VkDescriptorBufferInfo context_heads{},context_tokens{},projected{};
    VkDescriptorBufferInfo query{},key{},value{},fp32_scratch{};
    VkDescriptorBufferInfo pooled{}; // mandatory only for ProjPool
};
class NrRdna2SplitSwinBlock final {
public:
    NrRdna2SplitSwinBlock(const Device&,MemoryAllocator&,Scheduler&);
    ~NrRdna2SplitSwinBlock();
    NrRdna2SplitSwinBlock(const NrRdna2SplitSwinBlock&)=delete;
    NrRdna2SplitSwinBlock& operator=(const NrRdna2SplitSwinBlock&)=delete;
    [[nodiscard]] bool InitializeWithModel(
        const Eden::Graphics::NeuralRDNA2::NeuralModelFile&,
        std::uint32_t model_block,
        const Eden::Graphics::NeuralRDNA2::SplitBlockPlan& plan,
        const NrRdna2SplitSwinBlockBuffers&);
    [[nodiscard]] bool Record();
private:
    struct State;
    const Device& device;
    MemoryAllocator& allocator;
    Scheduler& scheduler;
    std::unique_ptr<State> state;
};
} // namespace Vulkan
