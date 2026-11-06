// SPDX-License-Identifier: MIT
// Read the canonical 1024x512 FinalHead matrix from a local NRMODEL1.
// The 16-byte raw tail in the upstream record is not a learned output
// gain for this no-residual GEMM; only .weight.bin is consumed here.
#pragma once
#include "video_core/renderer_vulkan/nr_rdna2_model_file.h"
#include "video_core/renderer_vulkan/nr_rdna2_e4m3_requantize.h"
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>
namespace Eden::Graphics::NeuralRDNA2 {
[[nodiscard]] inline bool DecodeSplitFinalHeadWeights(
    std::span<const std::uint8_t> raw,
    std::vector<std::uint8_t>& output) {
    output.clear();
    if(raw.size()!=1024u*512u)return false;
    for(auto v:raw)if((v&0x7fu)==0x7fu)return false;
    std::vector<std::uint8_t> staged(raw.begin(),raw.end());
    RequantizeE4M3(staged);
    output=std::move(staged);
    return true;
}
[[nodiscard]] inline bool PrepareSplitFinalHeadWeights(
    const NeuralModelFile& model,std::string_view prefix,
    std::vector<std::uint8_t>& output) {
    output.clear();
    if(!model.IsOpen()||prefix.empty())return false;
    const std::string name=std::string(prefix)+".weight.bin";
    if(!ValidNeuralWeightName(name))return false;
    std::vector<std::uint8_t> raw;
    if(!model.LoadModelMatrix(name,1024u,512u,raw))return false;
    return DecodeSplitFinalHeadWeights(raw,output);
}
} // namespace Eden::Graphics::NeuralRDNA2
