// SPDX-License-Identifier: MIT
// Host reference for the exact 2D feature-raster/FSwin window layout.
// Provides a safe first-layer upload path pending complete image preprocessing.
#pragma once
#include "rdna2_nr_fswin_raster_layout_policy.h"
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <span>
#include <utility>
#include <vector>
#include <limits>

namespace Eden::Graphics::NeuralRDNA2 {
[[nodiscard]] inline bool TransformFSwinFeatureRaster(
    std::span<const std::uint8_t> source,
    const FSwinRasterLayout& layout,
    std::vector<std::uint8_t>& output) {
    constexpr auto max=std::numeric_limits<std::uint64_t>::max();
    if(!CanFSwinRasterLayout(layout,{max,max})){
        output.clear();
        return false;
    }
    const std::uint64_t raster=std::uint64_t(layout.width)*layout.height*
        layout.channels;
    const std::uint64_t padded=std::uint64_t(layout.windows_x)*
        layout.windows_y*64u*layout.channels;
    const std::uint64_t input=layout.direction==0u?raster:padded;
    const std::uint64_t result=layout.direction==0u?padded:raster;
    if(source.size()!=input||
       !CanFSwinRasterLayout(layout,{input,result})){
        output.clear();
        return false;
    }
    std::vector<std::uint8_t> prepared(
        static_cast<std::size_t>(result),std::uint8_t{0});
    for(std::uint32_t w=0u;
        w<layout.windows_x*layout.windows_y;++w){
        for(std::uint32_t t=0u;t<64u;++t){
            const std::uint32_t quadrant=t/16u,slot=t%16u;
            const std::uint32_t x=(w%layout.windows_x)*8u+
                (quadrant%2u)*4u+slot%4u;
            const std::uint32_t y=(w/layout.windows_x)*8u+
                (quadrant/2u)*4u+slot/4u;
            if(x>=layout.width||y>=layout.height)continue;
            const auto raster_offset=(std::size_t(y)*layout.width+x)*
                layout.channels;
            const auto window_offset=(std::size_t(w)*64u+t)*
                layout.channels;
            if(layout.direction==0u){
                std::copy_n(source.data()+raster_offset,layout.channels,
                            prepared.data()+window_offset);
            }else{
                std::copy_n(source.data()+window_offset,layout.channels,
                            prepared.data()+raster_offset);
            }
        }
    }
    output=std::move(prepared);
    return true;
}
[[nodiscard]] inline bool PackFSwinFeatureRaster(
    std::span<const std::uint8_t> raster,
    FSwinRasterLayout shape,
    std::vector<std::uint8_t>& windows) {
    shape.direction=0u;
    return TransformFSwinFeatureRaster(raster,shape,windows);
}
[[nodiscard]] inline bool UnpackFSwinFeatureRaster(
    std::span<const std::uint8_t> windows,
    FSwinRasterLayout shape,
    std::vector<std::uint8_t>& raster) {
    shape.direction=1u;
    return TransformFSwinFeatureRaster(windows,shape,raster);
}
} // namespace Eden::Graphics::NeuralRDNA2
