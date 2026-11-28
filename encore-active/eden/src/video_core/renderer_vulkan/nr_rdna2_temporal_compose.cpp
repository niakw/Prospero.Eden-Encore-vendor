// SPDX-License-Identifier: MIT
#include "video_core/renderer_vulkan/nr_rdna2_temporal_compose.h"
#include "video_core/renderer_vulkan/nr_rdna2_temporal_blend_weight_loader.h"
#include "video_core/renderer_vulkan/present/util.h"
#include "video_core/renderer_vulkan/vk_scheduler.h"
#include "video_core/vulkan_common/vulkan_device.h"
#include "video_core/host_shaders/rdna2_nr_temporal_compose_comp_spv.h"
#include <array>
#include <cstdint>
#include <utility>
namespace Vulkan {
namespace NR=Eden::Graphics::NeuralRDNA2;
namespace {
static_assert(sizeof(NR::NRTemporalComposeShape)==32u);
constexpr VkMemoryBarrier kVisible{
    .sType=VK_STRUCTURE_TYPE_MEMORY_BARRIER,
    .pNext=nullptr,.srcAccessMask=VK_ACCESS_SHADER_WRITE_BIT,
    .dstAccessMask=VK_ACCESS_SHADER_READ_BIT|VK_ACCESS_SHADER_WRITE_BIT
};
}
NrRdna2TemporalCompose::NrRdna2TemporalCompose(
    const Device& d,Scheduler& s):device{d},scheduler{s}{
    descriptor_layout=CreateWrappedDescriptorSetLayout(
        d,{VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
           VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
           VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
           VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
           VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
           VK_DESCRIPTOR_TYPE_STORAGE_BUFFER},
        VK_SHADER_STAGE_COMPUTE_BIT);
    constexpr VkPushConstantRange push{
        .stageFlags=VK_SHADER_STAGE_COMPUTE_BIT,
        .offset=0,.size=sizeof(NR::NRTemporalComposeShape)};
    layout=d.GetLogical().CreatePipelineLayout({
        .sType=VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
        .pNext=nullptr,.flags=0,.setLayoutCount=1,
        .pSetLayouts=descriptor_layout.address(),
        .pushConstantRangeCount=1,.pPushConstantRanges=&push});
    shader=CreateWrappedShaderModule(
        d,RDNA2_NR_TEMPORAL_COMPOSE_COMP_SPV);
    pipeline=CreateWrappedComputePipeline(d,layout,*shader);
}
NrRdna2TemporalCompose::~NrRdna2TemporalCompose()=default;
bool NrRdna2TemporalCompose::InitializeWithModel(
    const NR::NeuralModelFile& model,std::uint32_t post_block,
    NR::NRTemporalComposeShape request,
    VkDescriptorBufferInfo src,VkDescriptorBufferInfo head,
    VkDescriptorBufferInfo motion,VkDescriptorBufferInfo previous,
    VkDescriptorBufferInfo dst,VkDescriptorBufferInfo next_dst){
    if(attempted)return false;
    attempted=true;
    const std::array<VkDescriptorBufferInfo,6> resources{
        src,head,motion,previous,dst,next_dst};
    std::array<std::uint64_t,6> capacities{};
    for(std::size_t i=0u;i<resources.size();++i){
        const auto& item=resources[i];
        if(!item.buffer||item.offset||!item.range||
           item.range==VK_WHOLE_SIZE||
           item.range>device.GetMaxStorageBufferRange())return false;
        for(std::size_t j=0u;j<i;++j)
            if(resources[j].buffer==item.buffer)return false;
        capacities[i]=item.range;
    }
    // The graph's fourth learned head channel is a temporal logit.
    // The blend scalar MUST originate from the user's indexed model,
    // and is intentionally absent from some legacy unpacked packs.
    float authentic_scale=0.0f;
    if(!NR::PrepareNRTemporalBlendScale(
           model,post_block,authentic_scale))return false;
    request.blend_scale=authentic_scale;
    if(!NR::CanNRTemporalCompose(request,capacities))return false;
    auto proposed_pool=CreateWrappedDescriptorPool(
        device,6u,1u,{VK_DESCRIPTOR_TYPE_STORAGE_BUFFER});
    std::array<VkDescriptorSetLayout,1> layouts{*descriptor_layout};
    auto assigned=CreateWrappedDescriptorSets(
        proposed_pool,vk::Span<VkDescriptorSetLayout>{
            layouts.data(),layouts.size()});
    if(assigned.IsOutOfPoolMemory()||assigned.size()!=1u)return false;
    std::array<VkWriteDescriptorSet,6> writes{};
    for(std::uint32_t i=0u;i<6u;++i){
        writes[i]=VkWriteDescriptorSet{
            .sType=VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
            .pNext=nullptr,.dstSet=assigned[0],.dstBinding=i,
            .dstArrayElement=0u,.descriptorCount=1u,
            .descriptorType=VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
            .pImageInfo=nullptr,.pBufferInfo=&resources[i],
            .pTexelBufferView=nullptr};
    }
    device.GetLogical().UpdateDescriptorSets(
        vk::Span<VkWriteDescriptorSet>{writes.data(),writes.size()},{});
    pool=std::move(proposed_pool);
    sets=std::move(assigned);
    proxy=src;
    features=head;
    result=dst;
    next=next_dst;
    shape=request;
    ready=true;
    return true;
}
bool NrRdna2TemporalCompose::Record(){
    if(!ready||record_attempted||sets.size()!=1u)return false;
    record_attempted=true;
    const VkPipeline native=*pipeline;
    const VkPipelineLayout native_layout=*layout;
    const auto p=shape;
    const auto set=sets[0];
    const std::uint32_t gx=(p.width+7u)/8u,gy=(p.height+7u)/8u;
    scheduler.RequestOutsideRenderPassOperationContext();
    scheduler.Record([native,native_layout,p,set,gx,gy](vk::CommandBuffer cmd){
        cmd.BindPipeline(VK_PIPELINE_BIND_POINT_COMPUTE,native);
        cmd.BindDescriptorSets(VK_PIPELINE_BIND_POINT_COMPUTE,
                               native_layout,0u,set,{});
        cmd.PushConstants(native_layout,VK_SHADER_STAGE_COMPUTE_BIT,
                          0u,sizeof(p),&p);
        cmd.Dispatch(gx,gy,1u);
        cmd.PipelineBarrier(VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                            0u,kVisible);
    });
    recorded=true;
    return true;
}
} // namespace Vulkan
