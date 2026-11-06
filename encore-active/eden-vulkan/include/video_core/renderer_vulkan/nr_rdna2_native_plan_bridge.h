// SPDX-License-Identifier: MIT
// Parse a DLSSNR-AMD native frame plan and extract safe same-shape FSwin
// sections for NrRdna2FSwinChain. Structure only: no weights or model payload.
#pragma once
#include "rdna2_nr_fswin_chain_layout_policy.h"
#include <charconv>
#include <cstdint>
#include <limits>
#include <sstream>
#include <span>
#include <string>
#include <string_view>
#include <vector>
namespace Eden::Graphics::NeuralRDNA2 {
enum class NativePlanError : std::uint8_t {
    Ok, Empty, TooManyRows, MalformedRow, InvalidShape,
    InvalidOrder, InvalidDependency
};
struct NativePlanStep {
    std::uint32_t block{},layer{},div{},width{},height{};
    std::uint64_t tokens{};
    std::uint32_t grid_x{},grid_y{},grid_z{},channels{},heads{};
    std::uint32_t input_channels{},output_channels{};
    bool shifted{};
    std::uint32_t inputs{};
    std::int32_t input0{-1},input1{-1};
    std::string type,kernel;
    std::int32_t shift_x{100},shift_y{100};
};
struct NativeFSwinLayer {
    std::string weight_prefix;
    FSwinWindowLayout layout{};
    std::uint32_t block{},layer{},channels{},windows{};
};
struct NativeFSwinSegment {
    std::vector<NativeFSwinLayer> layers;
};
[[nodiscard]] inline bool NativePlanInt(
    std::string_view word,std::int32_t& out) noexcept {
    if(word.empty())return false;
    const auto r=std::from_chars(word.data(),word.data()+word.size(),out);
    return r.ec==std::errc{}&&r.ptr==word.data()+word.size();
}
[[nodiscard]] inline bool NativePlanUint(
    std::string_view word,std::uint32_t& out) noexcept {
    if(word.empty())return false;
    const auto r=std::from_chars(word.data(),word.data()+word.size(),out);
    return r.ec==std::errc{}&&r.ptr==word.data()+word.size();
}
[[nodiscard]] inline bool NativePlanUint64(
    std::string_view word,std::uint64_t& out) noexcept {
    if(word.empty())return false;
    const auto r=std::from_chars(word.data(),word.data()+word.size(),out);
    return r.ec==std::errc{}&&r.ptr==word.data()+word.size();
}
// Upstream nr_native_plan.cpp emits 19 base fields + 2 optional tile shifts.
// Preserve the native dispatch x/y axes verbatim; do not guess image rotation.
// On any invalid line, the destination is cleared (fail closed).
[[nodiscard]] inline NativePlanError ParseNativePlan(
    std::string_view text,std::vector<NativePlanStep>& output,
    std::uint32_t* failed_line=nullptr) {
    output.clear();
    if(failed_line)*failed_line=0u;
    if(text.size()>(1u<<20))return NativePlanError::TooManyRows;
    std::istringstream stream{std::string(text)};
    std::string line;
    std::vector<NativePlanStep> result;
    std::uint32_t line_number=0u;
    auto fail=[&](NativePlanError error) {
        if(failed_line)*failed_line=line_number;
        return error;
    };
    while(std::getline(stream,line)){
        ++line_number;
        std::istringstream row{line};
        std::vector<std::string> words;
        for(std::string word;row>>word;) {
            words.push_back(word);
            if(words.size()>21u)return fail(NativePlanError::MalformedRow);
        }
        if(words.empty()||words[0].starts_with("#"))continue;
        if(result.size()>=512u)return fail(NativePlanError::TooManyRows);
        if(words.size()!=19u&&words.size()!=21u)
            return fail(NativePlanError::MalformedRow);
        NativePlanStep s;
        std::uint32_t shift=0u;
        if(!NativePlanUint(words[0],s.block)||
           !NativePlanUint(words[1],s.layer)||
           !NativePlanUint(words[2],s.div)||
           !NativePlanUint(words[3],s.width)||
           !NativePlanUint(words[4],s.height)||
           !NativePlanUint64(words[5],s.tokens)||
           !NativePlanUint(words[6],s.grid_x)||
           !NativePlanUint(words[7],s.grid_y)||
           !NativePlanUint(words[8],s.grid_z)||
           !NativePlanUint(words[9],s.channels)||
           !NativePlanUint(words[10],s.heads)||
           !NativePlanUint(words[11],s.input_channels)||
           !NativePlanUint(words[12],s.output_channels)||
           !NativePlanUint(words[13],shift)||
           !NativePlanUint(words[14],s.inputs)||
           !NativePlanInt(words[15],s.input0)||
           !NativePlanInt(words[16],s.input1))
            return fail(NativePlanError::MalformedRow);
        if(words.size()==21u&&
           (!NativePlanInt(words[19],s.shift_x)||
            !NativePlanInt(words[20],s.shift_y)))
            return fail(NativePlanError::MalformedRow);
        s.shifted=shift==1u;
        s.type=words[17];
        s.kernel=words[18];
        if(s.block>511u||s.layer>7u||s.div==0u||s.div>256u||
           s.width==0u||s.height==0u||
           s.width>16384u||s.height>16384u||
           s.tokens==0u||s.tokens>(1ull<<32u)||
           !s.grid_x||!s.grid_y||!s.grid_z||
           s.grid_x>16384u||s.grid_y>16384u||s.grid_z>256u||
           s.channels>4096u||s.heads>16u||
           s.input_channels>4096u||s.output_channels>4096u||
           shift>1u||s.inputs>2u||s.type.size()>128u||
           s.kernel.size()>256u)
            return fail(NativePlanError::InvalidShape);
        if(s.input0 < -1 || s.input1 < -1||
           s.input0>static_cast<std::int32_t>(s.block)||
           s.input1>static_cast<std::int32_t>(s.block))
            return fail(NativePlanError::InvalidDependency);
        if(s.inputs==0u&&(s.input0!=-1||s.input1!=-1))
            return fail(NativePlanError::InvalidDependency);
        if(s.inputs==1u&&s.input1!=-1)
            return fail(NativePlanError::InvalidDependency);
        if(!result.empty()){
            const auto& prev=result.back();
            if(s.block<prev.block||
               (s.block==prev.block&&s.layer<=prev.layer)||
               (s.block>prev.block&&s.layer!=0u))
                return fail(NativePlanError::InvalidOrder);
        }
        result.push_back(std::move(s));
    }
    if(result.empty())return NativePlanError::Empty;
    output=std::move(result);
    return NativePlanError::Ok;
}
[[nodiscard]] inline bool NativePlainFSwin(
    const NativePlanStep& s) noexcept {
    if(!s.type.starts_with("CCTinlayoutFusedSwin")||
       !s.kernel.starts_with("cc_tinlayout_fused_swin_")||
       !s.kernel.ends_with("_fp8"))return false;
    // These families also change layout/shape; they need their own adapters.
    for(const auto marker:{"_ds_","_upsample_","_inpview_","_outview_"})
        if(s.kernel.find(marker)!=std::string::npos)return false;
    if(s.channels!=32u&&s.channels!=64u&&
       s.channels!=128u&&s.channels!=256u)return false;
    if(s.heads!=s.channels/32u||
       s.input_channels!=s.channels||
       s.output_channels!=s.channels)return false;
    if(s.shift_x < -1||s.shift_x>1||
       s.shift_y < -1||s.shift_y>1)return false;
    const auto windows=std::uint64_t(s.grid_x)*s.grid_y;
    return windows>0u&&windows<=256u&&s.grid_z==1u;
}
// Extract ONLY contiguous compatible sections. Model-wide scheduling,
// down/up transitions, split-Swin, ViT, pre/post, and skip edges are not
// represented by a chain segment and MUST NOT be silently skipped by a
// whole-network caller.
[[nodiscard]] inline std::vector<NativeFSwinSegment>
ExtractNativeFSwinSegments(std::span<const NativePlanStep> rows){
    std::vector<NativeFSwinSegment> segments;
    NativeFSwinSegment current;
    const auto flush=[&](){
        if(!current.layers.empty()){
            segments.emplace_back(std::move(current));
            current={};
        }
    };
    for(const auto& s:rows){
        if(!NativePlainFSwin(s)){flush();continue;}
        const auto windows=s.grid_x*s.grid_y;
        if(!current.layers.empty()){
            const auto& prev=current.layers.back();
            if(prev.channels!=s.channels||
               prev.windows!=windows||
               prev.layout.windows_x!=s.grid_x||
               prev.layout.windows_y!=s.grid_y)
                flush();
        }
        NativeFSwinLayer layer;
        layer.weight_prefix="unpacked/block"+std::to_string(s.block)+
            ".layer"+std::to_string(s.layer)+".layer";
        layer.layout={s.grid_x,s.grid_y,s.shift_x,s.shift_y};
        layer.block=s.block;
        layer.layer=s.layer;
        layer.channels=s.channels;
        layer.windows=windows;
        current.layers.push_back(std::move(layer));
        if(current.layers.size()==160u)flush();
    }
    flush();
    return segments;
}
} // namespace Eden::Graphics::NeuralRDNA2
