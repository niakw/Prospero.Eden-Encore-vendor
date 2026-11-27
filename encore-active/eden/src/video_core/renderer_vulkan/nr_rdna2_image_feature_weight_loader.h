// SPDX-License-Identifier: MIT
// Canonical user-supplied NRMODEL1 image-boundary matrices, no DLL/weights.
#pragma once
#include "video_core/renderer_vulkan/nr_rdna2_model_file.h"
#include "video_core/renderer_vulkan/nr_rdna2_weight_decode.h"
#include <cstdint>
#include <string>
#include <vector>
namespace Eden::Graphics::NeuralRDNA2 {
enum class ImageFeatureDirection : std::uint32_t { Lift = 0u, Project = 1u };
[[nodiscard]] inline bool PrepareImageFeatureWeights(
    const NeuralModelFile& model,ImageFeatureDirection direction,
    std::uint32_t post_block,std::vector<std::uint8_t>& output) {
    output.clear();
    if(!model.IsOpen())return false;
    std::string name;
    if(direction==ImageFeatureDirection::Lift){
        if(post_block!=0u)return false;
        name="unpacked-preblock/block0.layer0.layer.input_lift.bin";
    }else if(direction==ImageFeatureDirection::Project){
        if(post_block!=70u&&post_block!=30u)return false;
        name="unpacked-postblock/block"+std::to_string(post_block)+
             ".layer0.layer.out_project.bin";
    }else return false;
    if(!ValidNeuralWeightName(name))return false;
    std::vector<std::uint8_t> raw;
    std::vector<float> decoded;
    if(!model.LoadRaw(name,raw)||raw.size()!=1024u||
       !NeuralDecodeF16(raw,decoded)||decoded.size()!=512u)
        return false;
    output=std::move(raw);
    return true;
}
} // namespace Eden::Graphics::NeuralRDNA2
