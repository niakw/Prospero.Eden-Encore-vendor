// SPDX-License-Identifier: MIT
#include "video_core/renderer_vulkan/nr_rdna2_vit_attention.h"
#include "video_core/renderer_vulkan/present/util.h"
#include "video_core/renderer_vulkan/vk_scheduler.h"
#include "video_core/vulkan_common/vulkan_device.h"
#include "video_core/host_shaders/rdna2_nr_vit_attention_comp_spv.h"
namespace Vulkan {
namespace {
using Eden::Graphics::NeuralRDNA2::VitAttention;
using Eden::Graphics::NeuralRDNA2::VitAttentionStorage;
static_assert(sizeof(VitAttention)==sizeof(u32)*5);
constexpr VkMemoryBarrier kVisible{
    .sType=VK_STRUCTURE_TYPE_MEMORY_BARRIER,
    .pNext=nullptr,
    .srcAccessMask=VK_ACCESS_SHADER_WRITE_BIT,
    .dstAccessMask=VK_ACCESS_SHADER_READ_BIT|VK_ACCESS_SHADER_WRITE_BIT,
};
}
NrRdna2VitAttention::NrRdna2VitAttention(
    const Device& device,Scheduler& scheduler_):scheduler{scheduler_}{
    descriptor_layout=CreateWrappedDescriptorSetLayout(
        device,{VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
                VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
                VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
                VK_DESCRIPTOR_TYPE_STORAGE_BUFFER},VK_SHADER_STAGE_COMPUTE_BIT);
    constexpr VkPushConstantRange range{
        .stageFlags=VK_SHADER_STAGE_COMPUTE_BIT,
        .offset=0,.size=sizeof(VitAttention),
    };
    layout=device.GetLogical().CreatePipelineLayout({
        .sType=VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
        .pNext=nullptr,.flags=0,.setLayoutCount=1,
        .pSetLayouts=descriptor_layout.address(),
        .pushConstantRangeCount=1,.pPushConstantRanges=&range,
    });
    shader=CreateWrappedShaderModule(device,RDNA2_NR_VIT_ATTENTION_COMP_SPV);
    pipeline=CreateWrappedComputePipeline(device,layout,*shader);
}
NrRdna2VitAttention::~NrRdna2VitAttention()=default;
bool NrRdna2VitAttention::Record(
    VkDescriptorSet validated_set,const VitAttention& request,
    const VitAttentionStorage& storage){
    if(!validated_set||
       !Eden::Graphics::NeuralRDNA2::CanVitAttention(request,storage))
        return false;
    const VkPipeline native_pipeline=*pipeline;
    const VkPipelineLayout native_layout=*layout;
    scheduler.RequestOutsideRenderPassOperationContext();
    scheduler.Record([validated_set,native_pipeline,native_layout,
                      request](vk::CommandBuffer cmd){
        cmd.BindPipeline(VK_PIPELINE_BIND_POINT_COMPUTE,native_pipeline);
        cmd.BindDescriptorSets(VK_PIPELINE_BIND_POINT_COMPUTE,native_layout,
                               0,validated_set,{});
        cmd.PushConstants(native_layout,VK_SHADER_STAGE_COMPUTE_BIT,
                          0,sizeof(VitAttention),&request);
        cmd.Dispatch((request.tokens+31u)/32u,32,1);
        cmd.PipelineBarrier(VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,0,kVisible);
    });
    return true;
}
} // namespace Vulkan
