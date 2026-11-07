// SPDX-License-Identifier: MIT
#include "video_core/renderer_vulkan/nr_rdna2_proxy_feature_stage.h"
#include "video_core/renderer_vulkan/present/util.h"
#include "video_core/renderer_vulkan/vk_scheduler.h"
#include "video_core/vulkan_common/vulkan_device.h"
#include "video_core/host_shaders/rdna2_nr_proxy_features_comp_spv.h"
#include <array>
#include <cstdint>
#include <utility>
namespace Vulkan {
namespace NR=Eden::Graphics::NeuralRDNA2;
namespace {
static_assert(sizeof(NR::NeuralProxyFeatures)==12u*sizeof(u32));
constexpr VkMemoryBarrier kVisible{
    .sType=VK_STRUCTURE_TYPE_MEMORY_BARRIER,.pNext=nullptr,
    .srcAccessMask=VK_ACCESS_SHADER_WRITE_BIT,
    .dstAccessMask=VK_ACCESS_SHADER_READ_BIT|VK_ACCESS_SHADER_WRITE_BIT};
}
NrRdna2ProxyFeatureStage::NrRdna2ProxyFeatureStage(
    const Device& d,Scheduler& s):device{d},scheduler{s}{
    descriptor_layout=CreateWrappedDescriptorSetLayout(
        d,{VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
           VK_DESCRIPTOR_TYPE_STORAGE_BUFFER},
        VK_SHADER_STAGE_COMPUTE_BIT);
    constexpr VkPushConstantRange push{
        .stageFlags=VK_SHADER_STAGE_COMPUTE_BIT,
        .offset=0,.size=sizeof(NR::NeuralProxyFeatures)};
    layout=d.GetLogical().CreatePipelineLayout({
        .sType=VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
        .pNext=nullptr,.flags=0,.setLayoutCount=1,
        .pSetLayouts=descriptor_layout.address(),
        .pushConstantRangeCount=1,.pPushConstantRanges=&push});
    shader=CreateWrappedShaderModule(
        d,RDNA2_NR_PROXY_FEATURES_COMP_SPV);
    pipeline=CreateWrappedComputePipeline(d,layout,*shader);
}
NrRdna2ProxyFeatureStage::~NrRdna2ProxyFeatureStage()=default;
bool NrRdna2ProxyFeatureStage::Initialize(
    const NR::NeuralProxyFeatures& shape,
    VkDescriptorBufferInfo input,VkDescriptorBufferInfo output){
    if(ready||!input.buffer||!output.buffer||
       input.buffer==output.buffer||
       input.offset||output.offset||
       !input.range||!output.range||
       input.range==VK_WHOLE_SIZE||output.range==VK_WHOLE_SIZE||
       input.range>device.GetMaxStorageBufferRange()||
       output.range>device.GetMaxStorageBufferRange()||
       !NR::CanNeuralProxyFeatures(shape,{
           input.range,output.range}))return false;
    auto proposed_pool=CreateWrappedDescriptorPool(
        device,2u,1u,{VK_DESCRIPTOR_TYPE_STORAGE_BUFFER});
    std::array<VkDescriptorSetLayout,1> layouts{*descriptor_layout};
    auto assigned=CreateWrappedDescriptorSets(
        proposed_pool,vk::Span<VkDescriptorSetLayout>{
            layouts.data(),layouts.size()});
    if(assigned.IsOutOfPoolMemory()||assigned.size()!=1u)return false;
    const std::array<VkDescriptorBufferInfo,2> infos{input,output};
    std::array<VkWriteDescriptorSet,2> writes{};
    for(u32 i=0u;i<2u;++i){
        writes[i]=VkWriteDescriptorSet{
            .sType=VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
            .pNext=nullptr,.dstSet=assigned[0],.dstBinding=i,
            .dstArrayElement=0u,.descriptorCount=1u,
            .descriptorType=VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
            .pImageInfo=nullptr,.pBufferInfo=&infos[i],
            .pTexelBufferView=nullptr};
    }
    device.GetLogical().UpdateDescriptorSets(
        vk::Span<VkWriteDescriptorSet>{
            writes.data(),writes.size()},{});
    pool=std::move(proposed_pool);
    sets=std::move(assigned);
    params=shape;
    capacity={input.range,output.range};
    source=input;
    result=output;
    ready=true;
    return true;
}
bool NrRdna2ProxyFeatureStage::Record(){
    if(!ready||sets.size()!=1u||
       !NR::CanNeuralProxyFeatures(params,capacity))return false;
    const VkPipeline native=*pipeline;
    const VkPipelineLayout native_layout=*layout;
    const VkDescriptorSet descriptor=sets[0];
    const auto shape=params;
    const u32 gx=(shape.full_width+7u)/8u;
    const u32 gy=(shape.full_height+7u)/8u;
    scheduler.RequestOutsideRenderPassOperationContext();
    scheduler.Record([native,native_layout,descriptor,shape,gx,gy](
        vk::CommandBuffer cmd){
        cmd.BindPipeline(VK_PIPELINE_BIND_POINT_COMPUTE,native);
        cmd.BindDescriptorSets(VK_PIPELINE_BIND_POINT_COMPUTE,
                               native_layout,0,descriptor,{});
        cmd.PushConstants(native_layout,VK_SHADER_STAGE_COMPUTE_BIT,
                          0,sizeof(shape),&shape);
        cmd.Dispatch(gx,gy,1u);
        cmd.PipelineBarrier(VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                            0,kVisible);
    });
    return true;
}
} // namespace Vulkan
