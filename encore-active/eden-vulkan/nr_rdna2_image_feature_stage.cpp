// SPDX-License-Identifier: MIT
#include "video_core/renderer_vulkan/nr_rdna2_image_feature_stage.h"
#include "video_core/renderer_vulkan/present/util.h"
#include "video_core/renderer_vulkan/vk_scheduler.h"
#include "video_core/vulkan_common/vulkan_device.h"
#include "video_core/vulkan_common/vulkan_memory_allocator.h"
#include "video_core/host_shaders/rdna2_nr_image_feature_comp_spv.h"
#include <array>
#include <cstdint>
#include <memory>
#include <utility>
namespace Vulkan {
namespace NR=Eden::Graphics::NeuralRDNA2;
namespace {
static_assert(sizeof(NR::ImageFeatureShape)==3u*sizeof(u32));
constexpr VkMemoryBarrier kVisible{
    .sType=VK_STRUCTURE_TYPE_MEMORY_BARRIER,.pNext=nullptr,
    .srcAccessMask=VK_ACCESS_SHADER_WRITE_BIT,
    .dstAccessMask=VK_ACCESS_SHADER_READ_BIT|VK_ACCESS_SHADER_WRITE_BIT};
}
NrRdna2ImageFeatureStage::NrRdna2ImageFeatureStage(
    const Device& d,MemoryAllocator& a,Scheduler& s)
    :device{d},allocator{a},scheduler{s}{
    descriptor_layout=CreateWrappedDescriptorSetLayout(
        d,{VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
           VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
           VK_DESCRIPTOR_TYPE_STORAGE_BUFFER},
        VK_SHADER_STAGE_COMPUTE_BIT);
    constexpr VkPushConstantRange push{
        .stageFlags=VK_SHADER_STAGE_COMPUTE_BIT,
        .offset=0,.size=sizeof(NR::ImageFeatureShape)};
    layout=d.GetLogical().CreatePipelineLayout({
        .sType=VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
        .pNext=nullptr,.flags=0,.setLayoutCount=1,
        .pSetLayouts=descriptor_layout.address(),
        .pushConstantRangeCount=1,.pPushConstantRanges=&push});
    shader=CreateWrappedShaderModule(
        d,RDNA2_NR_IMAGE_FEATURE_COMP_SPV);
    pipeline=CreateWrappedComputePipeline(d,layout,*shader);
}
NrRdna2ImageFeatureStage::~NrRdna2ImageFeatureStage()=default;
bool NrRdna2ImageFeatureStage::InitializeWithModel(
    const NR::NeuralModelFile& model,
    NR::ImageFeatureDirection direction,std::uint32_t post_block,
    const NR::ImageFeatureShape& requested,
    VkDescriptorBufferInfo input,VkDescriptorBufferInfo target){
    if(ready||weights||requested.direction!=
         static_cast<std::uint32_t>(direction)||
       !model.IsOpen()||!input.buffer||!target.buffer||
       input.buffer==target.buffer||input.offset||target.offset||
       !input.range||!target.range||
       input.range==VK_WHOLE_SIZE||target.range==VK_WHOLE_SIZE||
       input.range>device.GetMaxStorageBufferRange()||
       target.range>device.GetMaxStorageBufferRange()||
       !NR::CanImageFeatureStage(
           requested,{input.range,1024u,target.range}))return false;
    std::vector<std::uint8_t> raw;
    if(!NR::PrepareImageFeatureWeights(
           model,direction,post_block,raw))return false;
    auto owned=std::make_unique<NrRdna2GpuWeights>(device,allocator);
    if(!owned->Initialize(raw)||owned->Size()!=1024u)return false;
    auto p=CreateWrappedDescriptorPool(
        device,3u,1u,{VK_DESCRIPTOR_TYPE_STORAGE_BUFFER});
    std::array<VkDescriptorSetLayout,1> layouts{*descriptor_layout};
    auto assigned=CreateWrappedDescriptorSets(
        p,vk::Span<VkDescriptorSetLayout>{
            layouts.data(),layouts.size()});
    if(assigned.IsOutOfPoolMemory()||assigned.size()!=1u)return false;
    const std::array<VkDescriptorBufferInfo,3> infos{
        input,owned->Descriptor(),target};
    std::array<VkWriteDescriptorSet,3> writes{};
    for(u32 i=0u;i<3u;++i){
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
    weights=std::move(owned);
    pool=std::move(p);
    sets=std::move(assigned);
    shape=requested;
    capacity={input.range,weights->Size(),target.range};
    source=input;
    output=target;
    ready=true;
    return true;
}
bool NrRdna2ImageFeatureStage::Record(){
    if(!ready||sets.size()!=1u||
       !NR::CanImageFeatureStage(shape,capacity))return false;
    const VkPipeline native=*pipeline;
    const VkPipelineLayout native_layout=*layout;
    const VkDescriptorSet set=sets[0];
    const auto pc=shape;
    const auto gx=(shape.width+63u)/64u;
    scheduler.RequestOutsideRenderPassOperationContext();
    scheduler.Record([native,native_layout,set,pc,gx](
        vk::CommandBuffer cmd){
        cmd.BindPipeline(VK_PIPELINE_BIND_POINT_COMPUTE,native);
        cmd.BindDescriptorSets(VK_PIPELINE_BIND_POINT_COMPUTE,
                               native_layout,0,set,{});
        cmd.PushConstants(native_layout,VK_SHADER_STAGE_COMPUTE_BIT,
                          0,sizeof(pc),&pc);
        cmd.Dispatch(gx,pc.height,1u);
        cmd.PipelineBarrier(VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                            0,kVisible);
    });
    return true;
}
} // namespace Vulkan
