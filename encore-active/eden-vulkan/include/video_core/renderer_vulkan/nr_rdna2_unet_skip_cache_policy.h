// SPDX-License-Identifier: MIT
// Pure host-checkable shape and storage contract for four native U-Net skips.
#pragma once
#include "video_core/renderer_vulkan/nr_rdna2_unet_skip_map.h"
#include <array>
#include <cstddef>
#include <cstdint>
namespace Eden::Graphics::NeuralRDNA2 {
struct NativeUNetSkipSurface {
    std::uint32_t width{},height{};
};
[[nodiscard]] constexpr bool CanNativeUNetSkipSurfaces(
    const NativeUNetSkipMap& map,
    const std::array<NativeUNetSkipSurface,4>& geometry,
    std::uint64_t device_storage_range,
    std::array<std::uint64_t,4>& output_bytes) noexcept {
    output_bytes={};
    if(map.count!=4u||!device_storage_range)return false;
    constexpr std::uint64_t kPerBuffer=256ull<<20;
    constexpr std::uint64_t kTotal=128ull<<20;
    std::array<std::uint64_t,4> staged{};
    std::uint64_t total=0u;
    for(std::size_t i=0;i<4u;++i){
        const auto& edge=map.edges[i];
        const auto& dims=geometry[i];
        const std::uint64_t x=dims.width,y=dims.height,C=edge.channels;
        if(!x||!y||x>16384u||y>16384u||
           (C!=32u&&C!=64u&&C!=128u&&C!=256u)||
           (x+7u)/8u!=edge.skip_windows_x||
           (y+7u)/8u!=edge.skip_windows_y||
           x*y!=std::uint64_t(edge.skip_width)*edge.skip_height)
            return false;
        const std::uint64_t bytes=x*y*C;
        if(!bytes||(bytes&3u)!=0u||bytes>kPerBuffer||
           bytes>device_storage_range||bytes>kTotal||
           total>kTotal-bytes)return false;
        for(std::size_t j=0;j<i;++j)
            if(map.edges[j].down_block==edge.down_block||
               map.edges[j].up_block==edge.up_block)
                return false;
        total+=bytes;
        staged[i]=bytes;
    }
    output_bytes=staged;
    return true;
}
} // namespace Eden::Graphics::NeuralRDNA2
