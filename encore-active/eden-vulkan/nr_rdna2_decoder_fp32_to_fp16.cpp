// SPDX-License-Identifier: MIT
#include "video_core/renderer_vulkan/nr_rdna2_decoder_fp32_to_fp16.h"
#include "video_core/renderer_vulkan/present/util.h"
#include "video_core/renderer_vulkan/vk_scheduler.h"
#include "video_core/vulkan_common/vulkan_device.h"
#include "video_core/host_shaders/rdna2_nr_decoder_fp32_to_fp16_comp_spv.h"
namespace Vulkan {
namespace NR=Eden::Graphics::NeuralRDNA2;
namespace {
static_assert(sizeof(NR::DecoderFp32ToFp16)==3u*sizeof(u32));
constexpr VkMemoryBarrier kVisible{
    .sType=VK_STRUCTURE_TYPE_MEMORY_BARRIER,.pNext=nullptr,
    .srcAccessMask=VK_ACCESS_SHADER_WRITE_BIT,
    .dstAccessMask=VK_ACCESS_SHADER_READ_BIT|VK_ACCESS_SHADER_WRITE_BIT};
}
NrRdna2DecoderFp32ToFp16::NrRdna2DecoderFp32ToFp16(
    const Device& d,Scheduler& s):scheduler{s}{
    descriptor_layout=CreateWrappedDescriptorSetLayout(
        d,{VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
           VK_DESCRIPTOR_TYPE_STORAGE_BUFFER},
        VK_SHADER_STAGE_COMPUTE_BIT);
    constexpr VkPushConstantRange range{
        .stageFlags=VK_SHADER_STAGE_COMPUTE_BIT,
        .offset=0,.size=sizeof(NR::DecoderFp32ToFp16)};
    layout=d.GetLogical().CreatePipelineLayout({
        .sType=VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
        .pNext=nullptr,.flags=0,.setLayoutCount=1,
        .pSetLayouts=descriptor_layout.address(),
        .pushConstantRangeCount=1,.pPushConstantRanges=&range});
    shader=CreateWrappedShaderModule(
        d,RDNA2_NR_DECODER_FP32_TO_FP16_COMP_SPV);
    pipeline=CreateWrappedComputePipeline(d,layout,*shader);
}
NrRdna2DecoderFp32ToFp16::~NrRdna2DecoderFp32ToFp16()=default;
bool NrRdna2DecoderFp32ToFp16::Record(
    VkDescriptorSet descriptor,
    VkDescriptorBufferInfo source,VkDescriptorBufferInfo destination,
    const NR::DecoderFp32ToFp16& request){
    if(!descriptor||!source.buffer||!destination.buffer||
       source.offset||destination.offset||
       !source.range||!destination.range||
       source.range==VK_WHOLE_SIZE||destination.range==VK_WHOLE_SIZE||
       source.buffer==destination.buffer||
       !NR::CanDecoderFp32ToFp16(request,{
           source.range,destination.range}))return false;
    const auto native=*pipeline,layout_native=*layout;
    const u32 groups=(request.elements+255u)/256u;
    scheduler.RequestOutsideRenderPassOperationContext();
    scheduler.Record([descriptor,request,native,layout_native,groups](
        vk::CommandBuffer cmd){
        cmd.BindPipeline(VK_PIPELINE_BIND_POINT_COMPUTE,native);
        cmd.BindDescriptorSets(VK_PIPELINE_BIND_POINT_COMPUTE,
                               layout_native,0,descriptor,{});
        cmd.PushConstants(layout_native,VK_SHADER_STAGE_COMPUTE_BIT,
                          0,sizeof(request),&request);
        cmd.Dispatch(groups,1u,1u);
        cmd.PipelineBarrier(VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                            0,kVisible);
    });
    return true;
}
} // namespace Vulkan
