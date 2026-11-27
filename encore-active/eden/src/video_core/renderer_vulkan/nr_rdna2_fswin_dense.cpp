// SPDX-License-Identifier: MIT
#include "video_core/renderer_vulkan/nr_rdna2_fswin_dense.h"
#include "video_core/renderer_vulkan/present/util.h"
#include "video_core/renderer_vulkan/vk_scheduler.h"
#include "video_core/vulkan_common/vulkan_device.h"
#include "video_core/host_shaders/rdna2_nr_fswin_dense_comp_spv.h"
namespace Vulkan {
namespace {
using Eden::Graphics::NeuralRDNA2::FSwinDense;
using Eden::Graphics::NeuralRDNA2::FSwinDenseStorage;
static_assert(sizeof(FSwinDense)==sizeof(u32)*8);
constexpr VkMemoryBarrier kVisible{
    .sType=VK_STRUCTURE_TYPE_MEMORY_BARRIER,
    .pNext=nullptr,
    .srcAccessMask=VK_ACCESS_SHADER_WRITE_BIT,
    .dstAccessMask=VK_ACCESS_SHADER_READ_BIT|VK_ACCESS_SHADER_WRITE_BIT,
};
}
NrRdna2FSwinDense::NrRdna2FSwinDense(
    const Device& device,Scheduler& scheduler_):scheduler{scheduler_}{
    descriptor_layout=CreateWrappedDescriptorSetLayout(
        device,{VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
                VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
                VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
                VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
                VK_DESCRIPTOR_TYPE_STORAGE_BUFFER},VK_SHADER_STAGE_COMPUTE_BIT);
    constexpr VkPushConstantRange range{
        .stageFlags=VK_SHADER_STAGE_COMPUTE_BIT,
        .offset=0,.size=sizeof(FSwinDense),
    };
    layout=device.GetLogical().CreatePipelineLayout({
        .sType=VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
        .pNext=nullptr,.flags=0,.setLayoutCount=1,
        .pSetLayouts=descriptor_layout.address(),
        .pushConstantRangeCount=1,
        .pPushConstantRanges=&range,
    });
    shader=CreateWrappedShaderModule(device,RDNA2_NR_FSWIN_DENSE_COMP_SPV);
    pipeline=CreateWrappedComputePipeline(device,layout,*shader);
}
NrRdna2FSwinDense::~NrRdna2FSwinDense()=default;
bool NrRdna2FSwinDense::Record(
    VkDescriptorSet validated_set,const FSwinDense& request,
    const FSwinDenseStorage& storage){
    if(!validated_set||
       !Eden::Graphics::NeuralRDNA2::CanFSwinDense(request,storage))
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
                          0,sizeof(FSwinDense),&request);
        cmd.Dispatch(Eden::Graphics::NeuralRDNA2::FSwinDenseGridX(request),
                     64,request.windows);
        cmd.PipelineBarrier(VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,0,kVisible);
    });
    return true;
}
bool NrRdna2FSwinDense::RecordRawC32(
    VkDescriptorSet validated_set,const FSwinDense& final_dense,
    const FSwinDenseStorage& raw_storage){
    if(!validated_set||final_dense.stage!=3u||
       final_dense.channels!=32u||
       !Eden::Graphics::NeuralRDNA2::CanFSwinDense(
           final_dense,raw_storage)||
       std::uint64_t(final_dense.windows)*64u*64u>
           raw_storage.output_bytes)return false;
    auto request=final_dense;
    request.stage=4u; // shader-only raw FP16 epilogue
    const VkPipeline native_pipeline=*pipeline;
    const VkPipelineLayout native_layout=*layout;
    scheduler.RequestOutsideRenderPassOperationContext();
    const auto grid=Eden::Graphics::NeuralRDNA2::FSwinDenseGridX(final_dense);
    scheduler.Record([validated_set,native_pipeline,native_layout,
                      request,grid](vk::CommandBuffer cmd){
        cmd.BindPipeline(VK_PIPELINE_BIND_POINT_COMPUTE,native_pipeline);
        cmd.BindDescriptorSets(VK_PIPELINE_BIND_POINT_COMPUTE,native_layout,
                               0,validated_set,{});
        cmd.PushConstants(native_layout,VK_SHADER_STAGE_COMPUTE_BIT,
                          0,sizeof(FSwinDense),&request);
        cmd.Dispatch(grid,64,request.windows);
        cmd.PipelineBarrier(VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,0,kVisible);
    });
    return true;
}
} // namespace Vulkan

// The raw output pass shares the ordinary shader ABI and validated dense-3
// weights, but writes two FP16 bytes per channel rather than one FP8 byte.
