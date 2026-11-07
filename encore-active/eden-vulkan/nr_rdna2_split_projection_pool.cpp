// SPDX-License-Identifier: MIT
#include "video_core/renderer_vulkan/nr_rdna2_split_projection_pool.h"
#include "video_core/renderer_vulkan/present/util.h"
#include "video_core/renderer_vulkan/vk_scheduler.h"
#include "video_core/vulkan_common/vulkan_device.h"
#include "video_core/host_shaders/rdna2_nr_split_projection_pool_comp_spv.h"
#include <array>
#include <cstdint>
namespace Vulkan {
namespace NR=Eden::Graphics::NeuralRDNA2;
namespace {
static_assert(sizeof(NR::SplitProjectionPool)==9u*sizeof(u32));
constexpr VkMemoryBarrier kVisible{
    .sType=VK_STRUCTURE_TYPE_MEMORY_BARRIER,.pNext=nullptr,
    .srcAccessMask=VK_ACCESS_SHADER_WRITE_BIT,
    .dstAccessMask=VK_ACCESS_SHADER_READ_BIT|VK_ACCESS_SHADER_WRITE_BIT};
}
NrRdna2SplitProjectionPool::NrRdna2SplitProjectionPool(
    const Device& d,Scheduler& s):scheduler{s}{
    descriptor_layout=CreateWrappedDescriptorSetLayout(
        d,{VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
           VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,VK_DESCRIPTOR_TYPE_STORAGE_BUFFER},
        VK_SHADER_STAGE_COMPUTE_BIT);
    constexpr VkPushConstantRange push{
        .stageFlags=VK_SHADER_STAGE_COMPUTE_BIT,.offset=0,
        .size=sizeof(NR::SplitProjectionPool)};
    layout=d.GetLogical().CreatePipelineLayout({
        .sType=VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
        .pNext=nullptr,.flags=0,.setLayoutCount=1,
        .pSetLayouts=descriptor_layout.address(),
        .pushConstantRangeCount=1,.pPushConstantRanges=&push});
    shader=CreateWrappedShaderModule(
        d,RDNA2_NR_SPLIT_PROJECTION_POOL_COMP_SPV);
    pipeline=CreateWrappedComputePipeline(d,layout,*shader);
}
NrRdna2SplitProjectionPool::~NrRdna2SplitProjectionPool()=default;
bool NrRdna2SplitProjectionPool::Record(
    VkDescriptorSet set,VkDescriptorBufferInfo scratch,
    VkDescriptorBufferInfo residual,VkDescriptorBufferInfo gains,
    VkDescriptorBufferInfo output,
    const NR::SplitProjectionPool& request){
    const std::array<VkDescriptorBufferInfo,4> infos{
        scratch,residual,gains,output};
    if(!set)return false;
    for(std::size_t i=0u;i<infos.size();++i){
        const auto& info=infos[i];
        if(!info.buffer||info.offset||!info.range||
           info.range==VK_WHOLE_SIZE)return false;
        for(std::size_t j=0u;j<i;++j)
            if(info.buffer==infos[j].buffer)return false;
    }
    if(!NR::CanSplitProjectionPool(request,{
        scratch.range,residual.range,gains.range,output.range}))
        return false;
    const VkPipeline native = *pipeline;
    const VkPipelineLayout pipe_layout = *layout;
    const auto tokens=NR::SplitPoolOutputTokens(request);
    scheduler.RequestOutsideRenderPassOperationContext();
    scheduler.Record([set,request,native,pipe_layout,tokens](
        vk::CommandBuffer cmd){
        cmd.BindPipeline(VK_PIPELINE_BIND_POINT_COMPUTE,native);
        cmd.BindDescriptorSets(VK_PIPELINE_BIND_POINT_COMPUTE,
                               pipe_layout,0,set,{});
        cmd.PushConstants(pipe_layout,VK_SHADER_STAGE_COMPUTE_BIT,
                          0,sizeof(request),&request);
        cmd.Dispatch(tokens,1u,1u);
        cmd.PipelineBarrier(VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,0,kVisible);
    });
    return true;
}
} // namespace Vulkan
