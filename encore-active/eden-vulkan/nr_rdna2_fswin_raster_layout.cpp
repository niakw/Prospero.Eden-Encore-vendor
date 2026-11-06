// SPDX-License-Identifier: MIT
#include "video_core/renderer_vulkan/nr_rdna2_fswin_raster_layout.h"
#include "video_core/renderer_vulkan/present/util.h"
#include "video_core/renderer_vulkan/vk_scheduler.h"
#include "video_core/vulkan_common/vulkan_device.h"
#include "video_core/host_shaders/rdna2_nr_fswin_raster_layout_comp_spv.h"
namespace Vulkan {
namespace {
using Eden::Graphics::NeuralRDNA2::FSwinRasterLayout;
using Eden::Graphics::NeuralRDNA2::FSwinRasterLayoutStorage;
static_assert(sizeof(FSwinRasterLayout)==sizeof(u32)*6);
constexpr VkMemoryBarrier kVisible{
    .sType=VK_STRUCTURE_TYPE_MEMORY_BARRIER,
    .pNext=nullptr,
    .srcAccessMask=VK_ACCESS_SHADER_WRITE_BIT,
    .dstAccessMask=VK_ACCESS_SHADER_READ_BIT|VK_ACCESS_SHADER_WRITE_BIT,
};
}
NrRdna2FSwinRasterLayout::NrRdna2FSwinRasterLayout(
    const Device& device,Scheduler& scheduler_):scheduler{scheduler_}{
    descriptor_layout=CreateWrappedDescriptorSetLayout(
        device,{VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
                VK_DESCRIPTOR_TYPE_STORAGE_BUFFER},VK_SHADER_STAGE_COMPUTE_BIT);
    constexpr VkPushConstantRange range{
        .stageFlags=VK_SHADER_STAGE_COMPUTE_BIT,
        .offset=0,.size=sizeof(FSwinRasterLayout),
    };
    layout=device.GetLogical().CreatePipelineLayout({
        .sType=VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
        .pNext=nullptr,.flags=0,.setLayoutCount=1,
        .pSetLayouts=descriptor_layout.address(),
        .pushConstantRangeCount=1,.pPushConstantRanges=&range,
    });
    shader=CreateWrappedShaderModule(
        device,RDNA2_NR_FSWIN_RASTER_LAYOUT_COMP_SPV);
    pipeline=CreateWrappedComputePipeline(device,layout,*shader);
}
NrRdna2FSwinRasterLayout::~NrRdna2FSwinRasterLayout()=default;
bool NrRdna2FSwinRasterLayout::Record(
    VkDescriptorSet validated_set,const FSwinRasterLayout& request,
    const FSwinRasterLayoutStorage& storage){
    if(!validated_set||
       !Eden::Graphics::NeuralRDNA2::CanFSwinRasterLayout(
           request,storage))return false;
    const VkPipeline native_pipeline=*pipeline;
    const VkPipelineLayout native_layout=*layout;
    scheduler.RequestOutsideRenderPassOperationContext();
    scheduler.Record([validated_set,native_pipeline,native_layout,
                      request](vk::CommandBuffer cmd){
        cmd.BindPipeline(VK_PIPELINE_BIND_POINT_COMPUTE,native_pipeline);
        cmd.BindDescriptorSets(VK_PIPELINE_BIND_POINT_COMPUTE,
                               native_layout,0,validated_set,{});
        cmd.PushConstants(native_layout,VK_SHADER_STAGE_COMPUTE_BIT,
                          0,sizeof(FSwinRasterLayout),&request);
        cmd.Dispatch(
            Eden::Graphics::NeuralRDNA2::FSwinRasterLayoutGridX(request),
            request.windows_x,request.windows_y);
        cmd.PipelineBarrier(VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,0,kVisible);
    });
    return true;
}
} // namespace Vulkan
