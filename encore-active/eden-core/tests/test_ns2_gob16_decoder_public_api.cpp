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
    // Maxwell pitch-to-tiled fallback uses SwizzleSubrect for 3D GOB
    // volumes, with line_count spanning complete Z slices. The rounded
    // depth is part of the destination size even for one logical slice.
    for (u32 bpp : {1U, 4U, 16U}) for (u32 depth : {2U, 3U})
    for (u32 block_depth : {0U, 1U, 2U}) {
        constexpr u32 width=17, height=9, block_height=1;
        const u32 pitch=width*bpp;
        const u64 padded_width=(static_cast<u64>(pitch)+63)&~u64{63};
        const u64 padded_height=(static_cast<u64>(height)+15)&~u64{15};
        const u64 block_z=u64{1}<<block_depth;
        const u64 padded_depth=(static_cast<u64>(depth)+block_z-1)&~(block_z-1);
        const auto tiled_bytes=Tegra::Texture::CalculateSize(
            true,bpp,width,height,depth,block_height,block_depth);
        Check(tiled_bytes==padded_width*padded_height*padded_depth,
              "3D DMA padded GOB envelope");
        std::vector<u8> linear(static_cast<size_t>(pitch)*height*depth);
        for (size_t i=0;i<linear.size();++i)
            linear[i]=static_cast<u8>((i*23U+bpp*7U+block_depth*11U)&255U);
        std::vector<u8> tiled(tiled_bytes,0xAA),readback(linear.size(),0);
        Tegra::Texture::SwizzleSubrect(
            tiled,linear,bpp,width,height,depth,0,0,width,height*depth,
            block_height,block_depth,pitch);
        Tegra::Texture::UnswizzleSubrect(
            readback,tiled,bpp,width,height,depth,0,0,width,height*depth,
            block_height,block_depth,pitch);
        Check(readback==linear,"3D pitch-to-block-linear DMA subrect roundtrip");
        Mix(digest,tiled);
        Mix(digest,readback);
        ++partial_cases;
    }
#ifdef EDEN_TEST_NATIVE_TILING
    // The same decoder entrypoints are used by MaxwellDMA. Cover a
    // multi-slice 16-byte GOB upload/readback on the NS2 channel, not just
    // the single-layer I2M fixture below.
    for (u32 depth : {2U, 3U}) for (u32 bd : {0U, 1U}) {
        constexpr u32 bpp=16, width=17, height=9, bh=1, pitch=width*bpp;
        const size_t linear_size=static_cast<size_t>(pitch)*height*depth;
        const size_t tiled_size=Tegra::Texture::CalculateSize(true,bpp,width,height,depth,bh,bd);
        std::vector<u8> original(linear_size),native(tiled_size,0xC3),back(linear_size,0x00);
        for (size_t i=0;i<linear_size;++i)
            original[i]=static_cast<u8>((i*29U + depth*7U + bd*13U)&255U);
        Tegra::Texture::SwizzleSubrect(native,original,bpp,width,height,depth,
                                      0,0,width,height*depth,bh,bd,pitch,true);
        Tegra::Texture::UnswizzleSubrect(back,native,bpp,width,height,depth,
                                        0,0,width,height*depth,bh,bd,pitch,true);
        Check(back==original,"NS2 native 3D DMA roundtrip");
        Mix(digest,native);
        ++partial_cases;
    }
    // NV50 DST_LAYER semantics: a depth-tiled 3D destination selects
    // one Z slice without changing the base address. Exercise the maintained
    // overload under both Maxwell/NS1 and Switch 2 native GOB ordering.
    for (bool native : {false, true}) for (u32 bpp : {1U,4U,16U})
    for (u32 bd : {1U,2U}) for (u32 layer : {1U,2U,3U}) {
        constexpr u32 width=11, height=9, depth=4, bh=1;
        const u32 pitch=width*bpp;
        const size_t linear_size=static_cast<size_t>(pitch)*height;
        const size_t tiled_size=Tegra::Texture::CalculateSize(
            true,bpp,width,height,depth,bh,bd);
        std::vector<u8> original(linear_size),tiled(tiled_size,0x5A);
        std::vector<u8> back(linear_size,0),base(linear_size,0);
        for (size_t i=0;i<linear_size;++i)
            original[i]=static_cast<u8>((i*41U+bpp*17U+bd*7U+layer*13U)&255U);
        Tegra::Texture::SwizzleSubrect(
            tiled,original,bpp,width,height,depth,0,0,width,height,
            bh,bd,pitch,native,layer);
        Tegra::Texture::UnswizzleSubrect(
            back,tiled,bpp,width,height,depth,0,0,width,height,
            bh,bd,pitch,native,layer);
        Tegra::Texture::UnswizzleSubrect(
            base,tiled,bpp,width,height,depth,0,0,width,height,
            bh,bd,pitch,native,0);
        Check(back==original,"DST_LAYER subrect roundtrip");
        Check(base!=original,"DST_LAYER must not overwrite z0");
        Mix(digest,tiled);
        ++partial_cases;
    }
    // Lichii #2085 regression: a 2x2 BPP16 I2M upload differs at GOB
    // offset bits 5/6 on Switch 2. Maxwell-written bytes must NOT decode
    // as the original under native tiling; native write/read must roundtrip.
    {
        constexpr u32 bpp=16, width=2, height=2, depth=1, bh=0, bd=0;
        const auto tiled_bytes=Tegra::Texture::CalculateSize(true,bpp,width,height,depth,bh,bd);
        const auto linear_bytes=Tegra::Texture::CalculateSize(false,bpp,width,height,depth,bh,bd);
        std::vector<u8> original(linear_bytes),maxwell(tiled_bytes,0),native(tiled_bytes,0);
        std::vector<u8> wrong(linear_bytes,0),back(linear_bytes,0);
        for(std::size_t i=0;i<original.size();++i)
            original[i]=static_cast<u8>((i*17U+3U)&255U);
        Tegra::Texture::SwizzleSubrect(maxwell,original,bpp,width,height,depth,
                                      0,0,width,height,bh,bd,width*bpp,false);
        Tegra::Texture::UnswizzleSubrect(wrong,maxwell,bpp,width,height,depth,
                                        0,0,width,height,bh,bd,width*bpp,true);
        Check(wrong!=original,"Maxwell/native mismatch witness");
        Tegra::Texture::SwizzleSubrect(native,original,bpp,width,height,depth,
                                      0,0,width,height,bh,bd,width*bpp,true);
        Tegra::Texture::UnswizzleSubrect(back,native,bpp,width,height,depth,
                                        0,0,width,height,bh,bd,width*bpp,true);
        Check(back==original,"native I2M subrect roundtrip");
        std::fill(native.begin(),native.end(),0);
        std::fill(back.begin(),back.end(),0);
        Tegra::Texture::SwizzleTexture(native,original,bpp,width,height,depth,bh,bd,1,true);
        Tegra::Texture::UnswizzleTexture(back,native,bpp,width,height,depth,bh,bd,1,true);
        Check(back==original,"native full texture roundtrip");
        Mix(digest,native);
    }
#endif
    std::cout << "PASS: full=" << full_cases << " partial=" << partial_cases
              << " digest=" << digest
#ifdef EDEN_TEST_NATIVE_TILING
              << " native_tiling=1 mismatch_witness=1"
#endif
              << '\n';
}
