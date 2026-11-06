// SPDX-License-Identifier: MIT
// Match FSwin _ds skip producers to FSwin _upsample consumers in recovered
// 152/76-row NRMODEL1 plans. Structural edge contract, not GPU execution.
#pragma once
#include "video_core/renderer_vulkan/nr_rdna2_native_plan_bridge.h"
#include <array>
#include <cstdint>
#include <span>
namespace Eden::Graphics::NeuralRDNA2 {
struct NativeUNetSkipEdge {
    std::uint32_t down_block{};
    std::uint32_t up_block{};
    std::uint32_t channels{};
    std::uint32_t skip_width{},skip_height{};
    std::uint32_t up_input_width{},up_input_height{};
    std::uint32_t up_input_block{};
};
struct NativeUNetSkipMap {
    std::array<NativeUNetSkipEdge,4> edges{};
    std::uint32_t count{};
};
enum class NativeUNetSkipError : std::uint8_t {
    Ok, MissingProducer, BadShape, Duplicate, OutOfOrder, Incomplete
};
[[nodiscard]] inline NativeUNetSkipError BuildNativeUNetSkipMap(
    std::span<const NativePlanStep> steps,NativeUNetSkipMap& out) noexcept {
    out={};
    std::array<const NativePlanStep*,4> producers{};
    std::uint32_t producer_count=0u;
    for(const auto& step:steps){
        if(!step.kernel.ends_with("_ds_fp8")||
           !step.type.starts_with("CCTinlayoutFusedSwin"))
            continue;
        if(!NativeDownsampleFSwin(step))
            return NativeUNetSkipError::BadShape;
        if(producer_count>=producers.size())
            return NativeUNetSkipError::Duplicate;
        producers[producer_count++]=&step;
    }
    if(producer_count!=4u)return NativeUNetSkipError::Incomplete;
    NativeUNetSkipMap staged{};
    for(const auto& step:steps){
        if(!step.kernel.ends_with("_upsample_fp8")||
           !step.type.starts_with("CCTinlayoutFusedSwin"))
            continue;
        if(staged.count>=staged.edges.size())
            return NativeUNetSkipError::Duplicate;
        if(step.inputs!=2u||step.input0<0||step.input1<0||
           step.input_channels!=2u*step.channels||
           step.output_channels!=step.channels||
           step.grid_z!=1u)
            return NativeUNetSkipError::BadShape;
        const NativePlanStep* producer=nullptr;
        for(const auto* p:producers)
            if(p&&p->block==std::uint32_t(step.input1))
                producer=p;
        if(!producer)return NativeUNetSkipError::MissingProducer;
        if(step.block<=producer->block||
           step.channels!=producer->channels||
           step.width>producer->width||
           step.height>producer->height||
           (std::uint64_t(step.width)*2u<producer->width-1u)||
           (std::uint64_t(step.height)*2u<producer->height-1u)||
           std::uint64_t(step.width)*2u>producer->width+1u||
           std::uint64_t(step.height)*2u>producer->height+1u)
            return NativeUNetSkipError::BadShape;
        if(staged.count){
            const auto& previous=staged.edges[staged.count-1u];
            if(step.block<=previous.up_block||
               producer->block>=previous.down_block)
                return NativeUNetSkipError::OutOfOrder;
        }
        staged.edges[staged.count++]={
            producer->block,step.block,step.channels,
            producer->width,producer->height,
            step.width,step.height,
            static_cast<std::uint32_t>(step.input0),
        };
    }
    if(staged.count!=4u)return NativeUNetSkipError::Incomplete;
    out=staged;
    return NativeUNetSkipError::Ok;
}
} // namespace Eden::Graphics::NeuralRDNA2
