// SPDX-License-Identifier: MIT
// Native 512-wide Split-Swin FFWDProj/Proj weight+residual reader.
#pragma once
#include "video_core/renderer_vulkan/nr_rdna2_model_file.h"
#include "video_core/renderer_vulkan/nr_rdna2_weight_decode.h"
#include "video_core/renderer_vulkan/nr_rdna2_e4m3_requantize.h"
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>
namespace Eden::Graphics::NeuralRDNA2 {
struct SplitResidualWeights {
    std::vector<std::uint8_t> matrix;
    std::vector<std::uint8_t> gain_f16;
};
[[nodiscard]] inline bool DecodeSplitResidualWeights(
    std::span<const std::uint8_t> matrix,
    std::span<const std::uint8_t> gain,
    SplitResidualWeights& output) {
    output={};
    if(matrix.size()!=512u*512u||gain.size()!=512u*2u)
        return false;
    for(auto byte:matrix)if((byte&0x7fu)==0x7fu)return false;
    std::vector<float> gain_values;
    if(!NeuralDecodeF16(gain,gain_values)||
       gain_values.size()!=512u)return false;
    SplitResidualWeights prepared;
    prepared.matrix.assign(matrix.begin(),matrix.end());
    prepared.gain_f16.assign(gain.begin(),gain.end());
    RequantizeE4M3(prepared.matrix);
    output=std::move(prepared);
    return true;
}
[[nodiscard]] inline bool PrepareSplitResidualWeights(
    const NeuralModelFile& model,std::string_view prefix,
    SplitResidualWeights& output) {
    output={};
    if(!model.IsOpen()||prefix.empty())return false;
    const std::string base(prefix);
    if(!ValidNeuralWeightName(base+".weight.bin")||
       !ValidNeuralWeightName(base+".skip_weight.bin"))return false;
    std::vector<std::uint8_t> matrix,gain;
    if(!model.LoadModelMatrix(base+".weight.bin",512u,512u,matrix)||
       !model.LoadRaw(base+".skip_weight.bin",gain))return false;
    return DecodeSplitResidualWeights(matrix,gain,output);
}
} // namespace Eden::Graphics::NeuralRDNA2
