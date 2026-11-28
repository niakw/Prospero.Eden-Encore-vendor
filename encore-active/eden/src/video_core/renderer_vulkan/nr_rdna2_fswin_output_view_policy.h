// SPDX-License-Identifier: MIT
// Canonical FSwin output-view: optional tile shift reset then raster unpack.
#pragma once
#include "rdna2_nr_fswin_chain_layout_policy.h"
#include "rdna2_nr_fswin_raster_layout_policy.h"
#include "rdna2_nr_fswin_window_repack_policy.h"
#include <cstdint>
namespace Eden::Graphics::NeuralRDNA2 {
struct FSwinOutputViewPlan {
    FSwinWindowRepack repack{};
    FSwinRasterLayout raster{};
    FSwinWindowRepackStorage repack_storage{};
    FSwinRasterLayoutStorage raster_storage{};
    bool shifted{};
};
[[nodiscard]] constexpr bool MakeFSwinOutputViewPlan(
    FSwinWindowLayout layout,std::uint32_t channels,
    std::uint32_t width,std::uint32_t height,
    std::uint64_t source_bytes,std::uint64_t destination_bytes,
    FSwinOutputViewPlan& output) noexcept {
    output={};
    if(!layout.windows_x||!layout.windows_y||
       layout.shift_x<-1||layout.shift_x>1||
       layout.shift_y<-1||layout.shift_y>1)return false;
    const auto windows=std::uint64_t(layout.windows_x)*layout.windows_y;
    const auto padded=windows*64u*channels;
    const FSwinRasterLayout raster{
        width,height,layout.windows_x,layout.windows_y,channels,1u};
    if(!CanFSwinRasterLayout(raster,{padded,destination_bytes})||
       source_bytes<padded)return false;
    const FSwinWindowRepack repack{
        layout.windows_x,layout.windows_y,channels,
        layout.shift_x,layout.shift_y,0,0};
    if(!CanFSwinWindowRepack(repack,{source_bytes,padded}))return false;
    output.repack=repack;
    output.raster=raster;
    output.repack_storage={source_bytes,padded};
    output.raster_storage={padded,destination_bytes};
    output.shifted=layout.shift_x!=0||layout.shift_y!=0;
    return true;
}
} // namespace Eden::Graphics::NeuralRDNA2
