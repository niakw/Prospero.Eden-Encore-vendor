// SPDX-License-Identifier: MIT
// Construct a validated unfused FSwin layer graph (C32 has no middle pass).
#pragma once
#include "rdna2_nr_fswin_graph_policy.h"
#include <cstdint>
namespace Eden::Graphics::NeuralRDNA2 {
[[nodiscard]] inline bool MakeFSwinGraphPlan(
    std::uint32_t channels,std::uint32_t windows,
    FSwinGraph& output) noexcept {
    if((channels!=32u&&channels!=64u&&
        channels!=128u&&channels!=256u)||
        windows==0u||windows>256u)return false;
    FSwinGraph plan{};
    const std::uint64_t C=channels;
    const std::uint64_t tokens=std::uint64_t(windows)*64u;
    const std::uint32_t heads=channels/32u;
    for(std::uint32_t i=0;i<4u;++i){
        if(channels==32u&&i==1u)continue;
        plan.dense[i]={0,0,0,0,0,windows,channels,i};
        const std::uint64_t in=tokens*(
            i==1u||(i==2u&&channels==32u)?4u*C:C);
        const std::uint64_t weights=i==0u?4u*C*C:
            i==1u?C*128u:i==2u&&channels==32u?4u*C*C:C*C;
        const std::uint64_t out=tokens*(i==0u?4u*C:C);
        plan.dense_storage[i]={in,weights,tokens*C,
                               i>=2u?4u*C:0u,out};
    }
    const std::uint64_t tensor=tokens*C;
    plan.qkv={0,0,0,0,0,0,windows,channels,heads};
    plan.qkv_storage={tensor,3u*C*C,4u*heads,tensor,tensor,tensor};
    plan.attention={0,0,0,0,0,windows,heads};
    plan.attention_storage={tensor,tensor,tensor,tensor,
                            std::uint64_t(heads)*4096u*4u};
    if(!CanFSwinGraph(plan))return false;
    output=plan;
    return true;
}
} // namespace Eden::Graphics::NeuralRDNA2
