// SPDX-License-Identifier: MIT
#include "rdna2_nr_vit_min_pool_policy.h"
#include <cassert>
#include <cstdint>
#include <initializer_list>
using namespace Eden::Graphics::NeuralRDNA2;
int main() {
    constexpr std::uint64_t MiB=1024ull*1024ull;
    constexpr auto original=MakeVitMinPoolLayout(2560u);
    constexpr auto reduced=MakeVitMinPoolLayout(960u);
    static_assert(ValidateVitMinPoolLayout(original));
    static_assert(ValidateVitMinPoolLayout(reduced));
    static_assert(original.original_bytes==4625ull*MiB/10ull);
    static_assert(original.candidate_bytes==375ull*MiB/10ull);
    static_assert(original.original_bytes-original.candidate_bytes==
                  425ull*MiB);
    static_assert(reduced.original_bytes==1734375ull*MiB/10000ull);
    static_assert(reduced.candidate_bytes==140625ull*MiB/10000ull);
    static_assert(!MakeVitMinPoolLayout(0u).valid);
    static_assert(!MakeVitMinPoolLayout(4097u).valid);
    static_assert(!MakeVitMinPoolLayout(2560u,0u).valid);
    static_assert(!MakeVitMinPoolLayout(2560u,3u).valid);
    static_assert(!MakeVitMinPoolLayout(2560u,7u).valid);
    static_assert(!MakeVitMinPoolLayout(2560u,8u).valid);
    static_assert(!MakeVitMinPoolLayout(2560u,16u).valid);
    static_assert(MakeVitMinPoolLayout(2560u,5u).candidate_bytes==
                  40ull*MiB);
    static_assert(MakeVitMinPoolLayout(2560u,6u).candidate_bytes==
                  425ull*MiB/10ull);
    static_assert(MakeVitMinPoolLayout(2560u,12u).candidate_bytes==
                  575ull*MiB/10ull);
    for(const auto tokens : {1u,64u,960u,2560u,4096u}) {
        auto p=MakeVitMinPoolLayout(tokens);
        assert(ValidateVitMinPoolLayout(p));
        assert(p.candidate_bytes<p.original_bytes);
        for(std::uint32_t scratch : {4u,5u,6u,12u}) {
            const auto profile=MakeVitMinPoolLayout(tokens,scratch);
            assert(ValidateVitMinPoolLayout(profile));
            assert(profile.scratch_planes==scratch);
            assert(profile.slot_bytes[5u]==
                   profile.plane_bytes*scratch);
            assert(profile.candidate_bytes==
                   profile.plane_bytes*(11u+scratch));
            auto malformed=profile;
            malformed.scratch_planes=7u;
            assert(!ValidateVitMinPoolLayout(malformed));
            malformed=profile;
            malformed.slot_bytes[5u]-=1u;
            assert(!ValidateVitMinPoolLayout(malformed));
        }
        for(std::size_t i=0;i<kVitMinBlocks;++i) {
            assert(p.block_slots[i][0]!=p.block_slots[i][4]);
            if(i)assert(p.block_slots[i][0]==p.block_slots[i-1u][4]);
        }
        auto wrong=p;
        wrong.block_slots[3u][7u]=wrong.block_slots[3u][6u];
        assert(!ValidateVitMinPoolLayout(wrong));
        wrong=p;
        wrong.block_slots[4u][0u]=99u;
        assert(!ValidateVitMinPoolLayout(wrong));
        wrong=p;
        wrong.block_slots[4u][0u]=wrong.block_slots[4u][4u];
        assert(!ValidateVitMinPoolLayout(wrong));
        wrong=p;
        wrong.candidate_bytes+=1u;
        assert(!ValidateVitMinPoolLayout(wrong));
        wrong=p;
        wrong.slot_bytes[5u]=0u;
        assert(!ValidateVitMinPoolLayout(wrong));
    }
}
