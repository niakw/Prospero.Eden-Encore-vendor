// SPDX-License-Identifier: MIT
#include "video_core/renderer_vulkan/nr_rdna2_split_context_repack.h"
#include "video_core/renderer_vulkan/present/util.h"
#include "video_core/renderer_vulkan/vk_scheduler.h"
#include "video_core/vulkan_common/vulkan_device.h"
#include "video_core/host_shaders/rdna2_nr_split_context_repack_comp_spv.h"
#include <cstdint>
namespace Vulkan {
namespace NR=Eden::Graphics::NeuralRDNA2;
namespace {
static_assert(sizeof(NR::SplitContextRepack)==4u*sizeof(u32));
constexpr VkMemoryBarrier kVisible{
    .sType=VK_STRUCTURE_TYPE_MEMORY_BARRIER,.pNext=nullptr,
    .srcAccessMask=VK_ACCESS_SHADER_WRITE_BIT,
    .dstAccessMask=VK_ACCESS_SHADER_READ_BIT|VK_ACCESS_SHADER_WRITE_BIT};
}
NrRdna2SplitContextRepack::NrRdna2SplitContextRepack(
    const Device& d,Scheduler& s):scheduler{s}{
    descriptor_layout=CreateWrappedDescriptorSetLayout(
        d,{VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
           VK_DESCRIPTOR_TYPE_STORAGE_BUFFER},VK_SHADER_STAGE_COMPUTE_BIT);
    constexpr VkPushConstantRange push{
        .stageFlags=VK_SHADER_STAGE_COMPUTE_BIT,.offset=0,
        .size=sizeof(NR::SplitContextRepack)};
    layout=d.GetLogical().CreatePipelineLayout({
        .sType=VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
        .pNext=nullptr,.flags=0,.setLayoutCount=1,
        .pSetLayouts=descriptor_layout.address(),
        .pushConstantRangeCount=1,.pPushConstantRanges=&push});
    shader=CreateWrappedShaderModule(
        d,RDNA2_NR_SPLIT_CONTEXT_REPACK_COMP_SPV);
    pipeline=CreateWrappedComputePipeline(d,layout,*shader);
}
NrRdna2SplitContextRepack::~NrRdna2SplitContextRepack()=default;
bool NrRdna2SplitContextRepack::Record(
    VkDescriptorSet set,VkDescriptorBufferInfo input,
    VkDescriptorBufferInfo output,const NR::SplitContextRepack& request){
    if(!set||!input.buffer||!output.buffer||
       input.buffer==output.buffer||input.offset||output.offset||
       input.range==VK_WHOLE_SIZE||output.range==VK_WHOLE_SIZE||
       !NR::CanSplitContextRepack(
           request,{input.range,output.range}))return false;
    const auto native=*pipeline;
    const auto pipe_layout=*layout;
    const auto groups=NR::SplitContextRepackGridX(request);
    scheduler.RequestOutsideRenderPassOperationContext();
    scheduler.Record([set,request,native,pipe_layout,groups](
        vk::CommandBuffer cmd){
        cmd.BindPipeline(VK_PIPELINE_BIND_POINT_COMPUTE,native);
        cmd.BindDescriptorSets(VK_PIPELINE_BIND_POINT_COMPUTE,
                               pipe_layout,0,set,{});
        cmd.PushConstants(pipe_layout,VK_SHADER_STAGE_COMPUTE_BIT,
                          0,sizeof(request),&request);
        cmd.Dispatch(groups,1u,1u);
        cmd.PipelineBarrier(VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,0,kVisible);
    });
    return true;
}
} // namespace Vulkan
