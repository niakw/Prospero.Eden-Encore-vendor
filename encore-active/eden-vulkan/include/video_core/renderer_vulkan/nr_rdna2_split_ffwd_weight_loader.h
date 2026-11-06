// SPDX-License-Identifier: MIT
// Decode native CCSplitSwin16HFfwd's THREE real weight matrices from the
// canonical pair emitted by MIT unpack_splitswin.py. No guessed transforms.
// Original bit maps: DLSSNR-AMD linux/src/core/nr_graph.cpp ff_a/q0/q2.
// Canonical matrices: unpacked-splitswin/blockN.layer0.layer.ffwd_{a,b}.bin
#pragma once
#include "video_core/renderer_vulkan/nr_rdna2_model_file.h"
#include "video_core/renderer_vulkan/nr_rdna2_e4m3_requantize.h"
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>
namespace Eden::Graphics::NeuralRDNA2 {
constexpr std::size_t kSplitFFMatrix=512u*512u;
struct SplitFFWDWeights {
    std::vector<std::uint8_t> w1; // [8][64][512], 262144 bytes
    std::vector<std::uint8_t> w2; // [8][256][64], 131072 bytes
    std::vector<std::uint8_t> w3; // [8][64][256], 131072 bytes
};
// Inverse of the exact N-fastest 16x32 B-fragment-to-canonical permutation.
// It restores a raw fragment for the original FFWD3 three-index maps.
[[nodiscard]] inline bool SplitFFCanonicalToFragment(
    std::span<const std::uint8_t> canonical,
    std::vector<std::uint8_t>& fragment) {
    fragment.clear();
    if(canonical.size()!=kSplitFFMatrix)return false;
    std::vector<std::uint8_t> recovered(kSplitFFMatrix);
    for(std::size_t kg=0u;kg<16u;++kg)
      for(std::size_t ng=0u;ng<32u;++ng)
        for(std::size_t half=0u;half<2u;++half)
          for(std::size_t lane=0u;lane<32u;++lane)
            for(std::size_t byte=0u;byte<8u;++byte){
                const auto n=ng*16u+half*8u+lane/4u;
                const auto k=kg*32u+8u*(byte/2u)+
                             2u*(lane%4u)+(byte%2u);
                const auto src=(kg*32u+ng)*512u+
                                lane*16u+half*8u+byte;
                recovered[src]=canonical[n*512u+k];
            }
    fragment=std::move(recovered);
    return true;
}
[[nodiscard]] constexpr std::size_t SplitFFSourceA(
    std::uint32_t g,std::uint32_t r,std::uint32_t k) noexcept {
    return ((k&1u)<<0u)|(((k>>1u)&1u)<<4u)|(((k>>2u)&1u)<<5u)|
      (((k>>3u)&1u)<<1u)|(((k>>4u)&1u)<<2u)|
      (((k>>5u)&1u)<<14u)|(((k>>6u)&1u)<<15u)|
      (((k>>7u)&1u)<<16u)|(((k>>8u)&1u)<<17u)|
      ((r&1u)<<3u)|(((r>>1u)&1u)<<6u)|(((r>>2u)&1u)<<7u)|
      (((r>>3u)&1u)<<8u)|(((r>>4u)&1u)<<9u)|
      (((r>>5u)&1u)<<10u)|((g&1u)<<11u)|
      (((g>>1u)&1u)<<12u)|(((g>>2u)&1u)<<13u);
}
[[nodiscard]] constexpr std::size_t SplitFFSourceQ0(
    std::uint32_t g,std::uint32_t j,std::uint32_t r) noexcept {
    return std::size_t(g)*16384u+
      (((r&1u)<<1u)|(((r>>1u)&1u)<<0u)|(((r>>2u)&1u)<<4u)|
       (((r>>3u)&1u)<<5u)|(((r>>4u)&1u)<<2u)|
       (((r>>5u)&1u)<<13u)|((j&1u)<<6u)|
       (((j>>1u)&1u)<<3u)|(((j>>2u)&1u)<<9u)|
       (((j>>3u)&1u)<<7u)|(((j>>4u)&1u)<<8u)|
       (((j>>5u)&1u)<<10u)|(((j>>6u)&1u)<<11u)|
       (((j>>7u)&1u)<<12u));
}
[[nodiscard]] constexpr std::size_t SplitFFSourceQ2(
    std::uint32_t g,std::uint32_t n,std::uint32_t j) noexcept {
    return 131072u+std::size_t(g)*16384u+
      (((j&1u)<<0u)|(((j>>1u)&1u)<<1u)|(((j>>2u)&1u)<<2u)|
       (((j>>3u)&1u)<<4u)|(((j>>4u)&1u)<<5u)|
       (((j>>5u)&1u)<<11u)|(((j>>6u)&1u)<<12u)|
       (((j>>7u)&1u)<<13u)|((n&1u)<<6u)|
       (((n>>1u)&1u)<<7u)|(((n>>2u)&1u)<<8u)|
       (((n>>3u)&1u)<<3u)|(((n>>4u)&1u)<<9u)|
       (((n>>5u)&1u)<<10u));
}
[[nodiscard]] inline bool DecodeSplitFFWDWeights(
    std::span<const std::uint8_t> canonical_a,
    std::span<const std::uint8_t> canonical_b,
    SplitFFWDWeights& output) {
    output={};
    std::vector<std::uint8_t> raw_a,raw_b;
    if(!SplitFFCanonicalToFragment(canonical_a,raw_a)||
       !SplitFFCanonicalToFragment(canonical_b,raw_b))return false;
    // Deny any invalid E4M3FN source, not just invalid final locations.
    for(const auto byte:canonical_a)if((byte&0x7fu)==0x7fu)return false;
    for(const auto byte:canonical_b)if((byte&0x7fu)==0x7fu)return false;
    SplitFFWDWeights staged;
    staged.w1.resize(kSplitFFMatrix);
    staged.w2.resize(kSplitFFMatrix/2u);
    staged.w3.resize(kSplitFFMatrix/2u);
    for(std::uint32_t g=0u;g<8u;++g){
        for(std::uint32_t r=0u;r<64u;++r)
            for(std::uint32_t k=0u;k<512u;++k)
                staged.w1[(g*64u+r)*512u+k]=
                    raw_a[SplitFFSourceA(g,r,k)];
        for(std::uint32_t j=0u;j<256u;++j)
            for(std::uint32_t r=0u;r<64u;++r)
                staged.w2[(g*256u+j)*64u+r]=
                    raw_b[SplitFFSourceQ0(g,j,r)];
        for(std::uint32_t n=0u;n<64u;++n)
            for(std::uint32_t j=0u;j<256u;++j)
                staged.w3[(g*64u+n)*256u+j]=
                    raw_b[SplitFFSourceQ2(g,n,j)];
    }
    RequantizeE4M3(staged.w1);
    RequantizeE4M3(staged.w2);
    RequantizeE4M3(staged.w3);
    output=std::move(staged);
    return true;
}
[[nodiscard]] inline bool PrepareSplitFFWDWeights(
    const NeuralModelFile& model,std::string_view prefix,
    SplitFFWDWeights& output) {
    output={};
    if(!model.IsOpen()||prefix.empty())return false;
    const std::string base(prefix);
    if(!ValidNeuralWeightName(base+".ffwd_a.bin")||
       !ValidNeuralWeightName(base+".ffwd_b.bin"))return false;
    std::vector<std::uint8_t> a,b;
    if(!model.LoadModelMatrix(base+".ffwd_a.bin",512u,512u,a)||
       !model.LoadModelMatrix(base+".ffwd_b.bin",512u,512u,b))return false;
    return DecodeSplitFFWDWeights(a,b,output);
}
} // namespace Eden::Graphics::NeuralRDNA2
