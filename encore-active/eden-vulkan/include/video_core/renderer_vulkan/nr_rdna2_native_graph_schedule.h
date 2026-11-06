// SPDX-License-Identifier: MIT
// Whole-model native operation decoder. No operation is silently skipped.
// This is the execution-order CONTRACT for the RDNA2 graph builder; it
// intentionally does not claim a shader or a runnable whole-model pipeline.
#pragma once
#include "video_core/renderer_vulkan/nr_rdna2_native_plan_bridge.h"
#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>
#include <vector>
namespace Eden::Graphics::NeuralRDNA2 {
enum class NativeNrOp : std::uint8_t {
    PreImage,
    Fswin, FswinInput, FswinOutput, FswinDown, FswinUp,
    SplitFfwd, SplitFfwdProjection, SplitQkvAttention,
    SplitProjection, SplitProjectionPool, SplitFinalHead,
    VitFfwdExpand, VitFfwdContract, VitQkv, VitAttention, VitProjection,
    DecoderUpsample, PostImage,
    Count
};
enum class NativeNrGraphError : std::uint8_t {
    Ok, Empty, Unrecognized, InvalidShape, MissingPredecessor,
    InvalidOrder, InvalidBoundary, UnsupportedLength
};
struct NativeNrInstruction {
    NativeNrOp operation{};
    std::uint32_t row{},block{},layer{};
    std::int32_t source0{-1},source1{-1};
    std::uint32_t width{},height{},channels{},in_channels{},out_channels{};
};
struct NativeNrSchedule {
    std::vector<NativeNrInstruction> instructions;
    std::array<std::uint32_t,
        static_cast<std::size_t>(NativeNrOp::Count)> counts{};
    [[nodiscard]] std::uint32_t Count(NativeNrOp op) const noexcept {
        return counts[static_cast<std::size_t>(op)];
    }
};
[[nodiscard]] constexpr bool IsNativeNrFswinOp(NativeNrOp op) noexcept {
    return op>=NativeNrOp::Fswin&&op<=NativeNrOp::FswinUp;
}
[[nodiscard]] constexpr bool IsNativeNrSplitOp(NativeNrOp op) noexcept {
    return op>=NativeNrOp::SplitFfwd&&op<=NativeNrOp::SplitFinalHead;
}
[[nodiscard]] constexpr bool IsNativeNrVitOp(NativeNrOp op) noexcept {
    return op>=NativeNrOp::VitFfwdExpand&&op<=NativeNrOp::VitProjection;
}
[[nodiscard]] inline NativeNrOp ClassifyNativeNrOp(
    const NativePlanStep& s) noexcept {
    if(s.type=="CCTinlayoutFusedPreBlockSwin1H"&&
       s.kernel=="cc_tinlayout_fused_pre_block_swin_1h_32_1_ds_fp8")
        return NativeNrOp::PreImage;
    if(s.type.starts_with("CCTinlayoutFusedSwin")&&
       s.kernel.starts_with("cc_tinlayout_fused_swin_")){
        if(s.kernel.ends_with("_inpview_fp8"))return NativeNrOp::FswinInput;
        if(s.kernel.ends_with("_outview_fp8"))return NativeNrOp::FswinOutput;
        if(s.kernel.ends_with("_ds_fp8"))return NativeNrOp::FswinDown;
        if(s.kernel.ends_with("_upsample_fp8"))return NativeNrOp::FswinUp;
        if(s.kernel.ends_with("_fp8"))return NativeNrOp::Fswin;
    }
    if(s.type=="CCSplitSwin16HFfwd"&&
       s.kernel.starts_with("cc_split_swin_16h_ffwd_"))
        return NativeNrOp::SplitFfwd;
    if(s.type=="CCSplitSwin16HFfwdProj"&&
       s.kernel.starts_with("cc_split_swin_16h_ffwd_proj_"))
        return NativeNrOp::SplitFfwdProjection;
    if(s.type=="CCSplitSwin16HQKVAttn"&&
       s.kernel.starts_with("cc_split_swin_16h_qkv_"))
        return NativeNrOp::SplitQkvAttention;
    if(s.type=="CCSplitSwin16HProj"&&
       s.kernel.starts_with("cc_split_swin_16h_proj_"))
        return NativeNrOp::SplitProjection;
    if(s.type=="CCSplitSwin16HProjPool"&&
       s.kernel=="cc_split_swin_16h_proj_pool_512_fp8")
        return NativeNrOp::SplitProjectionPool;
    if(s.type=="CCSplitSwin16HFinalHead"&&
       s.kernel=="cc_split_swin_16h_final_head_512_fp8")
        return NativeNrOp::SplitFinalHead;
    if(s.type=="CCVit1DFfnExpand"&&
       s.kernel=="cc_vit_1d_ffn_expand_fp8")return NativeNrOp::VitFfwdExpand;
    if(s.type=="CCVit1DFfnContract"&&
       s.kernel=="cc_vit_1d_ffn_contract_fp8")return NativeNrOp::VitFfwdContract;
    if(s.type=="CCVit1DQKV"&&
       s.kernel=="cc_vit_1d_qkv_fp8")return NativeNrOp::VitQkv;
    if(s.type=="CCVit1DAttention"&&
       s.kernel=="cc_vit_1d_attention_fp8")return NativeNrOp::VitAttention;
    if(s.type=="CCVit1DProjection"&&
       s.kernel=="cc_vit_1d_projection_fp8")return NativeNrOp::VitProjection;
    if(s.type=="CCDecInputUpsample"&&
       s.kernel=="cc_dec_input_upsample_1024_512_fp8")
        return NativeNrOp::DecoderUpsample;
    if(s.type=="CCTinlayoutFusedPostBlockSwin1H"&&
       s.kernel=="cc_tinlayout_fused_post_block_swin_1h_32_fp8")
        return NativeNrOp::PostImage;
    return NativeNrOp::Count;
}
[[nodiscard]] inline bool NativeNrShapeValid(
    const NativePlanStep& s,NativeNrOp op) noexcept {
    if(op==NativeNrOp::Count||s.grid_x==0u||s.grid_y==0u||
       s.grid_z==0u||s.width==0u||s.height==0u)return false;
    if(IsNativeNrFswinOp(op)){
        if(op==NativeNrOp::Fswin)return NativePlainFSwin(s);
        if(op==NativeNrOp::FswinDown)return NativeDownsampleFSwin(s);
        if(op==NativeNrOp::FswinInput||op==NativeNrOp::FswinOutput){
            NativeFSwinView view{};
            if(!NativeViewFSwin(s,view))return false;
            return view==(op==NativeNrOp::FswinInput?
                NativeFSwinView::InputView:NativeFSwinView::OutputView);
        }
        NativePlanStep normal=s;
        normal.output_channels=normal.channels;
        normal.input_channels=normal.channels;
        normal.kernel="cc_tinlayout_fused_swin_canonical_fp8";
        return s.inputs==2u&&s.input0>=0&&s.input1>=0&&
            s.output_channels==s.channels&&
            s.input_channels==2u*s.channels&&NativePlainFSwin(normal);
    }
    if(IsNativeNrSplitOp(op)){
        if(s.channels!=512u||s.width>128u||s.height>128u||
           s.heads!=4u&&s.heads!=8u)return false;
        if(op==NativeNrOp::SplitFinalHead)
            return s.input_channels==512u&&s.output_channels==1024u;
        return s.input_channels==512u&&s.output_channels==512u;
    }
    if(IsNativeNrVitOp(op)){
        if(s.channels!=1024u||s.heads!=4u||s.grid_z>4u)return false;
        if(op==NativeNrOp::VitFfwdExpand)
            return s.input_channels==1024u&&s.output_channels==4096u;
        if(op==NativeNrOp::VitFfwdContract)
            return s.input_channels==4096u&&s.output_channels==1024u;
        return s.input_channels==1024u&&s.output_channels==1024u;
    }
    if(op==NativeNrOp::PreImage)
        return s.block==0u&&s.layer==0u&&s.input_channels==3u&&
               s.output_channels==32u&&s.inputs==0u;
    if(op==NativeNrOp::DecoderUpsample)
        return s.input_channels==1024u&&s.output_channels==512u&&
               s.inputs==2u&&s.input0>=0&&s.input1>=0;
    if(op==NativeNrOp::PostImage)
        return s.input_channels==32u&&s.output_channels==3u&&
               s.inputs==2u&&s.input0>=0&&s.input1>=0;
    return false;
}
// All plan rows must have a recognized OPERATION and valid source blocks.
// Builds transactionally: on failure the output schedule is always empty.
[[nodiscard]] inline NativeNrGraphError BuildNativeNrSchedule(
    std::span<const NativePlanStep> steps,NativeNrSchedule& output,
    std::uint32_t* bad_row=nullptr){
    output={};
    if(bad_row)*bad_row=0u;
    if(steps.empty())return NativeNrGraphError::Empty;
    if(steps.size()>512u)return NativeNrGraphError::UnsupportedLength;
    NativeNrSchedule staged;
    staged.instructions.reserve(steps.size());
    std::array<bool,512> seen_block{};
    std::uint32_t previous_block=0u,previous_layer=0u;
    for(std::uint32_t i=0u;i<steps.size();++i){
        const auto& s=steps[i];
        if(bad_row)*bad_row=i;
        const auto op=ClassifyNativeNrOp(s);
        if(op==NativeNrOp::Count)return NativeNrGraphError::Unrecognized;
        if(!NativeNrShapeValid(s,op))return NativeNrGraphError::InvalidShape;
        if(i&&((s.block<previous_block)||
               (s.block==previous_block&&s.layer<=previous_layer)||
               (s.block>previous_block&&s.layer!=0u)))
            return NativeNrGraphError::InvalidOrder;
        const auto predecessor=[&](std::int32_t block) {
            return block>=0&&static_cast<std::uint32_t>(block)<seen_block.size()&&
                   seen_block[static_cast<std::uint32_t>(block)];
        };
        if((s.inputs>=1u&&!predecessor(s.input0))||
           (s.inputs==2u&&!predecessor(s.input1)))
            return NativeNrGraphError::MissingPredecessor;
        if((op==NativeNrOp::PreImage&&i!=0u)||
           (op==NativeNrOp::PostImage&&i+1u!=steps.size())||
           (op!=NativeNrOp::PreImage&&i==0u))
            return NativeNrGraphError::InvalidBoundary;
        staged.instructions.push_back({op,i,s.block,s.layer,
            s.input0,s.input1,s.width,s.height,s.channels,
            s.input_channels,s.output_channels});
        ++staged.counts[static_cast<std::size_t>(op)];
        seen_block[s.block]=true;
        previous_block=s.block;
        previous_layer=s.layer;
    }
    if(staged.Count(NativeNrOp::PreImage)!=1u||
       staged.Count(NativeNrOp::PostImage)!=1u||
       staged.Count(NativeNrOp::DecoderUpsample)!=1u)
        return NativeNrGraphError::InvalidBoundary;
    output=std::move(staged);
    if(bad_row)*bad_row=0u;
    return NativeNrGraphError::Ok;
}
} // namespace Eden::Graphics::NeuralRDNA2
