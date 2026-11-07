// SPDX-License-Identifier: MIT
#include "video_core/renderer_vulkan/nr_rdna2_nohistory_present.h"
#include "video_core/renderer_vulkan/present/util.h"
#include "video_core/renderer_vulkan/vk_scheduler.h"
#include "video_core/vulkan_common/vulkan_device.h"
#include "video_core/host_shaders/rdna2_nr_nohistory_present_comp_spv.h"
#include <array>
#include <cstdint>
#include <utility>
namespace Vulkan {
namespace NR=Eden::Graphics::NeuralRDNA2;
namespace {
static_assert(sizeof(NR::NRNoHistoryPresentShape)==4u*sizeof(u32));
constexpr VkMemoryBarrier kVisible{
    .sType=VK_STRUCTURE_TYPE_MEMORY_BARRIER,.pNext=nullptr,
    .srcAccessMask=VK_ACCESS_SHADER_WRITE_BIT,
    .dstAccessMask=VK_ACCESS_SHADER_READ_BIT,
};
}
NrRdna2NoHistoryPresent::NrRdna2NoHistoryPresent(
    const Device& d,Scheduler& s):device{d},scheduler{s}{
    descriptor_layout=CreateWrappedDescriptorSetLayout(
        d,{VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
           VK_DESCRIPTOR_TYPE_STORAGE_IMAGE},VK_SHADER_STAGE_COMPUTE_BIT);
    constexpr VkPushConstantRange pc{
        .stageFlags=VK_SHADER_STAGE_COMPUTE_BIT,
        .offset=0u,.size=sizeof(NR::NRNoHistoryPresentShape)};
    layout=d.GetLogical().CreatePipelineLayout({
        .sType=VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
        .pNext=nullptr,.flags=0u,.setLayoutCount=1u,
        .pSetLayouts=descriptor_layout.address(),
        .pushConstantRangeCount=1u,.pPushConstantRanges=&pc});
    shader=CreateWrappedShaderModule(d,RDNA2_NR_NOHISTORY_PRESENT_COMP_SPV);
    pipeline=CreateWrappedComputePipeline(d,layout,*shader);
}
NrRdna2NoHistoryPresent::~NrRdna2NoHistoryPresent()=default;
bool NrRdna2NoHistoryPresent::Initialize(
    NR::NRNoHistoryPresentShape requested,VkDescriptorBufferInfo input,
    VkImageView view,VkFormat format,VkExtent2D extent){
    if(attempted)return false;
    attempted=true;
    if(!input.buffer||input.offset||!input.range||
       input.range==VK_WHOLE_SIZE||
       input.range>device.GetMaxStorageBufferRange()||
       view==VK_NULL_HANDLE||format!=VK_FORMAT_R32G32B32A32_SFLOAT||
       !NR::CanNRNoHistoryPresent(
           requested,input.range,{extent.width,extent.height}))
        return false;
    const auto props=device.GetPhysical().GetFormatProperties(format);
    if(!(props.optimalTilingFeatures&VK_FORMAT_FEATURE_STORAGE_IMAGE_BIT))
        return false;
    auto proposed_pool=CreateWrappedDescriptorPool(
        device,2u,1u,
        {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,VK_DESCRIPTOR_TYPE_STORAGE_IMAGE});
    std::array<VkDescriptorSetLayout,1> layouts{*descriptor_layout};
    auto allocated=CreateWrappedDescriptorSets(
        proposed_pool,vk::Span<VkDescriptorSetLayout>{
            layouts.data(),layouts.size()});
    if(allocated.IsOutOfPoolMemory()||allocated.size()!=1u)return false;
    const VkDescriptorImageInfo image{
        .sampler=VK_NULL_HANDLE,.imageView=view,
        .imageLayout=VK_IMAGE_LAYOUT_GENERAL};
    const VkWriteDescriptorSet buffers{
        .sType=VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
        .pNext=nullptr,.dstSet=allocated[0],.dstBinding=0,
        .dstArrayElement=0u,.descriptorCount=1u,
        .descriptorType=VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
        .pImageInfo=nullptr,.pBufferInfo=&input,.pTexelBufferView=nullptr};
    const VkWriteDescriptorSet images{
        .sType=VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
        .pNext=nullptr,.dstSet=allocated[0],.dstBinding=1,
        .dstArrayElement=0u,.descriptorCount=1u,
        .descriptorType=VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
        .pImageInfo=&image,.pBufferInfo=nullptr,.pTexelBufferView=nullptr};
    const std::array<VkWriteDescriptorSet,2> writes{buffers,images};
    device.GetLogical().UpdateDescriptorSets(
        vk::Span<const VkWriteDescriptorSet>{writes.data(),writes.size()},{});
    pool=std::move(proposed_pool);
    sets=std::move(allocated);
    shape=requested;
    source=input;
    destination=view;
    ready=true;
    return true;
}
bool NrRdna2NoHistoryPresent::Record(){
    if(!ready||record_attempted||sets.size()!=1u)return false;
    record_attempted=true;
    const VkPipeline native=*pipeline;
    const VkPipelineLayout native_layout=*layout;
    const auto pc=shape;
    const VkDescriptorSet set=sets[0];
    const auto gx=(pc.raster_height+7u)/8u;
    const auto gy=(pc.raster_width+7u)/8u;
    scheduler.RequestOutsideRenderPassOperationContext();
    scheduler.Record([native,native_layout,pc,set,gx,gy](vk::CommandBuffer cmd){
        cmd.BindPipeline(VK_PIPELINE_BIND_POINT_COMPUTE,native);
        cmd.BindDescriptorSets(VK_PIPELINE_BIND_POINT_COMPUTE,
                               native_layout,0u,set,{});
        cmd.PushConstants(native_layout,VK_SHADER_STAGE_COMPUTE_BIT,
                          0u,sizeof(pc),&pc);
        cmd.Dispatch(gx,gy,1u);
        cmd.PipelineBarrier(VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                            VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT |
                            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,0u,kVisible);
    });
    recorded=true;
    return true;
}
} // namespace Vulkan
