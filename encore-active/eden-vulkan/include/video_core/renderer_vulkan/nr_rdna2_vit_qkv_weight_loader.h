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
    // Authentic Original/Compact NRMODEL1 packs preserve ViT QKV as an
    // original 128-byte header + 3 interleaved 1024x1024 matrices in
    // inventory/weights. Canonical .header/.q/.k/.v exist only in packs
    // generated with a different exporter, not in the user DLL format.
    if(!model.Find(base+".header.bin")){
        if(model.Find(base+".q.bin")||model.Find(base+".k.bin")||
           model.Find(base+".v.bin"))return false;
        constexpr std::string_view key="unpacked-vit/block";
        constexpr std::string_view suffix=".layer2.layer";
        if(!layer_prefix.starts_with(key)||
           !layer_prefix.ends_with(suffix))return false;
        const auto number=layer_prefix.substr(key.size(),
            layer_prefix.size()-key.size()-suffix.size());
        if(number.empty()||number.size()>3u)return false;
        for(char c:number)if(c<'0'||c>'9')return false;
        const std::string name="inventory/weights/block"+
            std::string(number)+".layer2.layer.bin";
        std::vector<std::uint8_t> raw;
        constexpr std::size_t kMatrix=1024u*1024u;
        if(!model.LoadRaw(name,raw)||
           raw.size()!=128u+3u*kMatrix)return false;
        ViTQkvModelPayload reconstructed{};
        if(!DecodeViTQkvTemperatures(
               {raw.data(),128u},reconstructed.temperatures))
            return false;
        reconstructed.row_major_qkv.resize(3u*kMatrix);
        for(std::uint32_t which=0;which<3u;++which)
          for(std::uint32_t row=0;row<1024u;++row)
            for(std::uint32_t k=0;k<1024u;++k){
                const std::uint32_t b=(k&1u)|
                    (((k>>1u)&1u)<<4u)|(((k>>2u)&1u)<<5u)|
                    (((k>>3u)&1u)<<1u)|(((k>>4u)&1u)<<2u)|
                    ((row&1u)<<6u)|(((row>>1u)&1u)<<7u)|
                    (((row>>2u)&1u)<<8u)|(((row>>3u)&1u)<<3u)|
                    (((row>>4u)&1u)<<9u);
                const auto tile=(row>>5u)|((k>>5u)<<5u);
                const auto src=128u+(3u*tile+which)*1024u+b;
                reconstructed.row_major_qkv[
                    (std::size_t(which)*1024u+row)*1024u+k]=raw[src];
            }
        RequantizeE4M3(reconstructed.row_major_qkv);
        for(auto b:reconstructed.row_major_qkv)
            if((b&0x7fu)==0x7fu)return false;
        output=std::move(reconstructed);
        return true;
    }
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
