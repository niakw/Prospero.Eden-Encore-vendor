// SPDX-License-Identifier: MIT
#include "video_core/renderer_vulkan/nr_rdna2_split_to_vit_bridge.h"
#include "video_core/renderer_vulkan/present/util.h"
#include "video_core/renderer_vulkan/vk_scheduler.h"
#include "video_core/vulkan_common/vulkan_device.h"
#include "video_core/host_shaders/rdna2_nr_split_to_vit_raster_comp_spv.h"
#include <array>
#include <cstdint>
#include <utility>
namespace Vulkan {
namespace NR=Eden::Graphics::NeuralRDNA2;
namespace {
static_assert(sizeof(NR::SplitToVitRaster)==4u*sizeof(u32));
constexpr VkMemoryBarrier kVisible{
    .sType=VK_STRUCTURE_TYPE_MEMORY_BARRIER,.pNext=nullptr,
    .srcAccessMask=VK_ACCESS_SHADER_WRITE_BIT,
    .dstAccessMask=VK_ACCESS_SHADER_READ_BIT|VK_ACCESS_SHADER_WRITE_BIT};
}
NrRdna2SplitToVitBridge::NrRdna2SplitToVitBridge(
    const Device& d,Scheduler& s):device{d},scheduler{s}{
    layout=CreateWrappedDescriptorSetLayout(
        d,{VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
           VK_DESCRIPTOR_TYPE_STORAGE_BUFFER},VK_SHADER_STAGE_COMPUTE_BIT);
    constexpr VkPushConstantRange push{
        .stageFlags=VK_SHADER_STAGE_COMPUTE_BIT,.offset=0,
        .size=sizeof(NR::SplitToVitRaster)};
    pipeline_layout=d.GetLogical().CreatePipelineLayout({
        .sType=VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
        .pNext=nullptr,.flags=0,.setLayoutCount=1,
        .pSetLayouts=layout.address(),
        .pushConstantRangeCount=1,.pPushConstantRanges=&push});
    shader=CreateWrappedShaderModule(
        d,RDNA2_NR_SPLIT_TO_VIT_RASTER_COMP_SPV);
    pipeline=CreateWrappedComputePipeline(d,pipeline_layout,*shader);
}
NrRdna2SplitToVitBridge::~NrRdna2SplitToVitBridge()=default;
bool NrRdna2SplitToVitBridge::Initialize(
    std::uint32_t width,std::uint32_t height,
    VkDescriptorBufferInfo input,VkDescriptorBufferInfo output){
    if(ready||!input.buffer||!output.buffer||
       input.buffer==output.buffer||input.offset||output.offset||
       !input.range||!output.range||
       input.range==VK_WHOLE_SIZE||output.range==VK_WHOLE_SIZE||
       input.range>device.GetMaxStorageBufferRange()||
       output.range>device.GetMaxStorageBufferRange())return false;
    const NR::SplitToVitRaster next_shape{width,height,0u,0u};
    const NR::SplitToVitRasterStorage next_storage{
        input.range,output.range};
    if(!NR::CanSplitToVitRaster(next_shape,next_storage))return false;
    auto new_pool=CreateWrappedDescriptorPool(
        device,2u,1u,{VK_DESCRIPTOR_TYPE_STORAGE_BUFFER});
    std::array<VkDescriptorSetLayout,1> layouts{*layout};
    auto allocated=CreateWrappedDescriptorSets(
        new_pool,vk::Span<VkDescriptorSetLayout>{
            layouts.data(),layouts.size()});
    if(allocated.IsOutOfPoolMemory()||allocated.size()!=1u)return false;
    const std::array<VkDescriptorBufferInfo,2> buffers{input,output};
    std::array<VkWriteDescriptorSet,2> writes{};
    for(std::uint32_t i=0u;i<2u;++i)
        writes[i]=VkWriteDescriptorSet{
            .sType=VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,.pNext=nullptr,
            .dstSet=allocated[0],.dstBinding=i,.dstArrayElement=0u,
            .descriptorCount=1u,.descriptorType=VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
            .pImageInfo=nullptr,.pBufferInfo=&buffers[i],
            .pTexelBufferView=nullptr};
    device.GetLogical().UpdateDescriptorSets(
        vk::Span<VkWriteDescriptorSet>{writes.data(),writes.size()},{});
    pool=std::move(new_pool);sets=std::move(allocated);
    shape=next_shape;storage=next_storage;ready=true;
    return true;
}
bool NrRdna2SplitToVitBridge::Record(){
    if(!ready||sets.size()!=1u||
       !NR::CanSplitToVitRaster(shape,storage))return false;
    const auto native=*pipeline,pipe_layout=*pipeline_layout;
    const auto set=sets[0],request=shape;
    const auto groups=NR::SplitToVitRasterTokens(request);
    scheduler.RequestOutsideRenderPassOperationContext();
    scheduler.Record([native,pipe_layout,set,request,groups](
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
