// SPDX-License-Identifier: MIT
// R09 DLSS-NR Min: candidate pool layout only; no GPU objects or runtime enable.
// No cross-session pooling. Caller must validate Vulkan lifetimes and fences.
#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
namespace Eden::Graphics::NeuralRDNA2 {
inline constexpr std::size_t kVitMinBlocks=8u;
inline constexpr std::size_t kVitMinRoles=9u;
inline constexpr std::size_t kVitMinSlots=9u;
[[nodiscard]] constexpr bool IsSupportedVitMinScratchPlanes(
    std::uint32_t n) noexcept {
    return n==4u||n==5u||n==6u||n==12u;
}
inline constexpr std::array<std::uint32_t,kVitMinSlots> kVitMinSlotPlanes{
    1u,1u,4u,1u,1u,4u,1u,1u,1u
};
struct VitMinPoolLayout {
    bool valid{};
    std::uint32_t tokens{},scratch_planes{4u};
    std::uint64_t plane_bytes{},original_bytes{},candidate_bytes{};
    std::array<std::uint64_t,kVitMinSlots> slot_bytes{};
    std::array<std::array<std::uint8_t,kVitMinRoles>,kVitMinBlocks> block_slots{};
};
[[nodiscard]] constexpr bool ValidateVitMinPoolLayout(
    const VitMinPoolLayout& p) noexcept {
    if(!p.valid||p.tokens==0u||p.tokens>4096u||
       !IsSupportedVitMinScratchPlanes(p.scratch_planes)||
       p.plane_bytes!=std::uint64_t(p.tokens)*1024u||
       p.original_bytes!=p.plane_bytes*185u||
       p.candidate_bytes!=p.plane_bytes*(11u+p.scratch_planes)||
       p.candidate_bytes>=p.original_bytes)return false;
    std::uint64_t total{};
    for(std::size_t i=0;i<kVitMinSlots;++i){
        if(p.slot_bytes[i]!=p.plane_bytes*(i==5u?p.scratch_planes:kVitMinSlotPlanes[i]))return false;
        total+=p.slot_bytes[i];
    }
    if(total!=p.candidate_bytes)return false;
    for(std::size_t i=0;i<kVitMinBlocks;++i){
        const auto& r=p.block_slots[i];
        if(r[0]!=i%2u||r[4]!=(i+1u)%2u||
           r[1]!=2u||r[2]!=3u||r[3]!=4u||
           r[5]!=5u||r[6]!=6u||r[7]!=7u||r[8]!=8u)return false;
        for(std::size_t a=0;a<kVitMinRoles;++a){
            if(r[a]>=kVitMinSlots)return false;
            for(std::size_t b=0;b<a;++b)
                if(r[a]==r[b])return false;
        }
        if(i&&r[0]!=p.block_slots[i-1u][4])return false;
    }
    return true;
}
[[nodiscard]] constexpr VitMinPoolLayout MakeVitMinPoolLayout(
    std::uint32_t tokens,std::uint32_t scratch_planes=4u) noexcept {
    if(tokens==0u||tokens>4096u||
       !IsSupportedVitMinScratchPlanes(scratch_planes))return {};
    VitMinPoolLayout p{};
    p.tokens=tokens;
    p.scratch_planes=scratch_planes;
    p.plane_bytes=std::uint64_t(tokens)*1024u;
    p.original_bytes=p.plane_bytes*185u;
    p.candidate_bytes=p.plane_bytes*(11u+scratch_planes);
    for(std::size_t i=0;i<kVitMinSlots;++i)
        p.slot_bytes[i]=p.plane_bytes*(i==5u?scratch_planes:kVitMinSlotPlanes[i]);
    for(std::size_t i=0;i<kVitMinBlocks;++i)
        p.block_slots[i]={
            static_cast<std::uint8_t>(i%2u),2u,3u,4u,
            static_cast<std::uint8_t>((i+1u)%2u),5u,6u,7u,8u};
    p.valid=true;
    return ValidateVitMinPoolLayout(p)?p:VitMinPoolLayout{};
}
} // namespace Eden::Graphics::NeuralRDNA2
