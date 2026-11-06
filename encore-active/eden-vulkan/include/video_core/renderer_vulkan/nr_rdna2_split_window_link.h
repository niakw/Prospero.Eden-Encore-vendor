// SPDX-License-Identifier: MIT
// Two real RDNA2 Vulkan dispatches: Q/K/V projection -> window attention.
// No implicit buffers, hidden CPU readback, model weights or PS5 activation.
#pragma once
#include "video_core/renderer_vulkan/nr_rdna2_qkv_projection.h"
#include "video_core/renderer_vulkan/nr_rdna2_window_attention.h"
#include "video_core/renderer_vulkan/nr_rdna2_model_file.h"
#include <memory>
#include <string_view>
#include "video_core/renderer_vulkan/nr_rdna2_split_window_link_policy.h"

namespace Vulkan {
class Device;
class Scheduler;
class MemoryAllocator;
class NrRdna2SplitWindowModelBuffers;
struct NrRdna2SplitWindowBuffers {
    VkDescriptorBufferInfo input{},weights{},scales{};
    VkDescriptorBufferInfo query{},key{},value{};
    VkDescriptorBufferInfo context{},bias{};
};
class NrRdna2SplitWindowLink final {
public:
    NrRdna2SplitWindowLink(const Device&,Scheduler&);
    ~NrRdna2SplitWindowLink();
    // Caller retains all eight backing VkBuffers and their contents until
    // the recorded GPU work completes. Initialize must be called once.
    [[nodiscard]] bool Initialize(
        const Eden::Graphics::NeuralRDNA2::SplitWindowLink& plan,
        const NrRdna2SplitWindowBuffers& buffers);
    // Bind model-owned immutable QKV weights, scales and attention bias.
    [[nodiscard]] bool InitializeWithModel(
        const Eden::Graphics::NeuralRDNA2::SplitWindowLink& plan,
        const Eden::Graphics::NeuralRDNA2::NeuralModelFile& model,
        std::string_view layer_prefix,MemoryAllocator& allocator,
        NrRdna2SplitWindowBuffers buffers);
    [[nodiscard]] bool Record();
private:
    const Device& device;
    NrRdna2QkvProjection qkv;
    NrRdna2WindowAttention attention;
    vk::DescriptorPool pool;
    vk::DescriptorSets allocations;
    Eden::Graphics::NeuralRDNA2::SplitWindowLink plan{};
    Eden::Graphics::NeuralRDNA2::SplitWindowLinkStorage storage{};
    std::unique_ptr<NrRdna2SplitWindowModelBuffers> model_buffers;
    bool initialized{};
};
} // namespace Vulkan
