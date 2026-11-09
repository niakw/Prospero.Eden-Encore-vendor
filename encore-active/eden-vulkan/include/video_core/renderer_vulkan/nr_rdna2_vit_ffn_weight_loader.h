// SPDX-License-Identifier: MIT
// Canonical CCVit1D FP8 FFN/Projection weights from a user NRMODEL1.
// Unpacked with the MIT unpack_vit.py, no bundled proprietary learned weights.
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
struct VitFfnBlockWeights {
    std::vector<std::uint8_t> expansion;          // [4096,1024] E4M3FN
    std::vector<std::uint8_t> contraction;        // [1024,4096] E4M3FN
    std::vector<std::uint8_t> contract_gain_f16;  // [1024] FP16
    std::vector<std::uint8_t> projection;         // [1024,1024] E4M3FN
    std::vector<std::uint8_t> projection_gain_f16;// [1024] FP16
};
[[nodiscard]] inline bool DecodeVitWeightMatrix(
    std::span<const std::uint8_t> raw,
    std::uint32_t channels,std::uint32_t inner,
    std::vector<std::uint8_t>& result) {
    result.clear();
    if((channels!=4096u||inner!=1024u)&&
       (channels!=1024u||inner!=4096u)&&
       (channels!=1024u||inner!=1024u))return false;
    if(raw.size()!=std::size_t(channels)*inner)return false;
    for(const auto value:raw)
        if((value&0x7fu)==0x7fu)return false;
    std::vector<std::uint8_t> ready(raw.begin(),raw.end());
    RequantizeE4M3(ready);
    result=std::move(ready);
    return true;
}
[[nodiscard]] inline bool DecodeVitSkipWeights(
    std::span<const std::uint8_t> raw,
    std::vector<std::uint8_t>& result) {
    result.clear();
    if(raw.size()!=1024u*2u)return false;
    std::vector<float> finite;
    if(!NeuralDecodeF16(raw,finite)||finite.size()!=1024u)
        return false;
    result.assign(raw.begin(),raw.end());
    return true;
}
[[nodiscard]] inline bool PrepareVitFfnBlockWeights(
    const NeuralModelFile& model,std::uint32_t model_block,
    VitFfnBlockWeights& output) {
    output={};
    if(!model.IsOpen()||model_block>511u)return false;
    const std::string base="unpacked-vit/block"+std::to_string(model_block);
    VitFfnBlockWeights staged;
    const auto load_matrix=[&](
        std::uint32_t layer,std::uint32_t N,std::uint32_t K,
        std::vector<std::uint8_t>& dst) {
        const std::string name=base+".layer"+std::to_string(layer)+
                               ".layer.weight.bin";
        if(!ValidNeuralWeightName(name))return false;
        std::vector<std::uint8_t> raw;
        return model.LoadModelMatrix(name,N,K,raw)&&
               DecodeVitWeightMatrix(raw,N,K,dst);
    };
    const auto load_skip=[&](std::uint32_t layer,
                             std::vector<std::uint8_t>& dst) {
        const std::string name=base+".layer"+std::to_string(layer)+
                               ".layer.skip_weight.bin";
        if(!ValidNeuralWeightName(name))return false;
        std::vector<std::uint8_t> raw;
        return model.LoadRaw(name,raw)&&DecodeVitSkipWeights(raw,dst);
    };
    if(!load_matrix(0u,4096u,1024u,staged.expansion)||
       !load_matrix(1u,1024u,4096u,staged.contraction)||
       !load_skip(1u,staged.contract_gain_f16)||
       !load_matrix(4u,1024u,1024u,staged.projection)||
       !load_skip(4u,staged.projection_gain_f16))
        return false;
    output=std::move(staged);
    return true;
}
} // namespace Eden::Graphics::NeuralRDNA2
