// SPDX-License-Identifier: MIT
// Import FSwin _upsample projection and a COMPLETE learned gain vector.
// The recovered C>=64 upsample_gain files may be 16 FP16 entries short.
// Do not pad with zeros or guess their position in residual_scale.
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
    std::vector<std::uint8_t> projection,raw_gain;
    std::vector<float> gains;
    if(!model.LoadModelMatrix(proj_name,channels,2u*channels,projection)||
       !model.LoadRaw(gain_name,raw_gain)||
       !NeuralDecodeF16(raw_gain,gains)||
       gains.size()!=channels)return false;
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
