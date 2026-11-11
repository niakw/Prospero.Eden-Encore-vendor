// SPDX-License-Identifier: MIT
// Host-only public-API comparison. Link this same fixture once with the
// pinned Eden decoders.cpp, once with the maintained R3 decoders.cpp.
// Source compilation uses no PS5 SDK, fake unused GPU/assert headers.
#include <array>
#include <cstdint>
#include <iostream>
#include <span>
#include <stdexcept>
#include <vector>
#include "video_core/textures/decoders.h"

using u8=std::uint8_t;
using u32=std::uint32_t;
using u64=std::uint64_t;

static void Check(bool ok, const char* reason) {
    if (!ok) throw std::runtime_error(reason);
}
static void Mix(u64& hash, std::span<const u8> bytes) {
    for (u8 b : bytes) {
        hash ^= static_cast<u64>(b);
        hash *= 1099511628211ULL;
    }
}
int main() {
    u64 digest = 1469598103934665603ULL;
    u32 full_cases = 0, partial_cases = 0;
    constexpr std::array<u32, 8> bpps{1,2,3,4,6,8,12,16};
    constexpr std::array<u32, 5> widths{1,3,7,16,33};
    constexpr std::array<u32, 4> heights{1,7,9,19};
    for (u32 bpp : bpps) for (u32 width : widths)
    for (u32 height : heights) for (u32 depth : {1U,2U})
    for (u32 bh : {0U,1U}) for (u32 bd : {0U,1U}) {
        const auto tiled_bytes=Tegra::Texture::CalculateSize(true,bpp,width,height,depth,bh,bd);
        const auto linear_bytes=Tegra::Texture::CalculateSize(false,bpp,width,height,depth,bh,bd);
        Check(tiled_bytes>0 && linear_bytes>0 && tiled_bytes<8'000'000,"full geometry");
        std::vector<u8> original(linear_bytes),tiled(tiled_bytes,0xA7),back(linear_bytes,0xC1);
        for(std::size_t i=0;i<original.size();++i)
            original[i]=static_cast<u8>((i*37U + width*19U + height*7U + bpp*3U)&255U);
        Tegra::Texture::SwizzleTexture(tiled,original,bpp,width,height,depth,bh,bd);
        Tegra::Texture::UnswizzleTexture(back,tiled,bpp,width,height,depth,bh,bd);
        if (bpp==16) Check(back==original,"BPP16 full roundtrip");
        Mix(digest,tiled);
        Mix(digest,back);
        ++full_cases;
    }
    for (u32 bpp : bpps) for (u32 width : {8U,17U,33U})
    for (u32 height : {7U,9U}) for (u32 bh : {0U,1U}) {
        constexpr u32 depth=1;
        const u32 ox=width/3, oy=1, ex=width-ox-1, ey=height-oy-1;
        const auto tiled_bytes=Tegra::Texture::CalculateSize(true,bpp,width,height,depth,bh,0);
        const auto linear_bytes=Tegra::Texture::CalculateSize(false,bpp,width,height,depth,bh,0);
        std::vector<u8> original(linear_bytes),tiled(tiled_bytes,0xD3),back(linear_bytes,0xE5);
        for(std::size_t i=0;i<original.size();++i)
            original[i]=static_cast<u8>((i*13U + bpp*29U + ox*11U)&255U);
        Tegra::Texture::SwizzleSubrect(tiled,original,bpp,width,height,depth,ox,oy,ex,ey,bh,0,width*bpp);
        Tegra::Texture::UnswizzleSubrect(back,tiled,bpp,width,height,depth,ox,oy,ex,ey,bh,0,width*bpp);
        Mix(digest,tiled);
        Mix(digest,back);
        ++partial_cases;
    }
    std::cout << "PASS: full=" << full_cases << " partial=" << partial_cases
              << " digest=" << digest << '\n';
}
