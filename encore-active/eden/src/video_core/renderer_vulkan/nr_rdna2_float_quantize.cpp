// SPDX-License-Identifier: MIT
#include "video_core/renderer_vulkan/nr_rdna2_float_quantize.h"
#include "video_core/renderer_vulkan/present/util.h"
#include "video_core/renderer_vulkan/vk_scheduler.h"
#include "video_core/vulkan_common/vulkan_device.h"
#include "video_core/host_shaders/rdna2_nr_float_quantize_comp_spv.h"
namespace Vulkan {
namespace {
using Eden::Graphics::NeuralRDNA2::FloatQuantize;
static_assert(sizeof(FloatQuantize)==4u*sizeof(u32));
constexpr VkMemoryBarrier kVisibility{
    .sType=VK_STRUCTURE_TYPE_MEMORY_BARRIER,.pNext=nullptr,
    .srcAccessMask=VK_ACCESS_SHADER_WRITE_BIT,
    .dstAccessMask=VK_ACCESS_SHADER_READ_BIT|VK_ACCESS_SHADER_WRITE_BIT,
};
}
NrRdna2FloatQuantize::NrRdna2FloatQuantize(
    const Device& d,Scheduler& s):scheduler{s}{
    descriptor_layout=CreateWrappedDescriptorSetLayout(
        d,{VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,VK_DESCRIPTOR_TYPE_STORAGE_BUFFER},
        VK_SHADER_STAGE_COMPUTE_BIT);
    constexpr VkPushConstantRange range{
        .stageFlags=VK_SHADER_STAGE_COMPUTE_BIT,
        .offset=0,.size=sizeof(FloatQuantize),
    };
    layout=d.GetLogical().CreatePipelineLayout({
        .sType=VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
        .pNext=nullptr,.flags=0,.setLayoutCount=1,
        .pSetLayouts=descriptor_layout.address(),
        .pushConstantRangeCount=1,.pPushConstantRanges=&range,
    });
    shader=CreateWrappedShaderModule(d,RDNA2_NR_FLOAT_QUANTIZE_COMP_SPV);
    pipeline=CreateWrappedComputePipeline(d,layout,*shader);
}
NrRdna2FloatQuantize::~NrRdna2FloatQuantize()=default;
bool NrRdna2FloatQuantize::Record(
    VkDescriptorSet descriptor,VkDescriptorBufferInfo input,
    VkDescriptorBufferInfo output,const FloatQuantize& request){
    if(!descriptor||!input.buffer||!output.buffer||
       input.buffer==output.buffer||
       input.range==VK_WHOLE_SIZE||output.range==VK_WHOLE_SIZE||
       !Eden::Graphics::NeuralRDNA2::CanFloatQuantize(
           request,{input.range,output.range}))return false;
    const VkPipeline native_pipeline=*pipeline;
    const VkPipelineLayout native_layout=*layout;
    const u32 groups=Eden::Graphics::NeuralRDNA2::FloatQuantizeGroups(request);
    scheduler.RequestOutsideRenderPassOperationContext();
    scheduler.Record([native_pipeline,native_layout,descriptor,request,groups](
                     vk::CommandBuffer cmd){
        cmd.BindPipeline(VK_PIPELINE_BIND_POINT_COMPUTE,native_pipeline);
        cmd.BindDescriptorSets(VK_PIPELINE_BIND_POINT_COMPUTE,
                               native_layout,0,descriptor,{});
        cmd.PushConstants(native_layout,VK_SHADER_STAGE_COMPUTE_BIT,
                          0,sizeof(FloatQuantize),&request);
        cmd.Dispatch(groups,1,1);
        cmd.PipelineBarrier(VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,0,kVisibility);
    });
    return true;
}
} // namespace Vulkan
