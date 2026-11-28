// SPDX-License-Identifier: MIT
// Native CCDecInputUpsample learned weights, extracted into canonical
// NRMODEL1 from MIT unpack_splitswin.py. Never bundle game/model weights.
#pragma once
#include "video_core/renderer_vulkan/nr_rdna2_model_file.h"
#include "video_core/renderer_vulkan/nr_rdna2_weight_decode.h"
#include "video_core/renderer_vulkan/nr_rdna2_e4m3_requantize.h"
#include <cstdint>
#include <string>
#include <vector>
namespace Eden::Graphics::NeuralRDNA2 {
struct NativeDecoderModelWeights {
    std::vector<std::uint8_t> matrix;    // E4M3FN[512][1024]
    std::vector<std::uint8_t> skip_gain; // FP16[512]
};
[[nodiscard]] inline bool PrepareNativeDecoderModelWeights(
    const NeuralModelFile& model,std::uint32_t decoder_block,
    NativeDecoderModelWeights& output) {
    output={};
    if(!model.IsOpen()||decoder_block>511u)return false;
    const auto prefix="unpacked-splitswin/block"+
                      std::to_string(decoder_block)+".layer0.layer";
    const auto matrix_name=prefix+".weight.bin";
    const auto gain_name=prefix+".skip_weight.bin";
    if(!ValidNeuralWeightName(matrix_name)||
       !ValidNeuralWeightName(gain_name))return false;
    NativeDecoderModelWeights staged;
    if(!model.LoadModelMatrix(
          matrix_name,512u,1024u,staged.matrix)||
       staged.matrix.size()!=512u*1024u)return false;
    for(std::uint8_t b:staged.matrix)
        if((b&0x7fu)==0x7fu)return false;
    RequantizeE4M3(staged.matrix);
    std::vector<std::uint8_t> raw;
    std::vector<float> decoded;
    if(!model.LoadRaw(gain_name,raw)||raw.size()!=1024u||
       !NeuralDecodeF16(raw,decoded)||decoded.size()!=512u)return false;
    staged.skip_gain=std::move(raw);
    output=std::move(staged);
    return true;
}
} // namespace Eden::Graphics::NeuralRDNA2
