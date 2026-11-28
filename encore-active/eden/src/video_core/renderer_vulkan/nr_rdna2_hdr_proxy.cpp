// SPDX-License-Identifier: MIT
#include "video_core/renderer_vulkan/nr_rdna2_hdr_proxy.h"
#include "video_core/renderer_vulkan/present/util.h"
#include "video_core/renderer_vulkan/vk_scheduler.h"
#include "video_core/vulkan_common/vulkan_device.h"
#include "video_core/host_shaders/rdna2_nr_hdr_proxy_comp_spv.h"
#include <array>
#include <cstdint>
#include <utility>
namespace Vulkan {
namespace NR=Eden::Graphics::NeuralRDNA2;
namespace {
static_assert(sizeof(NR::NRHDRProxyShape)==16u);
constexpr VkMemoryBarrier kVisible{
    .sType=VK_STRUCTURE_TYPE_MEMORY_BARRIER,.pNext=nullptr,
    .srcAccessMask=VK_ACCESS_SHADER_WRITE_BIT,
    .dstAccessMask=VK_ACCESS_SHADER_READ_BIT|VK_ACCESS_SHADER_WRITE_BIT,
};
}
NrRdna2HDRProxy::NrRdna2HDRProxy(
    const Device& d,Scheduler& s):device{d},scheduler{s}{
    descriptor_layout=CreateWrappedDescriptorSetLayout(
        d,{VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
           VK_DESCRIPTOR_TYPE_STORAGE_BUFFER},VK_SHADER_STAGE_COMPUTE_BIT);
    constexpr VkPushConstantRange push{
        .stageFlags=VK_SHADER_STAGE_COMPUTE_BIT,
        .offset=0,.size=sizeof(NR::NRHDRProxyShape)};
    layout=d.GetLogical().CreatePipelineLayout({
        .sType=VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
        .pNext=nullptr,.flags=0,.setLayoutCount=1,
        .pSetLayouts=descriptor_layout.address(),
        .pushConstantRangeCount=1,.pPushConstantRanges=&push});
    shader=CreateWrappedShaderModule(d,RDNA2_NR_HDR_PROXY_COMP_SPV);
    pipeline=CreateWrappedComputePipeline(d,layout,*shader);
}
NrRdna2HDRProxy::~NrRdna2HDRProxy()=default;
bool NrRdna2HDRProxy::Initialize(
    NR::NRHDRProxyShape request,
    VkDescriptorBufferInfo input,VkDescriptorBufferInfo result){
    if(attempted)return false;
    attempted=true;
    const std::array<VkDescriptorBufferInfo,2> buffers{input,result};
    for(std::size_t i=0u;i<buffers.size();++i){
        const auto& b=buffers[i];
        if(!b.buffer||b.offset||!b.range||b.range==VK_WHOLE_SIZE||
           b.range>device.GetMaxStorageBufferRange())return false;
    }
    if(input.buffer==result.buffer||
       !NR::CanNRHDRProxy(request,{input.range,result.range}))return false;
    auto proposed_pool=CreateWrappedDescriptorPool(
        device,2u,1u,{VK_DESCRIPTOR_TYPE_STORAGE_BUFFER});
    std::array<VkDescriptorSetLayout,1> layouts{*descriptor_layout};
    auto assigned=CreateWrappedDescriptorSets(
        proposed_pool,vk::Span<VkDescriptorSetLayout>{
            layouts.data(),layouts.size()});
    if(assigned.IsOutOfPoolMemory()||assigned.size()!=1u)return false;
    std::array<VkWriteDescriptorSet,2> writes{};
    for(std::uint32_t i=0u;i<2u;++i){
        writes[i]=VkWriteDescriptorSet{
            .sType=VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
            .pNext=nullptr,.dstSet=assigned[0],.dstBinding=i,
            .dstArrayElement=0,.descriptorCount=1,
            .descriptorType=VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
            .pImageInfo=nullptr,.pBufferInfo=&buffers[i],
            .pTexelBufferView=nullptr};
    }
    device.GetLogical().UpdateDescriptorSets(
        vk::Span<VkWriteDescriptorSet>{writes.data(),writes.size()},{});
    pool=std::move(proposed_pool);
    sets=std::move(assigned);
    shape=request;
    source=input;
    output=result;
    ready=true;
    return true;
}
bool NrRdna2HDRProxy::Record(){
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
