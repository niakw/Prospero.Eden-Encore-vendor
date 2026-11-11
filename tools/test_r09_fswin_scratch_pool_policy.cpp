// SPDX-License-Identifier: MIT
// Executable PURE contract for the opt-in (unwired) native FSwin pool.
// Compiled by two host compilers; no PS5 SDK, Vulkan driver or model weights.
#include "rdna2_nr_fswin_scratch_pool_policy.h"
#include <array>
#include <cassert>
#include <cstdint>
#include <iostream>
using namespace Eden::Graphics::NeuralRDNA2;
constexpr auto c32=MakeFswinSharedScratchPlan(32u,3600u,4u);
constexpr auto c64=MakeFswinSharedScratchPlan(64u,920u,4u);
constexpr auto c128=MakeFswinSharedScratchPlan(128u,240u,6u);
constexpr auto c256=MakeFswinSharedScratchPlan(256u,60u,8u);
static_assert(c32.valid&&c64.valid&&c128.valid&&c256.valid);
static_assert(c32.capacity[1]==0u);
static_assert(c32.scratch_handles==3u);
static_assert(c64.scratch_handles==4u);
static_assert(c256.candidate_bytes<c256.baseline_bytes);
static_assert(!c32.runtime_qualified);
static_assert(!MakeFswinSharedScratchPlan(0u,3600u,4u).valid);
static_assert(!MakeFswinSharedScratchPlan(32u,0u,4u).valid);
static_assert(!MakeFswinSharedScratchPlan(32u,3600u,0u).valid);
static_assert(!MakeFswinSharedScratchPlan(32u,3600u,161u).valid);
static_assert(!MakeFswinSharedScratchPlan(32u,65535u,4u).valid);
int main(){
    const std::array<std::array<std::uint32_t,4>,2> counts{{
        {{4u,4u,6u,8u}}, // Original: one down and one up segment of each
        {{2u,2u,2u,2u}}, // Compact: one down and one up segment of each
    }};
    const std::array<std::uint32_t,4> channels{{32u,64u,128u,256u}};
    const std::array<std::uint32_t,4> windows{{3600u,920u,240u,60u}};
    for(unsigned model=0u;model<2u;++model){
        std::uint64_t before{},after{},saved{};
        std::uint32_t original_handles{},new_handles{},layers{};
        for(unsigned direction=0u;direction<2u;++direction){
            for(unsigned level=0u;level<4u;++level){
                const auto p=MakeFswinSharedScratchPlan(
                    channels[level],windows[level],counts[model][level]);
                assert(ValidateFswinSharedScratchPlan(p));
                assert(p.baseline_handles>=p.candidate_handles);
                assert(p.baseline_bytes==p.candidate_bytes+p.saved_bytes);
                assert(p.scratch_handles==(level==0u?3u:4u));
                assert(!p.runtime_qualified);
                assert(p.private_input_bytes==p.private_output_bytes);
                assert(p.capacity[0]==4u*p.private_input_bytes);
                assert(p.capacity[2]==p.private_input_bytes);
                before+=p.baseline_bytes;
                after+=p.candidate_bytes;
                saved+=p.saved_bytes;
                original_handles+=p.baseline_handles;
                new_handles+=p.candidate_handles;
                layers+=p.layers;
            }
        }
        assert(layers==(model==0u?44u:16u));
        assert(original_handles==(model==0u?256u:92u));
        assert(new_handles==(model==0u?118u:62u));
        assert(before==(model==0u?1097072640ull:477757440ull));
        assert(after==(model==0u?439418880ull:295239680ull));
        assert(saved==(model==0u?657653760ull:182517760ull));
        std::cout<<"R09_FSWIN_CHAIN_POOL_POLICY_PASS "
                 <<(model==0u?"Original":"Compact")
                 <<" layers="<<layers
                 <<" buffers="<<original_handles<<"->"<<new_handles
                 <<" nominal_bytes="<<before<<"->"<<after
                 <<" savings_bytes="<<saved
                 <<" runtime=DISABLED\n";
    }
    auto mutation=c32;
    mutation.capacity[0]-=4u;
    assert(!ValidateFswinSharedScratchPlan(mutation));
    mutation=c64;
    mutation.capacity[1]=0u;
    assert(!ValidateFswinSharedScratchPlan(mutation));
    mutation=c32;
    mutation.candidate_handles--;
    assert(!ValidateFswinSharedScratchPlan(mutation));
    mutation=c32;
    mutation.runtime_qualified=true;
    assert(!ValidateFswinSharedScratchPlan(mutation));
}
