// SPDX-License-Identifier: MIT
// Native ViT FFN/projection canonical model loader, no bundled weights.
#pragma once
#include "video_core/renderer_vulkan/nr_rdna2_vit_ffn_weight_loader.h"
#include "rdna2_nr_vit_projection_stage_policy.h"
#include <string>
#include <utility>
namespace Eden::Graphics::NeuralRDNA2 {
struct VitProjectionLayerWeights {
    std::vector<std::uint8_t> matrix;
    std::vector<std::uint8_t> gain_f16;
};
[[nodiscard]] inline bool PrepareVitProjectionLayerWeights(
    const NeuralModelFile& model,std::uint32_t block,
    VitProjectionKind kind,VitProjectionLayerWeights& output) {
    output={};
    if(!model.IsOpen()||block>511u)return false;
    std::uint32_t N{},K{};
    if(!VitStageShape(kind,N,K))return false;
    const std::uint32_t layer=kind==VitProjectionKind::Expand?0u:
                              kind==VitProjectionKind::Contract?1u:4u;
    const std::string prefix="unpacked-vit/block"+std::to_string(block)+
                             ".layer"+std::to_string(layer)+".layer";
    const std::string weight_name=prefix+".weight.bin";
    const std::string gain_name=prefix+".skip_weight.bin";
    if(!ValidNeuralWeightName(weight_name)||
       !ValidNeuralWeightName(gain_name))return false;
    VitProjectionLayerWeights staged;
    std::vector<std::uint8_t> raw;
    if(!model.LoadModelMatrix(weight_name,N,K,raw)||
       !DecodeVitWeightMatrix(raw,N,K,staged.matrix))return false;
    if(kind!=VitProjectionKind::Expand){
        if(!model.LoadRaw(gain_name,raw)||
           !DecodeVitSkipWeights(raw,staged.gain_f16))return false;
    }
    output=std::move(staged);
    return true;
}
} // namespace Eden::Graphics::NeuralRDNA2
