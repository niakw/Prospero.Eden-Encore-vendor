// SPDX-License-Identifier: MIT
// Real Original/Compact CPU-only plan and pack preflight. No Vulkan objects.
#pragma once
#include "video_core/renderer_vulkan/nr_rdna2_model_file.h"
#include "video_core/renderer_vulkan/nr_rdna2_model_manifest.h"
#include "video_core/renderer_vulkan/nr_rdna2_unet_skip_map.h"
#include "rdna2_nr_split_native_routes_policy.h"
#include "rdna2_nr_vit_native_route_policy.h"
#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <utility>
#include <vector>
namespace Eden::Graphics::NeuralRDNA2 {
struct R09SessionCPUPlan {
    NeuralModelFile model{};
    std::string text{};
    NativeUNetSkipMap graph{};
    NativeSplitRoutes routes{};
    NativeVitRoute vit_route{};
    std::array<std::size_t,4u> down_segments{},up_segments{};
    std::uint32_t raster_width{},raster_height{};
};
[[nodiscard]] inline bool PreflightR09Session(
    std::string_view model_path,std::string_view native_plan,
    std::uint32_t src_width,std::uint32_t src_height,
    std::uint32_t dst_width,std::uint32_t dst_height,
    R09SessionCPUPlan& output){
    output={};
    if(model_path.empty()||native_plan.empty())return false;
    R09SessionCPUPlan checked{};
    if(!checked.model.Open(std::string(model_path)))return false;
    checked.text=std::string(native_plan);
    std::vector<NativePlanStep> rows;
    if(ParseNativePlan(checked.text,rows)!=NativePlanError::Ok||
       (rows.size()!=152u&&rows.size()!=76u))return false;
    const auto model=rows.size()==152u?
        R09ModelVariant::Original:R09ModelVariant::Compact;
    if(!ValidateR09ModelManifest(checked.model.Entries(),model))
        return false;
    // These two learned matrices are mandatory for every first-frame
    // session. Confirm their REAL FP16 byte sizes from the model's index
    // before allocating any Vulkan stage or multi-gigabyte intermediates.
    // A synthetic index-only pack containing all 599/255 names but
    // two-byte placeholders must never enter GPU initialization.
    constexpr std::string_view input_lift=
        "unpacked-preblock/block0.layer0.layer.input_lift.bin";
    const auto output_project=model==R09ModelVariant::Original?
        "unpacked-postblock/block70.layer0.layer.out_project.bin":
        "unpacked-postblock/block30.layer0.layer.out_project.bin";
    const auto* input_tensor=checked.model.Find(input_lift);
    const auto* output_tensor=checked.model.Find(output_project);
    if(!input_tensor||!output_tensor||
       input_tensor->size!=32ull*16ull*2ull||
       output_tensor->size!=16ull*32ull*2ull)
        return false;
    if(BuildNativeUNetSkipMap(rows,checked.graph)!=
           NativeUNetSkipError::Ok||
       BuildNativeSplitRoutes(rows,checked.routes)!=
           SplitNativeRoutesError::Ok||
       BuildNativeVitRoute(rows,checked.vit_route)!=
           NativeVitRouteError::Ok)return false;
    checked.raster_width=rows.front().height;
    checked.raster_height=rows.front().width;
    const auto w=checked.raster_width,h=checked.raster_height;
    if(!w||!h||w!=src_height||h!=src_width||
       w!=dst_height||h!=dst_width||
       rows.back().block!=(rows.size()==152u?70u:30u)||
       !checked.vit_route.tokens)return false;
    const std::uint64_t pixels=std::uint64_t(w)*h;
    if(pixels>16384ull*16384ull||
       pixels*64ull>(256ull<<20))return false;
    const auto segments=ExtractNativeFSwinSegments(rows);
    if(segments.size()!=8u)return false;
    std::array<bool,4u> seen_down{},seen_up{};
    for(std::size_t i=0u;i<segments.size();++i){
        const auto& segment=segments[i];
        if(segment.layers.empty())return false;
        const auto& first=segment.layers.front();
        const auto& last=segment.layers.back();
        if(last.view==NativeFSwinView::Downsample){
            if(first.channels<32u||first.channels>256u||
               (first.channels&(first.channels-1u)))return false;
            const std::size_t pos=first.channels==32u?0u:
                first.channels==64u?1u:first.channels==128u?2u:3u;
            if(seen_down[pos])return false;
            seen_down[pos]=true;
            checked.down_segments[pos]=i;
        }else if(first.view==NativeFSwinView::Upsample){
            const std::size_t pos=first.channels==256u?0u:
                first.channels==128u?1u:first.channels==64u?2u:3u;
            if(seen_up[pos]||first.channels!=(256u>>pos))return false;
            seen_up[pos]=true;
            checked.up_segments[pos]=i;
        }else return false;
    }
    for(std::size_t i=0u;i<4u;++i)
        if(!seen_down[i]||!seen_up[i])return false;
    output=std::move(checked);
    return true;
}
} // namespace Eden::Graphics::NeuralRDNA2
