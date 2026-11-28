// SPDX-License-Identifier: MIT
// First R09 model segment: proxy -> learned input lift -> C32 FSwin.
// Device buffers and NRMODEL1 weights are owned by the embedding renderer.
// This adapter is not an HDR capture, full-network or present path.
#pragma once
#include "rdna2_nr_proxy_fswin_link_policy.h"
#include "video_core/renderer_vulkan/nr_rdna2_proxy_lift_link.h"
#include "video_core/renderer_vulkan/nr_rdna2_fswin_chain.h"

namespace Vulkan {
class Scheduler;
class NrRdna2ProxyFSwinLink final {
public:
    explicit NrRdna2ProxyFSwinLink(Scheduler& scheduler);
    NrRdna2ProxyFSwinLink(const NrRdna2ProxyFSwinLink&)=delete;
    NrRdna2ProxyFSwinLink& operator=(const NrRdna2ProxyFSwinLink&)=delete;

    // A failed Bind consumes this instance: reconstruct before retry.
    // All stages must already be initialized and remain alive until GPU
    // submission is completed; the FSwin terminal must be bound separately.
    [[nodiscard]] bool Bind(NrRdna2ProxyFeatureStage& proxy,
                            NrRdna2ImageFeatureStage& lift,
                            NrRdna2FSwinChain& first_fswin);
    // Record only proxy and the learned lift. Another ordered owner, e.g.
    // FSwinLevelLink or UNetSkipCache, must then record this FSwin producer
    // exactly once. Never call Record() after RecordEntryOnly().
    [[nodiscard]] bool RecordEntryOnly();
    // Exactly once: prefix AND this FSwin chain. Not for U-Net producer
    // chains which are recorded by a downstream level/skip scheduler.
    [[nodiscard]] bool Record();
    // This is an E4M3FN window tensor, NOT a ready-to-present image.
    // Valid to consume on the same ordered scheduler after Record().
    [[nodiscard]] VkDescriptorBufferInfo WindowOutput() const noexcept {
        return successful&&fswin?fswin->Output():VkDescriptorBufferInfo{};
    }
private:
    Scheduler& scheduler;
    NrRdna2ProxyLiftLink entry;
    NrRdna2FSwinChain* fswin{};
    bool attempted{},bound{},recorded{},successful{};
};
} // namespace Vulkan
