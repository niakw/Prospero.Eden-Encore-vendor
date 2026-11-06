// SPDX-License-Identifier: MIT
// Connect imported FSwin weights with a validated RDNA2 graph request.
// CPU-only plan preparation; GPU allocation and descriptor writes are separate.
#pragma once
#include "rdna2_nr_fswin_graph_policy.h"
#include "video_core/renderer_vulkan/nr_rdna2_fswin_weight_loader.h"
#include <array>
#include <cstddef>
#include <cstdint>

namespace Eden::Graphics::NeuralRDNA2 {
[[nodiscard]] inline bool BindFSwinGraphWeights(
    const FSwinLayerWeightRanges& w,
    std::uint64_t arena_bytes,
    FSwinGraph& graph) noexcept {
    const std::uint32_t C=w.channels;
    if((C!=32u&&C!=64u&&C!=128u&&C!=256u)||
       w.heads!=C/32u||graph.dense[0].channels!=C||
       arena_bytes==0u||arena_bytes>(256ull<<20))return false;
    const std::array<NeuralWeightRange,9> ranges{
        w.expand,w.middle,w.contract,w.qkv,w.projection,
        w.residual_scale,w.attention_residual_scale,
        w.attention_bias,w.qk_scales};
    const std::array<std::uint64_t,9> expected{
        4ull*C*C,C==32u?0ull:std::uint64_t(C)*128u,
        C==32u?4ull*C*C:std::uint64_t(C)*C,
        3ull*C*C,std::uint64_t(C)*C,
        std::uint64_t(C)*4u,std::uint64_t(C)*4u,
        std::uint64_t(w.heads)*4096u*4u,
        std::uint64_t(w.heads)*4u};
    for(std::size_t i=0;i<ranges.size();++i){
        if(expected[i]==0u){
            if(ranges[i].length_bytes!=0u||ranges[i].offset_bytes!=0u)
                return false;
            continue;
        }
        const auto& r=ranges[i];
        if(r.length_bytes!=expected[i]||(r.offset_bytes&255u)!=0u||
           std::uint64_t(r.offset_bytes)+r.length_bytes>arena_bytes)
            return false;
        for(std::size_t j=0;j<i;++j){
            if(expected[j]==0u)continue;
            const auto& a=ranges[j];
            if(std::uint64_t(r.offset_bytes)<
                    std::uint64_t(a.offset_bytes)+a.length_bytes &&
               std::uint64_t(a.offset_bytes)<
                    std::uint64_t(r.offset_bytes)+r.length_bytes)
                return false;
        }
    }
    FSwinGraph proposed=graph;
    proposed.dense[0].w_base_bytes=w.expand.offset_bytes;
    if(C>32u)proposed.dense[1].w_base_bytes=w.middle.offset_bytes;
    proposed.dense[2].w_base_bytes=w.contract.offset_bytes;
    proposed.dense[3].w_base_bytes=w.projection.offset_bytes;
    proposed.dense[2].scale_base_floats=w.residual_scale.offset_bytes/4u;
    proposed.dense[3].scale_base_floats=
        w.attention_residual_scale.offset_bytes/4u;
    proposed.qkv.w_base_bytes=w.qkv.offset_bytes;
    proposed.qkv.scale_base_floats=w.qk_scales.offset_bytes/4u;
    proposed.attention.bias_base_floats=w.attention_bias.offset_bytes/4u;
    for(std::size_t i=0;i<4u;++i){
        if(i==1u&&C==32u)continue;
        proposed.dense_storage[i].weight_bytes=arena_bytes;
        if(i>=2u)proposed.dense_storage[i].scales_bytes=arena_bytes;
    }
    proposed.qkv_storage.weight_bytes=arena_bytes;
    proposed.qkv_storage.scale_bytes=arena_bytes;
    proposed.attention_storage.bias_bytes=arena_bytes;
    if(!CanFSwinGraph(proposed))return false;
    graph=proposed;
    return true;
}
} // namespace Eden::Graphics::NeuralRDNA2
