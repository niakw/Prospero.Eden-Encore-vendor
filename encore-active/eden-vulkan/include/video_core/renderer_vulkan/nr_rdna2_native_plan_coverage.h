// SPDX-License-Identifier: MIT
// Structural R09 model-graph inventory. No neural execution is claimed.
// Counts independent source families and genuine FSwin variant gaps.
#pragma once
#include "video_core/renderer_vulkan/nr_rdna2_native_plan_bridge.h"
#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>
namespace Eden::Graphics::NeuralRDNA2 {
enum class NativeFSwinKind : std::uint8_t {
    NotFSwin, Plain, InputView, OutputView, Downsample, Upsample, Unknown
};
[[nodiscard]] inline NativeFSwinKind ClassifyNativeFSwin(
    const NativePlanStep& s) noexcept {
    if(!s.type.starts_with("CCTinlayoutFusedSwin"))
        return NativeFSwinKind::NotFSwin;
    const std::string_view kernel=s.kernel;
    if(kernel.find("_inpview_")!=std::string_view::npos)
        return NativeFSwinKind::InputView;
    if(kernel.find("_outview_")!=std::string_view::npos)
        return NativeFSwinKind::OutputView;
    if(kernel.find("_ds_")!=std::string_view::npos)
        return NativeFSwinKind::Downsample;
    if(kernel.find("_upsample_")!=std::string_view::npos)
        return NativeFSwinKind::Upsample;
    if(kernel.starts_with("cc_tinlayout_fused_swin_") &&
       kernel.ends_with("_fp8"))
        return NativeFSwinKind::Plain;
    return NativeFSwinKind::Unknown;
}
struct NativePlanCoverage {
    std::uint32_t total{};
    std::uint32_t pre_image{},post_image{},decoder{};
    std::uint32_t vit{},split_swin{};
    std::uint32_t fswin_plain{},fswin_ready{},fswin_blocked{};
    std::uint32_t fswin_input_view{},fswin_output_view{};
    std::uint32_t fswin_input_view_ready{},fswin_output_view_ready{};
    std::uint32_t fswin_down{},fswin_up{},fswin_unknown{};
    std::uint32_t fswin_down_ready{};
    std::uint32_t other{};
    std::uint32_t compatible_segments{};
    [[nodiscard]] constexpr std::uint32_t FSwinSpecial() const noexcept {
        return fswin_input_view+fswin_output_view+
               fswin_down+fswin_up+fswin_unknown;
    }
    [[nodiscard]] constexpr std::uint32_t Classified() const noexcept {
        return pre_image+post_image+decoder+vit+split_swin+
               fswin_plain+FSwinSpecial()+other;
    }
    [[nodiscard]] constexpr std::uint32_t CanonicalFSwinReady() const noexcept {
        return fswin_ready+fswin_input_view_ready+
               fswin_output_view_ready+fswin_down_ready;
    }
    [[nodiscard]] constexpr std::uint32_t Uncovered() const noexcept {
        // Only standard FSwin inside the current safe shape/budget
        // is eligible for a chain. Other families require an actual
        // global scheduler, even if their isolated shaders exist.
        return total-CanonicalFSwinReady();
    }
};
[[nodiscard]] inline NativePlanCoverage AuditNativePlan(
    std::span<const NativePlanStep> rows){
    NativePlanCoverage c{};
    c.total=static_cast<std::uint32_t>(rows.size());
    for(const auto& row:rows){
        switch(ClassifyNativeFSwin(row)){
        case NativeFSwinKind::Plain:
            ++c.fswin_plain;
            if(NativePlainFSwin(row))++c.fswin_ready;
            else ++c.fswin_blocked;
            continue;
        case NativeFSwinKind::InputView:{
            ++c.fswin_input_view;
            NativeFSwinView view{};
            if(NativeViewFSwin(row,view)&&
               view==NativeFSwinView::InputView)
                ++c.fswin_input_view_ready;
            continue;
        }
        case NativeFSwinKind::OutputView:{
            ++c.fswin_output_view;
            NativeFSwinView view{};
            if(NativeViewFSwin(row,view)&&
               view==NativeFSwinView::OutputView)
                ++c.fswin_output_view_ready;
            continue;
        }
        case NativeFSwinKind::Downsample:
            ++c.fswin_down;
            if(NativeDownsampleFSwin(row))++c.fswin_down_ready;
            continue;
        case NativeFSwinKind::Upsample:++c.fswin_up;continue;
        case NativeFSwinKind::Unknown:++c.fswin_unknown;continue;
        case NativeFSwinKind::NotFSwin:break;
        }
        if(row.type.starts_with("CCTinlayoutFusedPreBlock"))++c.pre_image;
        else if(row.type.starts_with("CCTinlayoutFusedPostBlock"))
            ++c.post_image;
        else if(row.type.starts_with("CCSplitSwin"))++c.split_swin;
        else if(row.type.starts_with("CCVit1D"))++c.vit;
        else if(row.type.starts_with("CCDecInputUpsample"))++c.decoder;
        else ++c.other;
    }
    c.compatible_segments=static_cast<std::uint32_t>(
        ExtractNativeFSwinSegments(rows).size());
    return c;
}
} // namespace Eden::Graphics::NeuralRDNA2
