// SPDX-License-Identifier: MIT
// Stage the native MIT extractor's canonical ViT QKV matrices and FP32
// head temperatures from a user-supplied NRMODEL1 (no embedded weights).
#pragma once
#include "video_core/renderer_vulkan/nr_rdna2_model_file.h"
#include "video_core/renderer_vulkan/nr_rdna2_e4m3_requantize.h"
#include <array>
#include <bit>
#include <cmath>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>
namespace Eden::Graphics::NeuralRDNA2 {
struct ViTQkvModelPayload {
    // GEMM expects [3072,1024] row-major Q[1024],K[1024],V[1024].
    std::vector<std::uint8_t> row_major_qkv;
    std::array<float,32> temperatures{};
};
[[nodiscard]] inline bool DecodeViTQkvTemperatures(
    std::span<const std::uint8_t> raw,
    std::array<float,32>& output) noexcept {
    output={};
    if(raw.size()!=128u)return false;
    std::array<float,32> staged{};
    for(std::size_t h=0u;h<staged.size();++h){
        std::uint32_t word=0u;
        for(unsigned byte=0u;byte<4u;++byte)
            word|=std::uint32_t(raw[4u*h+byte])<<(8u*byte);
        const float temperature=std::bit_cast<float>(word);
        if(!std::isfinite(temperature))return false;
        staged[h]=temperature;
    }
    output=staged;
    return true;
}
[[nodiscard]] inline bool PrepareViTQkvModelWeights(
    const NeuralModelFile& model,std::string_view layer_prefix,
    ViTQkvModelPayload& output) {
    output={};
    if(!model.IsOpen()||layer_prefix.empty())return false;
    const std::string base(layer_prefix);
    if(!ValidNeuralWeightName(base+".header.bin")||
       !ValidNeuralWeightName(base+".q.bin")||
       !ValidNeuralWeightName(base+".k.bin")||
       !ValidNeuralWeightName(base+".v.bin"))return false;
    ViTQkvModelPayload staged;
    std::vector<std::uint8_t> raw_header;
    if(!model.LoadRaw(base+".header.bin",raw_header)||
       !DecodeViTQkvTemperatures(raw_header,staged.temperatures))
        return false;
    constexpr std::size_t kMatrix=1024u*1024u;
    staged.row_major_qkv.reserve(3u*kMatrix);
    for(const char* suffix:{".q.bin",".k.bin",".v.bin"}){
        std::vector<std::uint8_t> matrix;
        if(!model.LoadModelMatrix(base+suffix,1024u,1024u,matrix)||
           matrix.size()!=kMatrix)return false;
        RequantizeE4M3(matrix);
        // NaN E4M3 payloads are not valid trained weight values.
        for(auto b:matrix)if((b&0x7fu)==0x7fu)return false;
        staged.row_major_qkv.insert(staged.row_major_qkv.end(),
                                    matrix.begin(),matrix.end());
    }
    if(staged.row_major_qkv.size()!=3u*kMatrix)return false;
    output=std::move(staged);
    return true;
}
} // namespace Eden::Graphics::NeuralRDNA2
