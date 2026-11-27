// SPDX-License-Identifier: MIT
// Load BOTH trained postblock gains from a local NRMODEL1 package.
// The postblock cannot be approximated with unity/zero gains.
#pragma once
#include "video_core/renderer_vulkan/nr_rdna2_model_file.h"
#include "video_core/renderer_vulkan/nr_rdna2_weight_decode.h"
#include <cstdint>
#include <string>
#include <vector>
namespace Eden::Graphics::NeuralRDNA2 {
[[nodiscard]] inline bool PreparePostblockMixGains(
    const NeuralModelFile& model,std::uint32_t block,
    std::vector<std::uint8_t>& destination) {
    destination.clear();
    if(!model.IsOpen()||(block!=70u&&block!=30u))return false;
    const auto prefix="unpacked-postblock/block"+std::to_string(block)+
                      ".layer0.layer.";
    std::vector<std::uint8_t> staged;
    staged.reserve(128u);
    for(const char* suffix:{"main_gain.bin","skip_gain.bin"}){
        const auto name=prefix+suffix;
        std::vector<std::uint8_t> bytes;
        std::vector<float> floats;
        if(!ValidNeuralWeightName(name)||
           !model.LoadRaw(name,bytes)||bytes.size()!=64u||
           !NeuralDecodeF16(bytes,floats)||floats.size()!=32u)
            return false;
        staged.insert(staged.end(),bytes.begin(),bytes.end());
    }
    if(staged.size()!=128u)return false;
    destination=std::move(staged);
    return true;
}
} // namespace Eden::Graphics::NeuralRDNA2
