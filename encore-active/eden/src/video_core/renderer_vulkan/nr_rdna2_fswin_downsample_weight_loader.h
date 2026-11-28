// SPDX-License-Identifier: MIT
// Import only the learned 2C x C resample matrix for a FSwin _ds layer.
// Source: user-provided NRMODEL1, row-major E4M3FN. No proprietary weights.
#pragma once
#include "video_core/renderer_vulkan/nr_rdna2_model_file.h"
#include "video_core/renderer_vulkan/nr_rdna2_weight_arena.h"
#include "video_core/renderer_vulkan/nr_rdna2_e4m3_requantize.h"
#include <cstdint>
#include <string>
#include <string_view>
#include <utility>
#include <vector>
namespace Eden::Graphics::NeuralRDNA2 {
[[nodiscard]] inline bool PrepareFSwinDownsampleWeights(
    const NeuralModelFile& model,std::string_view layer_prefix,
    std::uint32_t channels,NeuralWeightArena& output,
    NeuralWeightRange& range){
    range={};
    if(channels!=32u&&channels!=64u&&
       channels!=128u&&channels!=256u)return false;
    const std::string name=std::string(layer_prefix)+".resample.bin";
    if(!ValidNeuralWeightName(name))return false;
    std::vector<std::uint8_t> matrix;
    if(!model.LoadModelMatrix(name,2u*channels,channels,matrix))
        return false;
    RequantizeE4M3(matrix);
    NeuralWeightArena prepared;
    NeuralWeightRange staged{};
    if(!prepared.AppendBytes(name,matrix,staged))return false;
    output=std::move(prepared);
    range=staged;
    return true;
}
} // namespace Eden::Graphics::NeuralRDNA2
