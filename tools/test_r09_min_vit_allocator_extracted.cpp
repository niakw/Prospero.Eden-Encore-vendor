// SPDX-License-Identifier: MIT
// Compile/run the EXACT MakeVitBuffers body extracted from vendor native C++.
// Fake handle IDs and allocations; no actual VkBuffer/PS5 model execution.
#include "r09_min_vit_allocator_extracted.h"
#include <array>
#include <cassert>
#include <cstdint>
#include <initializer_list>
#include <numeric>
#include <iostream>
#include <vector>

[[nodiscard]] std::array<std::uint64_t,9u> Handles(
    const NrRdna2VitBlockBuffers& b) {
    return {b.input.buffer,b.hidden.buffer,b.ffwd.buffer,
            b.attention_context.buffer,b.output.buffer,b.scratch.buffer,
            b.query.buffer,b.key.buffer,b.value.buffer};
}
int main() {
#if defined(EDEN_R09_MIN_VIT_POOL_EXPERIMENTAL) && EDEN_R09_MIN_VIT_POOL_EXPERIMENTAL
    constexpr bool experimental=true;
#else
    constexpr bool experimental=false;
#endif
    for(const auto tokens : {960u,2560u}) {
        HostFrameState state{};
        state.vit_route={8u,tokens};
        std::vector<NrRdna2VitBlockBuffers> blocks{};
        assert(state.MakeVitBuffers(blocks));
        assert(blocks.size()==8u);
        const std::uint64_t plane=std::uint64_t(tokens)*1024u;
        const std::uint64_t expected_calls=experimental?9u:65u;
        const std::uint64_t expected_bytes=plane*(experimental?24u:185u);
        assert(state.next_id==expected_calls);
        assert(state.requested.size()==expected_calls);
        assert(std::accumulate(state.requested.begin(),state.requested.end(),
                               std::uint64_t{0u})==expected_bytes);
        for(std::size_t i=0;i<blocks.size();++i) {
            const auto& b=blocks[i];
            const auto handles=Handles(b);
            for(std::size_t role=0u;role<handles.size();++role) {
                assert(handles[role]!=0u);
                for(std::size_t prior=0u;prior<role;++prior)
                    assert(handles[role]!=handles[prior]);
            }
            if(i)assert(b.input.buffer==blocks[i-1u].output.buffer);
            if(experimental && i) {
                assert(b.hidden.buffer==blocks[0].hidden.buffer);
                assert(b.ffwd.buffer==blocks[0].ffwd.buffer);
                assert(b.scratch.buffer==blocks[0].scratch.buffer);
                assert(b.query.buffer==blocks[0].query.buffer);
            }
        }
        std::cout<<"R09_MIN_REAL_ALLOCATOR_METHOD_HOST_PASS "
                 <<(experimental?"opt-in-9":"default-65")
                 <<" tokens="<<tokens<<" buffers="<<state.next_id<<"\n";
    }
    {
        // First-block fake allocation failure: no published partial block.
        HostFrameState bad{};
        bad.vit_route={8u,2560u};
        bad.fail_on=3u;
        std::vector<NrRdna2VitBlockBuffers> blocks{};
        assert(!bad.MakeVitBuffers(blocks));
        assert(blocks.empty());
    }
    {
        HostFrameState invalid{};
        invalid.vit_route={8u,0u};
        std::vector<NrRdna2VitBlockBuffers> blocks{};
        assert(!invalid.MakeVitBuffers(blocks));
        assert(blocks.empty());
    }
}
