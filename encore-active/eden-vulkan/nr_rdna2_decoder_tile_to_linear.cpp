// SPDX-License-Identifier: MIT
#include "video_core/renderer_vulkan/nr_rdna2_decoder_tile_to_linear.h"
#include "video_core/renderer_vulkan/present/util.h"
#include "video_core/renderer_vulkan/vk_scheduler.h"
#include "video_core/vulkan_common/vulkan_device.h"
#include "video_core/host_shaders/rdna2_nr_decoder_tile_linear_comp_spv.h"
#include <array>
#include <cstdint>
#include <utility>
namespace Vulkan {
namespace NR=Eden::Graphics::NeuralRDNA2;
namespace {
static_assert(sizeof(NR::DecoderTileToLinear)==2u*sizeof(u32));
constexpr VkMemoryBarrier kVisible{
    .sType=VK_STRUCTURE_TYPE_MEMORY_BARRIER,.pNext=nullptr,
    .srcAccessMask=VK_ACCESS_SHADER_WRITE_BIT,
    .dstAccessMask=VK_ACCESS_SHADER_READ_BIT|VK_ACCESS_SHADER_WRITE_BIT};
}
NrRdna2DecoderTileToLinear::NrRdna2DecoderTileToLinear(
    const Device& d,Scheduler& s):device{d},scheduler{s}{
    descriptor_layout=CreateWrappedDescriptorSetLayout(
        d,{VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
           VK_DESCRIPTOR_TYPE_STORAGE_BUFFER},
        VK_SHADER_STAGE_COMPUTE_BIT);
    constexpr VkPushConstantRange push{
        .stageFlags=VK_SHADER_STAGE_COMPUTE_BIT,
        .offset=0,.size=sizeof(NR::DecoderTileToLinear)};
    layout=d.GetLogical().CreatePipelineLayout({
        .sType=VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
        .pNext=nullptr,.flags=0,.setLayoutCount=1,
        .pSetLayouts=descriptor_layout.address(),
        .pushConstantRangeCount=1,.pPushConstantRanges=&push});
    shader=CreateWrappedShaderModule(
        d,RDNA2_NR_DECODER_TILE_LINEAR_COMP_SPV);
    pipeline=CreateWrappedComputePipeline(d,layout,*shader);
}
NrRdna2DecoderTileToLinear::~NrRdna2DecoderTileToLinear()=default;
bool NrRdna2DecoderTileToLinear::Initialize(
    const NR::DecoderTileToLinear& requested,
    VkDescriptorBufferInfo input,VkDescriptorBufferInfo target){
    if(ready||!input.buffer||!target.buffer||
       input.buffer==target.buffer||input.offset||target.offset||
       !input.range||!target.range||
       input.range==VK_WHOLE_SIZE||target.range==VK_WHOLE_SIZE||
       input.range>device.GetMaxStorageBufferRange()||
       target.range>device.GetMaxStorageBufferRange()||
       !NR::CanDecoderTileToLinear(
           requested,{input.range,target.range}))return false;
    auto pool_new=CreateWrappedDescriptorPool(
        device,2u,1u,{VK_DESCRIPTOR_TYPE_STORAGE_BUFFER});
    std::array<VkDescriptorSetLayout,1> layouts{*descriptor_layout};
    auto assigned=CreateWrappedDescriptorSets(
        pool_new,vk::Span<VkDescriptorSetLayout>{
            layouts.data(),layouts.size()});
    if(assigned.IsOutOfPoolMemory()||assigned.size()!=1u)return false;
    const std::array<VkDescriptorBufferInfo,2> buffers{input,target};
    std::array<VkWriteDescriptorSet,2> writes{};
    for(u32 i=0u;i<2u;++i){
        writes[i]=VkWriteDescriptorSet{
            .sType=VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
            .pNext=nullptr,.dstSet=assigned[0],.dstBinding=i,
            .dstArrayElement=0u,.descriptorCount=1u,
            .descriptorType=VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
            .pImageInfo=nullptr,.pBufferInfo=&buffers[i],
            .pTexelBufferView=nullptr};
    }
    device.GetLogical().UpdateDescriptorSets(
        vk::Span<VkWriteDescriptorSet>{writes.data(),writes.size()},{});
    pool=std::move(pool_new);
    sets=std::move(assigned);
    shape=requested;
    storage={input.range,target.range};
    ready=true;
    return true;
}
bool NrRdna2DecoderTileToLinear::Record(){
    if(!ready||sets.size()!=1u||
       !NR::CanDecoderTileToLinear(shape,storage))return false;
    const VkPipeline native=*pipeline;
    const VkPipelineLayout native_layout=*layout;
    const VkDescriptorSet set=sets[0];
    const auto parameters=shape;
    const auto words=std::uint64_t(shape.width)*shape.height*256u;
    const auto groups=static_cast<u32>((words+127u)/128u);
    scheduler.RequestOutsideRenderPassOperationContext();
    scheduler.Record([native,native_layout,set,parameters,groups](
        vk::CommandBuffer cmd){
        cmd.BindPipeline(VK_PIPELINE_BIND_POINT_COMPUTE,native);
        cmd.BindDescriptorSets(VK_PIPELINE_BIND_POINT_COMPUTE,
                               native_layout,0,set,{});
        cmd.PushConstants(native_layout,VK_SHADER_STAGE_COMPUTE_BIT,
                          0,sizeof(parameters),&parameters);
        cmd.Dispatch(groups,1u,1u);
        cmd.PipelineBarrier(VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                            0,kVisible);
    });
    return true;
}
} // namespace Vulkan
