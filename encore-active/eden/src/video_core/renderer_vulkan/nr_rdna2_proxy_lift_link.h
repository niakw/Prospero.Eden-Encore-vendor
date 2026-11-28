// SPDX-License-Identifier: MIT
// GPU no-history entry: normalized proxy -> FP16[16] -> learned FP8[32].
// Requires a local NRMODEL1 prepared by the embedding renderer. No game hook.
#pragma once
#include "rdna2_nr_proxy_lift_link_policy.h"
#include "video_core/renderer_vulkan/nr_rdna2_proxy_feature_stage.h"
#include "video_core/renderer_vulkan/nr_rdna2_image_feature_stage.h"
namespace Vulkan {
class Scheduler;
class NrRdna2ProxyLiftLink final {
public:
    explicit NrRdna2ProxyLiftLink(Scheduler&);
    NrRdna2ProxyLiftLink(const NrRdna2ProxyLiftLink&)=delete;
    NrRdna2ProxyLiftLink& operator=(const NrRdna2ProxyLiftLink&)=delete;
    [[nodiscard]] bool Bind(
        NrRdna2ProxyFeatureStage& proxy,NrRdna2ImageFeatureStage& lift);
    [[nodiscard]] bool Record();
    [[nodiscard]] VkDescriptorBufferInfo Result() const noexcept {
        return successful&&lift?lift->Result():VkDescriptorBufferInfo{};
    }
private:
    Scheduler& scheduler;
    NrRdna2ProxyFeatureStage* proxy{};
    NrRdna2ImageFeatureStage* lift{};
    bool ready{},attempted{},successful{};
};
} // namespace Vulkan
