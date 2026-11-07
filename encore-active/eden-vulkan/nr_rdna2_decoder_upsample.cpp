// SPDX-License-Identifier: MIT
#include "video_core/renderer_vulkan/nr_rdna2_decoder_upsample.h"
#include "video_core/renderer_vulkan/present/util.h"
#include "video_core/renderer_vulkan/vk_scheduler.h"
#include "video_core/vulkan_common/vulkan_device.h"
#include "video_core/host_shaders/rdna2_nr_decoder_upsample_comp_spv.h"
namespace Vulkan {
namespace {
using Eden::Graphics::NeuralRDNA2::DecoderUpsample;
using Eden::Graphics::NeuralRDNA2::DecoderStorage;
static_assert(sizeof(DecoderUpsample)==sizeof(u32)*7);
constexpr VkMemoryBarrier kVisibility{
    .sType=VK_STRUCTURE_TYPE_MEMORY_BARRIER,
    .pNext=nullptr,
    .srcAccessMask=VK_ACCESS_SHADER_WRITE_BIT,
    .dstAccessMask=VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT,
};
}
NrRdna2DecoderUpsample::NrRdna2DecoderUpsample(
    const Device& device,Scheduler& scheduler_):scheduler{scheduler_}{
    descriptor_layout=CreateWrappedDescriptorSetLayout(
        device,{VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
                VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
                VK_DESCRIPTOR_TYPE_STORAGE_BUFFER},VK_SHADER_STAGE_COMPUTE_BIT);
    constexpr VkPushConstantRange range{
        .stageFlags=VK_SHADER_STAGE_COMPUTE_BIT,
        .offset=0,.size=sizeof(DecoderUpsample),
    };
    layout=device.GetLogical().CreatePipelineLayout({
        .sType=VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
        .pNext=nullptr,.flags=0,.setLayoutCount=1,
        .pSetLayouts=descriptor_layout.address(),
        .pushConstantRangeCount=1,.pPushConstantRanges=&range,
    });
    shader=CreateWrappedShaderModule(device,RDNA2_NR_DECODER_UPSAMPLE_COMP_SPV);
    pipeline=CreateWrappedComputePipeline(device,layout,*shader);
}
NrRdna2DecoderUpsample::~NrRdna2DecoderUpsample()=default;
bool NrRdna2DecoderUpsample::Record(
    VkDescriptorSet validated_set,const DecoderUpsample& request,
    const DecoderStorage& storage) {
    if(!validated_set ||
       !Eden::Graphics::NeuralRDNA2::CanDecoderUpsample(request,storage))return false;
    const VkPipeline native_pipeline=*pipeline;
    const VkPipelineLayout native_layout=*layout;
    // Launch over the padded 4x4 token grid so border tokens are cleared.
    // The host policy bounds the entire read/write domain before recording.
    const u32 group_x=static_cast<u32>(
        (Eden::Graphics::NeuralRDNA2::DecoderOutputTileTokens(request)+63u)/64u);
    scheduler.RequestOutsideRenderPassOperationContext();
    scheduler.Record([validated_set,native_pipeline,native_layout,
                      group_x,request](vk::CommandBuffer cmd){
        cmd.BindPipeline(VK_PIPELINE_BIND_POINT_COMPUTE,native_pipeline);
        cmd.BindDescriptorSets(VK_PIPELINE_BIND_POINT_COMPUTE,native_layout,
                               0,validated_set,{});
        cmd.PushConstants(native_layout,VK_SHADER_STAGE_COMPUTE_BIT,
                          0,sizeof(DecoderUpsample),&request);
        cmd.Dispatch(group_x,128,1);
        cmd.PipelineBarrier(VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,0,kVisibility);
    });
    return true;
}
} // namespace Vulkan
