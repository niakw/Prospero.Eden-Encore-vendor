// SPDX-License-Identifier: MIT
// RDNA2 six-pass FSwin graph: dense 0/1/2 -> QKV -> window context -> dense 3.
// Descriptor binding ownership, model weights and resource lifetimes belong
// to the embedding renderer. This type only orders validated Vulkan passes.
#pragma once
#include <array>
#include "rdna2_nr_fswin_graph_policy.h"
#include "video_core/renderer_vulkan/nr_rdna2_fswin_dense.h"
#include "video_core/renderer_vulkan/nr_rdna2_fswin_qkv.h"
#include "video_core/renderer_vulkan/nr_rdna2_window_attention.h"
namespace Vulkan {
class Device;
class Scheduler;
struct FSwinGraphDescriptorSets {
    std::array<VkDescriptorSet,4> dense{};
    VkDescriptorSet qkv{};
    VkDescriptorSet attention{};
};
class NrRdna2FSwinGraph final {
public:
    NrRdna2FSwinGraph(const Device& device,Scheduler& scheduler);
    ~NrRdna2FSwinGraph();
    // No commands recorded if a descriptor is null or contract invalid.
    // Caller must bind each intermediate producer to the following consumer.
    [[nodiscard]] bool Record(
        const FSwinGraphDescriptorSets& sets,
        const Eden::Graphics::NeuralRDNA2::FSwinGraph& plan);
private:
    NrRdna2FSwinDense dense;
    NrRdna2FSwinQkv qkv;
    NrRdna2WindowAttention attention;
};
} // namespace Vulkan
