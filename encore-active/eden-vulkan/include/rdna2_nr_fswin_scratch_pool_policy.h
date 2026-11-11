// SPDX-License-Identifier: MIT
// R09 research-only FSwin chain-local scratch-sharing admission policy.
// This file does NOT allocate/bind Vulkan resources or enable the feature.
// A complete learned model, validated fences and a real GPU A/B are mandatory.
// Native default remains one owner for every individual FSwin layer.
#pragma once
#include "rdna2_nr_fswin_graph_policy.h"
#include "rdna2_nr_fswin_qkv_alias_policy.h"
#include <algorithm>
#include <array>
#include <cstdint>
#include <limits>

namespace Eden::Graphics::NeuralRDNA2 {
inline constexpr std::uint32_t kFswinScratchSlots=4u;
// Array mapping: expanded (index 1), middle (2 if C>=64),
// contracted (3), attention context (7). Q/K/V already view expanded.
struct FswinSharedScratchPlan {
    std::uint32_t channels{},windows{},layers{},scratch_handles{};
    std::array<std::uint64_t,kFswinScratchSlots> capacity{};
    std::uint64_t private_input_bytes{},private_output_bytes{};
    std::uint64_t baseline_bytes{},candidate_bytes{},saved_bytes{};
    std::uint32_t baseline_handles{},candidate_handles{};
    bool valid{},runtime_qualified{false};
};
[[nodiscard]] constexpr bool ValidateFswinSharedScratchPlan(
    const FswinSharedScratchPlan& p) noexcept {
    if(!p.valid||p.layers==0u||p.layers>160u||
       (p.channels!=32u&&p.channels!=64u&&
        p.channels!=128u&&p.channels!=256u)||
       !p.windows||p.windows>65535u||
       p.scratch_handles!=(p.channels==32u?3u:4u)||
       p.private_input_bytes==0u||p.private_output_bytes==0u||
       p.runtime_qualified)return false;
    constexpr std::uint64_t per_buffer=256ull<<20u;
    std::uint64_t scratch{};
    for(std::uint32_t i=0u;i<kFswinScratchSlots;++i) {
        const auto size=p.capacity[i];
        if((size==0u)!=(i==1u&&p.channels==32u))return false;
        if(size>per_buffer||(size&3u))return false;
        scratch+=size;
    }
    const auto layer_count=std::uint64_t(p.layers);
    const auto io=p.private_input_bytes+p.private_output_bytes;
    if(io>512ull<<20u||scratch>(512ull<<20u)-io)return false;
    const auto baseline=layer_count*(io+scratch);
    const auto candidate=layer_count*io+scratch;
    if(p.baseline_bytes!=baseline||
       p.candidate_bytes!=candidate||
       p.saved_bytes!=baseline-candidate||
       p.baseline_handles!=p.layers*(p.channels==32u?5u:6u)||
       p.candidate_handles!=p.layers*2u+p.scratch_handles||
       candidate>baseline)return false;
    return true;
}
[[nodiscard]] inline FswinSharedScratchPlan MakeFswinSharedScratchPlan(
    const FSwinGraph& graph,std::uint32_t layers) noexcept {
    FswinSharedScratchPlan result{};
    if(!CanFSwinGraph(graph)||!layers||layers>160u)return result;
    const bool c32=graph.dense[0].channels==32u;
    const std::uint64_t input=std::max(
        graph.dense_storage[0].input_bytes,
        graph.dense_storage[2].residual_bytes);
    const std::uint64_t output=graph.dense_storage[3].output_bytes;
    const std::array<std::uint64_t,kFswinScratchSlots> scratch{
        graph.dense_storage[0].output_bytes,
        c32?0u:graph.dense_storage[1].output_bytes,
        std::max({graph.dense_storage[2].output_bytes,
                  graph.dense_storage[3].residual_bytes,
                  graph.qkv_storage.activation_bytes}),
        std::max(graph.attention_storage.out_bytes,
                 graph.dense_storage[3].input_bytes),
    };
    const std::array<std::uint64_t,3u> qkv{
        std::max(graph.qkv_storage.q_bytes,graph.attention_storage.q_bytes),
        std::max(graph.qkv_storage.k_bytes,graph.attention_storage.k_bytes),
        std::max(graph.qkv_storage.v_bytes,graph.attention_storage.v_bytes)};
    if(!CanFSwinQkvReuseExpanded(
           {qkv[0],qkv[1],qkv[2],scratch[0]}))return {};
    const auto scratch_sum=scratch[0]+scratch[1]+scratch[2]+scratch[3];
    result.channels=graph.dense[0].channels;
    result.windows=graph.dense[0].windows;
    result.layers=layers;
    result.scratch_handles=c32?3u:4u;
    result.capacity=scratch;
    result.private_input_bytes=input;
    result.private_output_bytes=output;
    result.baseline_bytes=std::uint64_t(layers)*(input+output+scratch_sum);
    result.candidate_bytes=std::uint64_t(layers)*(input+output)+scratch_sum;
    result.saved_bytes=result.baseline_bytes-result.candidate_bytes;
    result.baseline_handles=layers*(c32?5u:6u);
    result.candidate_handles=layers*2u+result.scratch_handles;
    result.valid=true;
    return ValidateFswinSharedScratchPlan(result)?result:
                                                 FswinSharedScratchPlan{};
}
} // namespace Eden::Graphics::NeuralRDNA2
