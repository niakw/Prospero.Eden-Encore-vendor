// SPDX-License-Identifier: MIT
// Construct a CPU staging arena for ONE FSwin layer from a user-supplied
// NRMODEL1 pack. The source files are raw row-major E4M3FN, not GPU tiles.
// No Vulkan allocation, model weights, or built-in neural network included.
#pragma once
#include "video_core/renderer_vulkan/nr_rdna2_weight_arena.h"
#include "video_core/renderer_vulkan/nr_rdna2_fswin_qkv_layout.h"
#include "video_core/renderer_vulkan/nr_rdna2_e4m3_requantize.h"
#include "video_core/renderer_vulkan/nr_rdna2_weight_decode.h"
#include <bit>
#include <cmath>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace Eden::Graphics::NeuralRDNA2 {
struct FSwinLayerWeightRanges {
    NeuralWeightRange expand{},middle{},contract{},qkv{},projection{};
    NeuralWeightRange residual_scale{},attention_residual_scale{};
    NeuralWeightRange attention_bias{},qk_scales{};
    std::uint32_t channels{},heads{};
};
[[nodiscard]] inline bool PrepareFSwinLayerWeights(
    const NeuralModelFile& model,std::string_view prefix,
    std::uint32_t channels,bool residual_last,
    NeuralWeightArena& output,FSwinLayerWeightRanges& bindings) {
    bindings={};
    if(channels!=32u&&channels!=64u&&
       channels!=128u&&channels!=256u)return false;
    const auto C=channels,heads=C/32u;
    const std::string base(prefix);
    if(!ValidNeuralWeightName(base+".mlp_expand.bin"))return false;
    NeuralWeightArena staged;
    FSwinLayerWeightRanges layer{};
    layer.channels=C;
    layer.heads=heads;
    const auto file=[&](const char* suffix) {
        return base+suffix;
    };
    const auto matrix=[&](const char* suffix,std::uint32_t rows,
                          std::uint32_t columns,NeuralWeightRange& range){
        std::vector<std::uint8_t> values;
        if(!model.LoadModelMatrix(file(suffix),rows,columns,values))
            return false;
        RequantizeE4M3(values);
        return staged.AppendBytes(file(suffix),values,range);
    };
    if(!matrix(".mlp_expand.bin",4u*C,C,layer.expand))return false;
    if(heads>1u&&!matrix(".mlp_mid.bin",C,128u,layer.middle))return false;
    const auto contract_inputs=heads>1u?C:4u*C;
    if(!matrix(".mlp_contract.bin",C,contract_inputs,layer.contract))
        return false;
    // Model matrix order is [Q all C rows][K all C][V all C].
    // The portable shader expects [head][Q32][K32][V32] rows.
    std::vector<std::uint8_t> qkv_raw,qkv_heads;
    if(!model.LoadModelMatrix(file(".qkv.bin"),3u*C,C,qkv_raw))
        return false;
    RequantizeE4M3(qkv_raw);
    if(!ReorderFSwinQkvRowMajor(qkv_raw,C,qkv_heads)||
       !staged.AppendBytes(file(".qkv.bin"),qkv_heads,layer.qkv))return false;
    if(!matrix(".attn_out_proj.bin",C,C,layer.projection))return false;

    const auto half_param=[&](const char* suffix,std::vector<float>& values){
        std::vector<std::uint8_t> bytes;
        return model.LoadRaw(file(suffix),bytes)&&
               NeuralDecodeF16(bytes,values);
    };
    std::vector<float> raw,converted;
    if(!half_param(".residual_scale.bin",raw)||
       !NeuralResidualScale(raw,C,residual_last,converted)||
       !staged.AppendFP32(file(".residual_scale.bin"),
                          converted,layer.residual_scale))return false;
    if(!half_param(".attn_residual_scale.bin",raw)||
       raw.size()<C)return false;
    converted.assign(raw.begin(),raw.begin()+C);
    if(!staged.AppendFP32(file(".attn_residual_scale.bin"),
                          converted,layer.attention_residual_scale))return false;
    if(!half_param(".attn_pos_bias.bin",raw)||
       !NeuralDeswizzleBias64(raw,heads,converted)||
       !staged.AppendFP32(file(".attn_pos_bias.bin"),
                          converted,layer.attention_bias))return false;

    std::vector<std::uint8_t> scalars;
    if(!model.LoadRaw(file(".scalars_b.bin"),scalars)||
       scalars.size()<std::size_t(heads)*4u)return false;
    converted.resize(heads);
    for(std::uint32_t h=0u;h<heads;++h){
        std::uint32_t bits=0u;
        for(unsigned j=0u;j<4u;++j)
            bits|=std::uint32_t(scalars[4u*h+j])<<(8u*j);
        const auto value=std::bit_cast<float>(bits);
        if(!std::isfinite(value))return false;
        converted[h]=value;
    }
    if(!staged.AppendFP32(file(".scalars_b.bin"),
                          converted,layer.qk_scales))return false;
    // Commit atomically; no partially imported layer is made available.
    output=std::move(staged);
    bindings=layer;
    return true;
}
} // namespace Eden::Graphics::NeuralRDNA2
