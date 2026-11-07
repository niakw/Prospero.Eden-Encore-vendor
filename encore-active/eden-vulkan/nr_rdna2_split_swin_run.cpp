// SPDX-License-Identifier: MIT
#include "video_core/renderer_vulkan/nr_rdna2_split_swin_run.h"
#include "video_core/renderer_vulkan/present/util.h"
#include "video_core/vulkan_common/vulkan_device.h"
#include <array>
#include <cstdint>
#include <memory>
#include <utility>
#include <vector>
namespace Vulkan {
namespace NR=Eden::Graphics::NeuralRDNA2;
namespace {
[[nodiscard]] bool SameBuffer(
    VkDescriptorBufferInfo a,VkDescriptorBufferInfo b) noexcept {
    return a.buffer&&b.buffer&&a.buffer==b.buffer&&
           a.offset==b.offset&&a.range==b.range&&
           a.range!=0u&&a.range!=VK_WHOLE_SIZE;
}
// A direction-specific layout copy cannot read and write the same VkBuffer.
[[nodiscard]] bool ValidBoundary(
    VkDescriptorBufferInfo src,VkDescriptorBufferInfo dst,
    VkDeviceSize max_range) noexcept {
    return src.buffer&&dst.buffer&&src.buffer!=dst.buffer&&
           src.offset==0u&&dst.offset==0u&&
           src.range!=0u&&dst.range!=0u&&
           src.range!=VK_WHOLE_SIZE&&dst.range!=VK_WHOLE_SIZE&&
           src.range<=max_range&&dst.range<=max_range;
}
}
NrRdna2SplitSwinRun::NrRdna2SplitSwinRun(
    const Device& d,MemoryAllocator& a,Scheduler& s)
    :device{d},allocator{a},scheduler{s},raster_layout{d,s}{}
NrRdna2SplitSwinRun::~NrRdna2SplitSwinRun()=default;
bool NrRdna2SplitSwinRun::InitializeWithModel(
    const NR::NeuralModelFile& model,const NR::NativeSplitRoutes& routes,
    std::uint32_t run_index,
    std::span<const NrRdna2SplitSwinBlockBuffers> buffers,
    VkDescriptorBufferInfo source_raster,
    VkDescriptorBufferInfo destination_raster){
    if(ready||!model.IsOpen()||routes.run_count!=2u||
       run_index>=routes.run_count)return false;
    const auto& run=routes.runs[run_index];
    if(!run.count||run.start>=routes.count||
       run.start+run.count>routes.count||
       buffers.size()!=run.count)return false;
    const auto& first_route=routes.blocks[run.start];
    const auto& last_route=routes.blocks[run.start+run.count-1u];
    const bool has_input=first_route.input_view;
    const bool has_output=last_route.output_view;
    if(!has_input&&source_raster.buffer)return false;
    if(!has_output&&destination_raster.buffer)return false;
    NR::FSwinRasterLayout in_shape{},out_shape{};
    NR::FSwinRasterLayoutStorage in_sizes{},out_sizes{};
    if(has_input){
        in_shape={first_route.raster_width,first_route.raster_height,
                  first_route.plan.window_grid_x,
                  first_route.plan.window_grid_y,512u,0u};
        in_sizes={source_raster.range,buffers.front().input.range};
        if(!ValidBoundary(source_raster,buffers.front().input,
                          device.GetMaxStorageBufferRange())||
           !NR::CanFSwinRasterLayout(in_shape,in_sizes))
            return false;
    }
    if(has_output){
        out_shape={last_route.raster_width,last_route.raster_height,
                   last_route.plan.window_grid_x,
                   last_route.plan.window_grid_y,512u,1u};
        out_sizes={buffers.back().projected.range,
                   destination_raster.range};
        if(!ValidBoundary(buffers.back().projected,destination_raster,
                          device.GetMaxStorageBufferRange())||
           !NR::CanFSwinRasterLayout(out_shape,out_sizes))
            return false;
    }
    std::vector<std::unique_ptr<NrRdna2SplitSwinBlock>> staged;
    staged.reserve(run.count);
    for(std::uint32_t i=0u;i<run.count;++i){
        const auto& current=routes.blocks[run.start+i];
        const auto& descriptors=buffers[i];
        if(current.first_row>=512u||current.layer_count<4u||
           current.layer_count>5u||
           (current.plan.has_pooled_projection!=(current.layer_count==5u)))
            return false;
        if(i>0u){
            const auto& preceding=routes.blocks[run.start+i-1u];
            if(current.source_block!=preceding.block||
               preceding.plan.has_pooled_projection||
               !SameBuffer(descriptors.input,buffers[i-1u].projected))
                return false;
        } else if(current.source_block!=run.entry_block){
            return false;
        }
        // A pooled layer is terminal to the Split run: its FinalHead
        // output goes to ViT, never directly to the next Split block.
        if(current.plan.has_pooled_projection&&i+1u!=run.count)
            return false;
        auto block=std::make_unique<NrRdna2SplitSwinBlock>(
            device,allocator,scheduler);
        if(!block->InitializeWithModel(
              model,current.block,current.plan,descriptors))
            return false;
        staged.push_back(std::move(block));
    }
    const auto& last=routes.blocks[run.start+run.count-1u];
    VkDescriptorBufferInfo proposed=last.plan.has_pooled_projection?
        buffers.back().final_output:buffers.back().projected;
    if(!proposed.buffer||!proposed.range||
       proposed.range==VK_WHOLE_SIZE)return false;
    // Two raster layouts share a Vulkan pipeline, but require different
    // source/destination storage descriptor sets and push constants.
    auto new_pool=CreateWrappedDescriptorPool(
        device,4u,2u,{VK_DESCRIPTOR_TYPE_STORAGE_BUFFER});
    std::array<VkDescriptorSetLayout,2> layouts{
        raster_layout.DescriptorLayout(),raster_layout.DescriptorLayout()};
    auto assigned=CreateWrappedDescriptorSets(
        new_pool,vk::Span<VkDescriptorSetLayout>{
            layouts.data(),layouts.size()});
    if(assigned.IsOutOfPoolMemory()||assigned.size()!=2u)return false;
    std::array<VkDescriptorBufferInfo,4> descriptors{
        source_raster,buffers.front().input,
        buffers.back().projected,destination_raster};
    std::array<VkWriteDescriptorSet,4> writes{};
    std::uint32_t used=0u;
    const auto bind=[&](std::uint32_t set,std::uint32_t binding,
                        std::uint32_t buffer){
        writes[used++]=VkWriteDescriptorSet{
            .sType=VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
            .pNext=nullptr,.dstSet=assigned[set],
            .dstBinding=binding,.dstArrayElement=0u,
            .descriptorCount=1u,
            .descriptorType=VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
            .pImageInfo=nullptr,.pBufferInfo=&descriptors[buffer],
            .pTexelBufferView=nullptr};
    };
    if(has_input){bind(0u,0u,0u);bind(0u,1u,1u);}
    if(has_output){bind(1u,0u,2u);bind(1u,1u,3u);}
    if(used)device.GetLogical().UpdateDescriptorSets(
        vk::Span<VkWriteDescriptorSet>{writes.data(),used},{});
    raster_pool=std::move(new_pool);
    raster_sets=std::move(assigned);
    input_shape=in_shape;
    output_shape=out_shape;
    input_sizes=in_sizes;
    output_sizes=out_sizes;
    first_input=buffers.front().input;
    input_raster=has_input?source_raster:VkDescriptorBufferInfo{};
    primary_result=buffers.back().projected;
    result=has_output?destination_raster:proposed;
    input_view=has_input;
    output_view=has_output;
    blocks=std::move(staged);
    ready=true;
    return true;
}
bool NrRdna2SplitSwinRun::Record(){
    if(!ready||record_attempted||blocks.empty()||raster_sets.size()!=2u)
        return false;
    // No replay after an attempt that may have queued GPU work.
    record_attempted=true;
    if((input_view&&!NR::CanFSwinRasterLayout(
        input_shape,input_sizes))||
       (output_view&&!NR::CanFSwinRasterLayout(
        output_shape,output_sizes)))return false;
    if(input_view&&!raster_layout.Record(
           raster_sets[0],input_shape,input_sizes))return false;
    for(const auto& block:blocks)
        if(!block||!block->Record())return false;
    if(output_view&&!raster_layout.Record(
        raster_sets[1],output_shape,output_sizes))return false;
    recorded=true;
    return true;
}
} // namespace Vulkan
