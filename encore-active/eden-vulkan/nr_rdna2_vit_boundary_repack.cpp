// SPDX-License-Identifier: MIT
#include "video_core/renderer_vulkan/nr_rdna2_vit_boundary_repack.h"
#include "video_core/renderer_vulkan/present/util.h"
#include "video_core/renderer_vulkan/vk_scheduler.h"
#include "video_core/vulkan_common/vulkan_device.h"
#include "video_core/host_shaders/rdna2_nr_vit_boundary_repack_comp_spv.h"
#include <cstdint>
namespace Vulkan {
namespace NR=Eden::Graphics::NeuralRDNA2;
namespace {
static_assert(sizeof(NR::VitBoundaryRepack)==7u*sizeof(u32));
constexpr VkMemoryBarrier kVisible{
    .sType=VK_STRUCTURE_TYPE_MEMORY_BARRIER,.pNext=nullptr,
    .srcAccessMask=VK_ACCESS_SHADER_WRITE_BIT,
    .dstAccessMask=VK_ACCESS_SHADER_READ_BIT|VK_ACCESS_SHADER_WRITE_BIT};
}
NrRdna2VitBoundaryRepack::NrRdna2VitBoundaryRepack(
    const Device& d,Scheduler& s):scheduler{s}{
    descriptor_layout=CreateWrappedDescriptorSetLayout(
        d,{VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
           VK_DESCRIPTOR_TYPE_STORAGE_BUFFER},VK_SHADER_STAGE_COMPUTE_BIT);
    constexpr VkPushConstantRange range{
        .stageFlags=VK_SHADER_STAGE_COMPUTE_BIT,.offset=0,
        .size=sizeof(NR::VitBoundaryRepack)};
    layout=d.GetLogical().CreatePipelineLayout({
        .sType=VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
        .pNext=nullptr,.flags=0,.setLayoutCount=1,
        .pSetLayouts=descriptor_layout.address(),
        .pushConstantRangeCount=1,.pPushConstantRanges=&range});
    shader=CreateWrappedShaderModule(
        d,RDNA2_NR_VIT_BOUNDARY_REPACK_COMP_SPV);
    pipeline=CreateWrappedComputePipeline(d,layout,*shader);
}
NrRdna2VitBoundaryRepack::~NrRdna2VitBoundaryRepack()=default;
bool NrRdna2VitBoundaryRepack::Record(
    VkDescriptorSet set,VkDescriptorBufferInfo input,
    VkDescriptorBufferInfo output,
    const NR::VitBoundaryRepack& shape){
    if(!set||!input.buffer||!output.buffer||
       input.buffer==output.buffer||input.offset||output.offset||
       input.range==VK_WHOLE_SIZE||output.range==VK_WHOLE_SIZE||
       !NR::CanVitBoundaryRepack(
          shape,{input.range,output.range}))return false;
    const auto native=*pipeline;
    const auto native_layout=*layout;
    const auto groups=NR::VitBoundaryGrid(shape);
    scheduler.RequestOutsideRenderPassOperationContext();
    scheduler.Record([set,shape,native,native_layout,groups](
        vk::CommandBuffer cmd){
        cmd.BindPipeline(VK_PIPELINE_BIND_POINT_COMPUTE,native);
        cmd.BindDescriptorSets(VK_PIPELINE_BIND_POINT_COMPUTE,
                               native_layout,0,set,{});
        cmd.PushConstants(native_layout,VK_SHADER_STAGE_COMPUTE_BIT,
                          0,sizeof(shape),&shape);
        cmd.Dispatch(groups,1u,1u);
        cmd.PipelineBarrier(VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,0,kVisible);
    });
    return true;
}
}
