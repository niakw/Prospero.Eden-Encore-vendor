// SPDX-License-Identifier: MIT
#include "video_core/renderer_vulkan/nr_rdna2_nohistory_compose.h"
#include "video_core/renderer_vulkan/present/util.h"
#include "video_core/renderer_vulkan/vk_scheduler.h"
#include "video_core/vulkan_common/vulkan_device.h"
#include "video_core/host_shaders/rdna2_nr_nohistory_compose_comp_spv.h"
#include <array>
#include <cstdint>
#include <utility>
namespace Vulkan {
namespace NR=Eden::Graphics::NeuralRDNA2;
namespace {
static_assert(sizeof(NR::NRNoHistoryComposeShape)==16u);
constexpr VkMemoryBarrier kVisible{
    .sType=VK_STRUCTURE_TYPE_MEMORY_BARRIER,.pNext=nullptr,
    .srcAccessMask=VK_ACCESS_SHADER_WRITE_BIT,
    .dstAccessMask=VK_ACCESS_SHADER_READ_BIT|VK_ACCESS_SHADER_WRITE_BIT,
};
}
NrRdna2NoHistoryCompose::NrRdna2NoHistoryCompose(
    const Device& d,Scheduler& s):device{d},scheduler{s}{
    descriptor_layout=CreateWrappedDescriptorSetLayout(
        d,{VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
           VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
           VK_DESCRIPTOR_TYPE_STORAGE_BUFFER},VK_SHADER_STAGE_COMPUTE_BIT);
    constexpr VkPushConstantRange push{
        .stageFlags=VK_SHADER_STAGE_COMPUTE_BIT,
        .offset=0,.size=sizeof(NR::NRNoHistoryComposeShape)};
    layout=d.GetLogical().CreatePipelineLayout({
        .sType=VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
        .pNext=nullptr,.flags=0,.setLayoutCount=1,
        .pSetLayouts=descriptor_layout.address(),
        .pushConstantRangeCount=1,.pPushConstantRanges=&push});
    shader=CreateWrappedShaderModule(
        d,RDNA2_NR_NOHISTORY_COMPOSE_COMP_SPV);
    pipeline=CreateWrappedComputePipeline(d,layout,*shader);
}
NrRdna2NoHistoryCompose::~NrRdna2NoHistoryCompose()=default;
bool NrRdna2NoHistoryCompose::Initialize(
    NR::NRNoHistoryComposeShape request,
    VkDescriptorBufferInfo src,VkDescriptorBufferInfo head,
    VkDescriptorBufferInfo dst){
    if(attempted)return false;
    attempted=true;
    const std::array<VkDescriptorBufferInfo,3> descriptors{src,head,dst};
    for(std::size_t i=0;i<descriptors.size();++i){
        const auto& b=descriptors[i];
        if(!b.buffer||b.offset||!b.range||b.range==VK_WHOLE_SIZE||
           b.range>device.GetMaxStorageBufferRange())return false;
        for(std::size_t j=0;j<i;++j)
            if(descriptors[j].buffer==b.buffer)return false;
    }
    if(!NR::CanNRNoHistoryCompose(
        request,{src.range,head.range,dst.range}))return false;
    auto proposed_pool=CreateWrappedDescriptorPool(
        device,3u,1u,{VK_DESCRIPTOR_TYPE_STORAGE_BUFFER});
    std::array<VkDescriptorSetLayout,1> layouts{*descriptor_layout};
    auto assigned=CreateWrappedDescriptorSets(
        proposed_pool,vk::Span<VkDescriptorSetLayout>{
            layouts.data(),layouts.size()});
    if(assigned.IsOutOfPoolMemory()||assigned.size()!=1u)return false;
    std::array<VkWriteDescriptorSet,3> writes{};
    for(std::uint32_t i=0u;i<3u;++i){
        writes[i]=VkWriteDescriptorSet{
            .sType=VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
            .pNext=nullptr,.dstSet=assigned[0],.dstBinding=i,
            .dstArrayElement=0,.descriptorCount=1,
            .descriptorType=VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
            .pImageInfo=nullptr,.pBufferInfo=&descriptors[i],
            .pTexelBufferView=nullptr};
    }
    device.GetLogical().UpdateDescriptorSets(
        vk::Span<VkWriteDescriptorSet>{writes.data(),writes.size()},{});
    pool=std::move(proposed_pool);
    sets=std::move(assigned);
    shape=request;
    proxy=src;
    features=head;
    result=dst;
    ready=true;
    return true;
}
bool NrRdna2NoHistoryCompose::Record(){
    if(!ready||record_attempted||sets.size()!=1u)return false;
    record_attempted=true;
    const VkPipeline native=*pipeline;
    const VkPipelineLayout native_layout=*layout;
    const auto p=shape;
    const auto set=sets[0];
    const std::uint32_t gx=(p.width+7u)/8u;
    const std::uint32_t gy=(p.height+7u)/8u;
    scheduler.RequestOutsideRenderPassOperationContext();
    scheduler.Record([native,native_layout,set,p,gx,gy](
                     vk::CommandBuffer cmd){
        cmd.BindPipeline(VK_PIPELINE_BIND_POINT_COMPUTE,native);
        cmd.BindDescriptorSets(VK_PIPELINE_BIND_POINT_COMPUTE,
                               native_layout,0,set,{});
        cmd.PushConstants(native_layout,VK_SHADER_STAGE_COMPUTE_BIT,
                          0,sizeof(p),&p);
        cmd.Dispatch(gx,gy,1u);
        cmd.PipelineBarrier(VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,0,kVisible);
    });
    recorded=true;
    return true;
}
} // namespace Vulkan
