// SPDX-License-Identifier: MIT
// Owned, strict one-shot Vulkan first-frame DLSS-NR Original/Compact graph.
// No embedded weights. The embedding renderer controls actual source,
// output image layouts, submission/fence lifetime and presentation.
#pragma once
#include "video_core/vulkan_common/vulkan_wrapper.h"
#include <memory>
#include <string_view>
namespace Vulkan {
class Device;
class MemoryAllocator;
class Scheduler;
class NrRdna2FrameSession final {
public:
    NrRdna2FrameSession(const Device&,MemoryAllocator&,Scheduler&);
    ~NrRdna2FrameSession();
    NrRdna2FrameSession(const NrRdna2FrameSession&)=delete;
    NrRdna2FrameSession& operator=(const NrRdna2FrameSession&)=delete;
    // Model is a *user-supplied* validated NRMODEL1 file; plan is the
    // matching exact native Original/Compact graph for this source extent.
    // Source VkImageView+VkSampler are already in GENERAL and shader
    // readable; output is a storage+sampled RGBA32F image in GENERAL.
    // No GPU work is submitted by Initialize.
    [[nodiscard]] bool Initialize(
        std::string_view model_path,std::string_view native_plan,
        VkImageView source_view,VkSampler source_sampler,
        VkFormat source_format,VkExtent2D source_extent,
        VkImageView output_view,VkFormat output_format,
        VkExtent2D output_extent,float paper_white,float intensity);
    // One-shot. Any failed Record may have enqueued commands. Do NOT
    // destroy until the embedding scheduler has retired that timeline.
    [[nodiscard]] bool Record();
    [[nodiscard]] bool Ready() const noexcept{return ready;}
    [[nodiscard]] VkImageView ResultView() const noexcept {
        return recorded?output_view:VK_NULL_HANDLE;
    }
private:
    struct State;
    const Device& device;
    MemoryAllocator& allocator;
    Scheduler& scheduler;
    std::unique_ptr<State> state;
    VkImageView output_view{VK_NULL_HANDLE};
    bool attempted{},ready{},record_attempted{},recorded{};
};
} // namespace Vulkan
