// SPDX-License-Identifier: MIT
// Optional authentic FP16 temporal blend coefficient, NEVER invented.
#pragma once
#include "video_core/renderer_vulkan/nr_rdna2_model_file.h"
#include "video_core/renderer_vulkan/nr_rdna2_weight_decode.h"
#include <cmath>
#include <cstdint>
#include <string>
#include <vector>
namespace Eden::Graphics::NeuralRDNA2 {
[[nodiscard]] inline bool PrepareNRTemporalBlendScale(
    const NeuralModelFile& model,std::uint32_t post_block,
    float& output) {
    output=0.0f;
    if(!model.IsOpen()||post_block>100u)return false;
    const auto name="unpacked-postblock/block"+
        std::to_string(post_block)+".layer0.layer.blend_scale.bin";
    std::vector<std::uint8_t> raw;
    std::vector<float> decoded;
    if(!model.LoadRaw(name,raw)||raw.size()!=2u||
       !NeuralDecodeF16(raw,decoded)||decoded.size()!=1u||
       !std::isfinite(decoded[0])||decoded[0]<0.0f||
       decoded[0]>1.0f)return false;
    output=decoded[0];
    return true;
}
} // namespace Eden::Graphics::NeuralRDNA2
