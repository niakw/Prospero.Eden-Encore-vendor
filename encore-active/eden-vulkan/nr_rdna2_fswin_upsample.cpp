// SPDX-License-Identifier: MIT
#include "video_core/renderer_vulkan/nr_rdna2_fswin_upsample.h"
#include "video_core/renderer_vulkan/present/util.h"
#include "video_core/renderer_vulkan/vk_scheduler.h"
#include "video_core/vulkan_common/vulkan_device.h"
#include "video_core/host_shaders/rdna2_nr_fswin_upsample_comp_spv.h"
namespace Vulkan {
namespace {
using Eden::Graphics::NeuralRDNA2::FSwinUpsample;
using Eden::Graphics::NeuralRDNA2::FSwinUpsampleStorage;
static_assert(sizeof(FSwinUpsample)==sizeof(u32)*10u);
constexpr VkMemoryBarrier kVisible{
    .sType=VK_STRUCTURE_TYPE_MEMORY_BARRIER,.pNext=nullptr,
    .srcAccessMask=VK_ACCESS_SHADER_WRITE_BIT,
    .dstAccessMask=VK_ACCESS_SHADER_READ_BIT|VK_ACCESS_SHADER_WRITE_BIT,
};
}
NrRdna2FSwinUpsample::NrRdna2FSwinUpsample(
    const Device& device,Scheduler& scheduler_):scheduler{scheduler_}{
    descriptor_layout=CreateWrappedDescriptorSetLayout(device,
        {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
         VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
         VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
         VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
         VK_DESCRIPTOR_TYPE_STORAGE_BUFFER},VK_SHADER_STAGE_COMPUTE_BIT);
    constexpr VkPushConstantRange range{
        .stageFlags=VK_SHADER_STAGE_COMPUTE_BIT,
        .offset=0,.size=sizeof(FSwinUpsample),
    };
    layout=device.GetLogical().CreatePipelineLayout({
        .sType=VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
        .pNext=nullptr,.flags=0,.setLayoutCount=1,
        .pSetLayouts=descriptor_layout.address(),
        .pushConstantRangeCount=1,.pPushConstantRanges=&range,
    });
    shader=CreateWrappedShaderModule(
        device,RDNA2_NR_FSWIN_UPSAMPLE_COMP_SPV);
    pipeline=CreateWrappedComputePipeline(device,layout,*shader);
}
NrRdna2FSwinUpsample::~NrRdna2FSwinUpsample()=default;
bool NrRdna2FSwinUpsample::Record(
    VkDescriptorSet set,const FSwinUpsample& request,
    const FSwinUpsampleStorage& storage){
    if(!set||
       !Eden::Graphics::NeuralRDNA2::CanFSwinUpsample(request,storage))
        return false;
    const VkPipeline native_pipeline=*pipeline;
    const VkPipelineLayout native_layout=*layout;
    scheduler.RequestOutsideRenderPassOperationContext();
    scheduler.Record([native_pipeline,native_layout,set,request](
                     vk::CommandBuffer cmd){
        cmd.BindPipeline(VK_PIPELINE_BIND_POINT_COMPUTE,native_pipeline);
        cmd.BindDescriptorSets(VK_PIPELINE_BIND_POINT_COMPUTE,
                               native_layout,0,set,{});
        cmd.PushConstants(native_layout,VK_SHADER_STAGE_COMPUTE_BIT,
                          0,sizeof(FSwinUpsample),&request);
        cmd.Dispatch(
            Eden::Graphics::NeuralRDNA2::FSwinUpsampleGridX(request),
            request.windows_x,request.windows_y);
        cmd.PipelineBarrier(VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,0,kVisible);
    });
    return true;
}
}
