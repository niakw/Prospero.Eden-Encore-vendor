// SPDX-License-Identifier: MIT
#include "video_core/renderer_vulkan/nr_rdna2_vit_run.h"
#include "video_core/renderer_vulkan/present/util.h"
#include "video_core/vulkan_common/vulkan_device.h"
#include <array>
#include <cstdint>
#include <memory>
#include <utility>
namespace Vulkan {
namespace NR=Eden::Graphics::NeuralRDNA2;
namespace {
[[nodiscard]] bool ExactBuffer(
    VkDescriptorBufferInfo a,VkDescriptorBufferInfo b) noexcept{
    return a.buffer&&a.buffer==b.buffer&&a.offset==b.offset&&
           a.range>0u&&a.range==b.range&&a.range!=VK_WHOLE_SIZE;
}
[[nodiscard]] bool DifferentLiveBuffers(
    VkDescriptorBufferInfo a,VkDescriptorBufferInfo b,
    VkDeviceSize limit) noexcept{
    return a.buffer&&b.buffer&&a.buffer!=b.buffer&&
           a.offset==0u&&b.offset==0u&&a.range>0u&&b.range>0u&&
           a.range!=VK_WHOLE_SIZE&&b.range!=VK_WHOLE_SIZE&&
           a.range<=limit&&b.range<=limit;
}
}
NrRdna2VitRun::NrRdna2VitRun(
    const Device& d,MemoryAllocator& a,Scheduler& s)
    :device{d},allocator{a},scheduler{s},boundary{d,s}{}
NrRdna2VitRun::~NrRdna2VitRun()=default;
bool NrRdna2VitRun::InitializeWithModel(
    const NR::NeuralModelFile& model,const NR::NativeVitRoute& route,
    std::span<const NrRdna2VitBlockBuffers> buffers,
    VkDescriptorBufferInfo source_split_tiles,
    VkDescriptorBufferInfo destination_decoder_tiles) {
    if(ready||!model.IsOpen()||route.count!=8u||
       route.tokens==0u||route.tokens>4096u||
       !route.raster_width||!route.raster_height||
       buffers.size()!=route.count)return false;
    const bool needs_input=source_split_tiles.buffer!=VK_NULL_HANDLE;
    const bool needs_output=destination_decoder_tiles.buffer!=VK_NULL_HANDLE;
    NR::VitBoundaryRepack from_split{
        route.raster_width,route.raster_height,1024u,
        route.tokens,0u,0u,0u};
    NR::VitBoundaryRepack to_decoder{
        route.raster_width,route.raster_height,1024u,
        route.tokens,1u,0u,0u};
    NR::VitBoundaryStorage input_cap{
        source_split_tiles.range,buffers.front().input.range};
    NR::VitBoundaryStorage output_cap{
        buffers.back().output.range,destination_decoder_tiles.range};
    if((needs_input&&
        (!DifferentLiveBuffers(source_split_tiles,
             buffers.front().input,device.GetMaxStorageBufferRange())||
         !NR::CanVitBoundaryRepack(from_split,input_cap)))||
       (needs_output&&
        (!DifferentLiveBuffers(buffers.back().output,
             destination_decoder_tiles,device.GetMaxStorageBufferRange())||
         !NR::CanVitBoundaryRepack(to_decoder,output_cap))))
        return false;
    std::vector<std::unique_ptr<NrRdna2VitBlock>> prepared;
    prepared.reserve(route.count);
    for(std::uint32_t i=0u;i<route.count;++i){
        const auto& step=route.blocks[i];
        if(!step.block||step.tokens!=route.tokens||
           (i==0u?
            step.input_block!=route.input_split_block:
            (step.block!=route.blocks[i-1u].block+1u||
             step.input_block!=route.blocks[i-1u].block||
             !ExactBuffer(buffers[i].input,buffers[i-1u].output))))
            return false;
        auto block=std::make_unique<NrRdna2VitBlock>(
            device,allocator,scheduler);
        if(!block->InitializeWithModel(
             model,step.block,step.tokens,buffers[i]))
            return false;
        prepared.push_back(std::move(block));
    }
    if(!buffers.back().output.buffer||
       buffers.back().output.range==0u||
       buffers.back().output.range==VK_WHOLE_SIZE)return false;
    if(needs_input||needs_output){
        auto pool=CreateWrappedDescriptorPool(
            device,4u,2u,{VK_DESCRIPTOR_TYPE_STORAGE_BUFFER});
        std::array<VkDescriptorSetLayout,2> layouts{
            boundary.DescriptorLayout(),boundary.DescriptorLayout()};
        auto sets=CreateWrappedDescriptorSets(
            pool,vk::Span<VkDescriptorSetLayout>{
                layouts.data(),layouts.size()});
        if(sets.IsOutOfPoolMemory()||sets.size()!=2u)return false;
        const std::array<VkDescriptorBufferInfo,4> infos{
            source_split_tiles,buffers.front().input,
            buffers.back().output,destination_decoder_tiles};
        std::array<VkWriteDescriptorSet,4> writes{};
        std::uint32_t count=0u;
        const auto bind=[&](std::uint32_t index){
            writes[count++]=VkWriteDescriptorSet{
                .sType=VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,.pNext=nullptr,
                .dstSet=sets[index/2u],.dstBinding=index%2u,
                .dstArrayElement=0u,.descriptorCount=1u,
                .descriptorType=VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
                .pImageInfo=nullptr,.pBufferInfo=&infos[index],
                .pTexelBufferView=nullptr};
        };
        if(needs_input){bind(0u);bind(1u);}
        if(needs_output){bind(2u);bind(3u);}
        device.GetLogical().UpdateDescriptorSets(
            vk::Span<VkWriteDescriptorSet>{writes.data(),count},{});
        boundary_pool=std::move(pool);
        boundary_sets=std::move(sets);
    }
    incoming_shape=from_split;
    outgoing_shape=to_decoder;
    incoming_storage=input_cap;
    outgoing_storage=output_cap;
    incoming_source=source_split_tiles;
    incoming_target=buffers.front().input;
    outgoing_source=buffers.back().output;
    outgoing_target=destination_decoder_tiles;
    has_incoming=needs_input;
    has_outgoing=needs_output;
    result=needs_output?destination_decoder_tiles:buffers.back().output;
    blocks=std::move(prepared);
    ready=true;
    return true;
}
bool NrRdna2VitRun::Record(){
    if(!ready||blocks.size()!=8u)return false;
    if((has_incoming||has_outgoing)&&boundary_sets.size()!=2u)
        return false;
    if(has_incoming&&
       (!NR::CanVitBoundaryRepack(incoming_shape,incoming_storage)||
        !boundary.Record(boundary_sets[0],incoming_source,incoming_target,
                         incoming_shape)))return false;
    for(const auto& block:blocks)
        if(!block||!block->Record())return false;
    return !has_outgoing||
        (NR::CanVitBoundaryRepack(outgoing_shape,outgoing_storage)&&
         boundary.Record(boundary_sets[1],outgoing_source,outgoing_target,
                         outgoing_shape));
}
} // namespace Vulkan
