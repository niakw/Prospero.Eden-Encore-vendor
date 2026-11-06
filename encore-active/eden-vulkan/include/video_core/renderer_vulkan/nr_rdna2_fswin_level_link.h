// SPDX-License-Identifier: MIT
// Connect a terminal FSwin _ds stage to the _inpview FSwin section at
// the next U-Net resolution. This is one complete ordered GPU level edge,
// not a global U-Net/ViT scheduler.
#pragma once
#include "rdna2_nr_fswin_level_link_policy.h"
#include "video_core/renderer_vulkan/nr_rdna2_fswin_chain.h"
#include <cstdint>
namespace Vulkan {
class NrRdna2FSwinLevelLink final {
public:
    NrRdna2FSwinLevelLink()=default;
    NrRdna2FSwinLevelLink(const NrRdna2FSwinLevelLink&)=delete;
    NrRdna2FSwinLevelLink& operator=(const NrRdna2FSwinLevelLink&)=delete;
    // The output buffer is owned by the caller and must survive until all
    // commands are finished. Failed preparation requires fresh chains.
    [[nodiscard]] bool Prepare(
        NrRdna2FSwinChain& from,NrRdna2FSwinChain& to,
        const Eden::Graphics::NeuralRDNA2::NeuralModelFile& model,
        VkDescriptorBufferInfo output_raster,
        std::uint32_t input_width,std::uint32_t input_height);
    // On success, record the DS block and then the next level's
    // InputView/FSwin chain on the SAME scheduler in submission order.
    [[nodiscard]] bool Record();
    [[nodiscard]] bool Ready() const noexcept{return ready;}
private:
    NrRdna2FSwinChain* source{};
    NrRdna2FSwinChain* target{};
    bool ready{};
};
} // namespace Vulkan
