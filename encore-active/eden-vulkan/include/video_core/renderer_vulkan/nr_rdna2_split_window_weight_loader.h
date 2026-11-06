// SPDX-License-Identifier: MIT
// Import canonical CCSplitSwin16HQKVAttn from user NRMODEL1 only.
// QKV [1536,512], 16 heads, 16*64*64 FP16 bias, 16 FP32 temperatures.
// No model material or proprietary DLLs are included.
#pragma once
#include "video_core/renderer_vulkan/nr_rdna2_model_file.h"
#include "video_core/renderer_vulkan/nr_rdna2_weight_decode.h"
#include "video_core/renderer_vulkan/nr_rdna2_e4m3_requantize.h"
#include <algorithm>
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
struct SplitWindowModelPayload {
    std::vector<std::uint8_t> qkv_head_major;
    std::vector<float> bias_row_major;
    std::array<float,16> temperatures{};
};
// Input [Q(C rows),K(C rows),V(C rows)] x C columns.
// Output [head][Q32,K32,V32] x C columns, as qkv_projection_rdna2.comp.
[[nodiscard]] inline bool ReorderSplitWindowQkv(
    std::span<const std::uint8_t> input,
    std::vector<std::uint8_t>& output) {
    output.clear();
    constexpr std::size_t C=512u,Heads=16u,Triplet=3u*C*C;
    if(input.size()!=Triplet)return false;
    std::vector<std::uint8_t> staged(Triplet);
    for(std::size_t head=0u;head<Heads;++head)
        for(std::size_t part=0u;part<3u;++part)
            for(std::size_t channel=0u;channel<32u;++channel){
                const std::size_t src_row=part*C+head*32u+channel;
                const std::size_t dst_row=head*96u+part*32u+channel;
                std::copy_n(input.data()+src_row*C,C,
                            staged.data()+dst_row*C);
            }
    RequantizeE4M3(staged);
    for(auto b:staged)if((b&0x7fu)==0x7fu)return false;
    output=std::move(staged);
    return true;
}
[[nodiscard]] inline bool DecodeSplitWindowTemperatures(
    std::span<const std::uint8_t> bytes,
    std::array<float,16>& output) noexcept {
    output={};
    if(bytes.size()!=64u)return false;
    std::array<float,16> staged{};
    for(std::size_t i=0;i<16u;++i){
        std::uint32_t bits=0u;
        for(unsigned j=0u;j<4u;++j)
            bits|=std::uint32_t(bytes[i*4u+j])<<(8u*j);
        const auto value=std::bit_cast<float>(bits);
        if(!std::isfinite(value))return false;
        staged[i]=value;
    }
    output=staged;
    return true;
}
[[nodiscard]] inline bool PrepareSplitWindowModelWeights(
    const NeuralModelFile& model,std::string_view prefix,
    SplitWindowModelPayload& output) {
    output={};
    if(!model.IsOpen()||prefix.empty())return false;
    const std::string base(prefix);
    if(!ValidNeuralWeightName(base+".qkv.bin")||
       !ValidNeuralWeightName(base+".attn_pos_bias.bin")||
       !ValidNeuralWeightName(base+".tail.bin"))return false;
    SplitWindowModelPayload staged;
    std::vector<std::uint8_t> raw_qkv,raw_bias,raw_tail;
    std::vector<float> decoded_bias;
    if(!model.LoadModelMatrix(base+".qkv.bin",1536u,512u,raw_qkv)||
       !ReorderSplitWindowQkv(raw_qkv,staged.qkv_head_major)||
       !model.LoadRaw(base+".attn_pos_bias.bin",raw_bias)||
       raw_bias.size()!=16u*4096u*2u||
       !NeuralDecodeF16(raw_bias,decoded_bias)||
       !NeuralDeswizzleBias64(decoded_bias,16u,staged.bias_row_major)||
       !model.LoadRaw(base+".tail.bin",raw_tail)||
       !DecodeSplitWindowTemperatures(raw_tail,staged.temperatures))
        return false;
    if(staged.bias_row_major.size()!=16u*4096u)return false;
    output=std::move(staged);
    return true;
}
} // namespace Eden::Graphics::NeuralRDNA2
