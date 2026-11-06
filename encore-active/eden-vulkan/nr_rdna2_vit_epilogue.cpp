// SPDX-License-Identifier: MIT
#include "video_core/renderer_vulkan/nr_rdna2_vit_epilogue.h"
#include "video_core/renderer_vulkan/present/util.h"
#include "video_core/renderer_vulkan/vk_scheduler.h"
#include "video_core/vulkan_common/vulkan_device.h"
#include "video_core/host_shaders/rdna2_nr_vit_activation_comp_spv.h"
#include "video_core/host_shaders/rdna2_nr_vit_residual_comp_spv.h"
#include <array>
#include <cstdint>
namespace Vulkan {
namespace NR=Eden::Graphics::NeuralRDNA2;
namespace {
static_assert(sizeof(NR::VitActivation)==4u*sizeof(u32));
static_assert(sizeof(NR::VitResidual)==6u*sizeof(u32));
constexpr VkMemoryBarrier kVisible{
    .sType=VK_STRUCTURE_TYPE_MEMORY_BARRIER,.pNext=nullptr,
    .srcAccessMask=VK_ACCESS_SHADER_WRITE_BIT,
    .dstAccessMask=VK_ACCESS_SHADER_READ_BIT|VK_ACCESS_SHADER_WRITE_BIT};
bool ValidBuffer(const VkDescriptorBufferInfo& b) {
    return b.buffer!=VK_NULL_HANDLE&&b.offset==0u&&
           b.range!=0u&&b.range!=VK_WHOLE_SIZE;
}
}
NrRdna2VitEpilogue::NrRdna2VitEpilogue(
    const Device& d,Scheduler& s):scheduler{s}{
    activation_layout=CreateWrappedDescriptorSetLayout(
        d,{VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,VK_DESCRIPTOR_TYPE_STORAGE_BUFFER},
        VK_SHADER_STAGE_COMPUTE_BIT);
    residual_layout=CreateWrappedDescriptorSetLayout(
        d,{VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
           VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,VK_DESCRIPTOR_TYPE_STORAGE_BUFFER},
        VK_SHADER_STAGE_COMPUTE_BIT);
    constexpr VkPushConstantRange act_push{
        .stageFlags=VK_SHADER_STAGE_COMPUTE_BIT,.offset=0,
        .size=sizeof(NR::VitActivation)};
    constexpr VkPushConstantRange residual_push{
        .stageFlags=VK_SHADER_STAGE_COMPUTE_BIT,.offset=0,
        .size=sizeof(NR::VitResidual)};
    activation_pipeline_layout=d.GetLogical().CreatePipelineLayout({
        .sType=VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
        .pNext=nullptr,.flags=0,.setLayoutCount=1,
        .pSetLayouts=activation_layout.address(),
        .pushConstantRangeCount=1,.pPushConstantRanges=&act_push});
    residual_pipeline_layout=d.GetLogical().CreatePipelineLayout({
        .sType=VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
        .pNext=nullptr,.flags=0,.setLayoutCount=1,
        .pSetLayouts=residual_layout.address(),
        .pushConstantRangeCount=1,.pPushConstantRanges=&residual_push});
    activation_shader=CreateWrappedShaderModule(
        d,RDNA2_NR_VIT_ACTIVATION_COMP_SPV);
    residual_shader=CreateWrappedShaderModule(
        d,RDNA2_NR_VIT_RESIDUAL_COMP_SPV);
    activation_pipeline=CreateWrappedComputePipeline(
        d,activation_pipeline_layout,*activation_shader);
    residual_pipeline=CreateWrappedComputePipeline(
        d,residual_pipeline_layout,*residual_shader);
}
NrRdna2VitEpilogue::~NrRdna2VitEpilogue()=default;
bool NrRdna2VitEpilogue::RecordActivation(
    VkDescriptorSet set,VkDescriptorBufferInfo source,
    VkDescriptorBufferInfo output,const NR::VitActivation& p){
    if(!set||!ValidBuffer(source)||!ValidBuffer(output)||
       source.buffer==output.buffer||
       !NR::CanVitActivation(p,{source.range,0,0,output.range}))
        return false;
    const VkPipeline native=*activation_pipeline;
    const VkPipelineLayout layout=*activation_pipeline_layout;
    const u32 groups=NR::VitEpilogueGroups(p.tokens,p.channels);
    scheduler.RequestOutsideRenderPassOperationContext();
    scheduler.Record([set,native,layout,groups,p](vk::CommandBuffer cmd){
        cmd.BindPipeline(VK_PIPELINE_BIND_POINT_COMPUTE,native);
        cmd.BindDescriptorSets(VK_PIPELINE_BIND_POINT_COMPUTE,layout,0,set,{});
        cmd.PushConstants(layout,VK_SHADER_STAGE_COMPUTE_BIT,
                          0,sizeof(p),&p);
        cmd.Dispatch(groups,1,1);
        cmd.PipelineBarrier(VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,0,kVisible);
    });
    return true;
}
bool NrRdna2VitEpilogue::RecordResidual(
    VkDescriptorSet set,VkDescriptorBufferInfo source,
    VkDescriptorBufferInfo residual,VkDescriptorBufferInfo gain,
    VkDescriptorBufferInfo output,const NR::VitResidual& p){
    if(!set)return false;
    const std::array<VkDescriptorBufferInfo,4> buffers{
        source,residual,gain,output};
    for(std::size_t i=0u;i<buffers.size();++i){
        if(!ValidBuffer(buffers[i]))return false;
        for(std::size_t j=0u;j<i;++j)
            if(buffers[j].buffer==buffers[i].buffer)return false;
    }
    if(!NR::CanVitResidual(p,{
        source.range,residual.range,gain.range,output.range}))return false;
    const VkPipeline native=*residual_pipeline;
    const VkPipelineLayout layout=*residual_pipeline_layout;
    const u32 groups=NR::VitEpilogueGroups(p.tokens,p.channels);
    scheduler.RequestOutsideRenderPassOperationContext();
    scheduler.Record([set,native,layout,groups,p](vk::CommandBuffer cmd){
        cmd.BindPipeline(VK_PIPELINE_BIND_POINT_COMPUTE,native);
        cmd.BindDescriptorSets(VK_PIPELINE_BIND_POINT_COMPUTE,layout,0,set,{});
        cmd.PushConstants(layout,VK_SHADER_STAGE_COMPUTE_BIT,
                          0,sizeof(p),&p);
        cmd.Dispatch(groups,1,1);
        cmd.PipelineBarrier(VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,0,kVisible);
    });
    return true;
}
} // namespace Vulkan
