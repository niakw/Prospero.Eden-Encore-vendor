// SPDX-License-Identifier: MIT
// Import FSwin _upsample projection and a COMPLETE learned gain vector.
// C>=64: the upstream unpacker put the first 16 transition gains at
// the end of residual_scale, followed by C-16 gains in upsample_gain.
// Recover only when the exact two-region byte layout is present.
#pragma once
#include "video_core/renderer_vulkan/nr_rdna2_model_file.h"
#include "video_core/renderer_vulkan/nr_rdna2_weight_arena.h"
#include "video_core/renderer_vulkan/nr_rdna2_e4m3_requantize.h"
#include "video_core/renderer_vulkan/nr_rdna2_weight_decode.h"
#include <cstdint>
#include <string>
#include <string_view>
#include <utility>
#include <vector>
namespace Eden::Graphics::NeuralRDNA2 {
struct FSwinUpsampleWeightRanges {
    NeuralWeightRange projection{};
    NeuralWeightRange skip_gain{};
    std::uint32_t channels{};
};
[[nodiscard]] inline bool PrepareFSwinUpsampleWeights(
    const NeuralModelFile& model,std::string_view layer_prefix,
    std::uint32_t channels,NeuralWeightArena& output,
    FSwinUpsampleWeightRanges& ranges){
    ranges={};
    if(channels!=32u&&channels!=64u&&
       channels!=128u&&channels!=256u)return false;
    const std::string base{layer_prefix};
    const std::string proj_name=base+".resample.bin";
    const std::string gain_name=base+".upsample_gain.bin";
    if(!ValidNeuralWeightName(proj_name)||
       !ValidNeuralWeightName(gain_name))return false;
    std::vector<std::uint8_t> projection,raw_gain,raw_residual;
    std::vector<float> gain_fragment,residual_region,gains;
    if(!model.LoadModelMatrix(proj_name,channels,2u*channels,projection)||
       !model.LoadRaw(gain_name,raw_gain)||
       !NeuralDecodeF16(raw_gain,gain_fragment))
        return false;
    if(channels>=64u&&gain_fragment.size()==channels-16u){
        const auto residual_name=base+".residual_scale.bin";
        if(!ValidNeuralWeightName(residual_name)||
           !model.LoadRaw(residual_name,raw_residual)||
           !NeuralDecodeF16(raw_residual,residual_region))
            return false;
    }
    if(!NeuralRecoverUpsampleGains(
           gain_fragment,residual_region,channels,gains))
        return false;
    RequantizeE4M3(projection);
    NeuralWeightArena prepared;
    FSwinUpsampleWeightRanges staged{};
    if(!prepared.AppendBytes(proj_name,projection,staged.projection)||
       !prepared.AppendFP32(gain_name,gains,staged.skip_gain))
        return false;
    staged.channels=channels;
    output=std::move(prepared);
    ranges=staged;
    return true;
}
} // namespace Eden::Graphics::NeuralRDNA2
