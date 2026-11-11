// SPDX-License-Identifier: MIT
// Strict HOST-side size contract for an isolated R09 FSwin scratch pool.
// Self-contained: vendor graph policies are injected by Encore at compile.
// Only share expanded, middle, contracted and context between sequential
// layers in ONE chain. Per-layer Upload input and DeviceLocal output remain
// private. No Vulkan memory is allocated by this header. Runtime OFF.
#pragma once
#include <array>
#include <cstdint>

namespace Eden::Graphics::NeuralRDNA2 {
inline constexpr std::uint32_t kFswinScratchSlots=4u;
// Slot 0=expanded, 1=middle (absent C32), 2=contracted, 3=context.
// Existing packed Q/K/V are disjoint slices inside slot 0 (expanded).
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
    constexpr std::uint64_t per_layer=512ull<<20u;
    const auto tokens=std::uint64_t(p.windows)*64u;
    const auto one=tokens*p.channels;
    if(p.private_input_bytes!=one||p.private_output_bytes!=one||
       p.capacity[0]!=4u*one||
       p.capacity[1]!=(p.channels==32u?0u:one)||
       p.capacity[2]!=one||p.capacity[3]!=one)return false;
    std::uint64_t scratch{};
    for(std::uint32_t i=0u;i<kFswinScratchSlots;++i) {
        const auto size=p.capacity[i];
        if((size==0u)!=(i==1u&&p.channels==32u))return false;
        if(size>per_buffer||(size&3u))return false;
        scratch+=size;
    }
    const auto layer_count=std::uint64_t(p.layers);
    const auto io=p.private_input_bytes+p.private_output_bytes;
    if(io>per_layer||scratch>per_layer-io)return false;
    const auto baseline=layer_count*(io+scratch);
    const auto candidate=layer_count*io+scratch;
    return p.baseline_bytes==baseline&&
           p.candidate_bytes==candidate&&
           p.saved_bytes==baseline-candidate&&
           p.baseline_handles==p.layers*(p.channels==32u?5u:6u)&&
           p.candidate_handles==p.layers*2u+p.scratch_handles&&
           candidate<=baseline;
}
[[nodiscard]] constexpr FswinSharedScratchPlan MakeFswinSharedScratchPlan(
    std::uint32_t channels,std::uint32_t windows,
    std::uint32_t layers) noexcept {
    if(!layers||layers>160u||!windows||windows>65535u||
       (channels!=32u&&channels!=64u&&
        channels!=128u&&channels!=256u))return {};
    const auto one=std::uint64_t(windows)*64u*channels;
    FswinSharedScratchPlan p{};
    p.channels=channels;
    p.windows=windows;
    p.layers=layers;
    p.scratch_handles=channels==32u?3u:4u;
    p.capacity={4u*one,channels==32u?0u:one,one,one};
    p.private_input_bytes=one;
    p.private_output_bytes=one;
    const auto scratch=p.capacity[0]+p.capacity[1]+
                       p.capacity[2]+p.capacity[3];
    p.baseline_bytes=std::uint64_t(layers)*(2u*one+scratch);
    p.candidate_bytes=std::uint64_t(layers)*2u*one+scratch;
    p.saved_bytes=p.baseline_bytes-p.candidate_bytes;
    p.baseline_handles=layers*(channels==32u?5u:6u);
    p.candidate_handles=layers*2u+p.scratch_handles;
    p.valid=true;
    return ValidateFswinSharedScratchPlan(p)?p:
                                                 FswinSharedScratchPlan{};
}
} // namespace Eden::Graphics::NeuralRDNA2
