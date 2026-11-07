// SPDX-License-Identifier: MIT
#include "video_core/renderer_vulkan/nr_rdna2_preblock_pool.h"
#include "video_core/renderer_vulkan/present/util.h"
#include "video_core/renderer_vulkan/vk_scheduler.h"
#include "video_core/vulkan_common/vulkan_device.h"
#include "video_core/host_shaders/rdna2_nr_preblock_pool_comp_spv.h"
#include <array>
#include <cstdint>
#include <utility>
namespace Vulkan {
namespace NR=Eden::Graphics::NeuralRDNA2;
namespace {
static_assert(sizeof(NR::PreblockPoolShape)==2u*sizeof(std::uint32_t));
constexpr VkMemoryBarrier kVisible{
    .sType=VK_STRUCTURE_TYPE_MEMORY_BARRIER,.pNext=nullptr,
    .srcAccessMask=VK_ACCESS_SHADER_WRITE_BIT,
    .dstAccessMask=VK_ACCESS_SHADER_READ_BIT|VK_ACCESS_SHADER_WRITE_BIT};
}
NrRdna2PreblockPool::NrRdna2PreblockPool(
    const Device& d,Scheduler& s):device{d},scheduler{s}{
    descriptor_layout=CreateWrappedDescriptorSetLayout(
        d,{VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,VK_DESCRIPTOR_TYPE_STORAGE_BUFFER},
        VK_SHADER_STAGE_COMPUTE_BIT);
    constexpr VkPushConstantRange pc{
        .stageFlags=VK_SHADER_STAGE_COMPUTE_BIT,.offset=0,
        .size=sizeof(NR::PreblockPoolShape)};
    layout=d.GetLogical().CreatePipelineLayout({
        .sType=VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
        .pNext=nullptr,.flags=0,.setLayoutCount=1,
        .pSetLayouts=descriptor_layout.address(),
        .pushConstantRangeCount=1,.pPushConstantRanges=&pc});
    shader=CreateWrappedShaderModule(d,RDNA2_NR_PREBLOCK_POOL_COMP_SPV);
    pipeline=CreateWrappedComputePipeline(d,layout,*shader);
}
NrRdna2PreblockPool::~NrRdna2PreblockPool()=default;
bool NrRdna2PreblockPool::Initialize(
    const NR::PreblockPoolShape& requested,
    VkDescriptorBufferInfo raw,VkDescriptorBufferInfo target){
    if(ready||!raw.buffer||!target.buffer||raw.buffer==target.buffer||
       raw.offset||target.offset||!raw.range||!target.range||
       raw.range==VK_WHOLE_SIZE||target.range==VK_WHOLE_SIZE||
       raw.range>device.GetMaxStorageBufferRange()||
       target.range>device.GetMaxStorageBufferRange()||
       !NR::CanPreblockPool(requested,{raw.range,target.range}))
        return false;
    auto new_pool=CreateWrappedDescriptorPool(
        device,2u,1u,{VK_DESCRIPTOR_TYPE_STORAGE_BUFFER});
    std::array<VkDescriptorSetLayout,1> layouts{*descriptor_layout};
    auto assigned=CreateWrappedDescriptorSets(
        new_pool,vk::Span<VkDescriptorSetLayout>{
            layouts.data(),layouts.size()});
    if(assigned.IsOutOfPoolMemory()||assigned.size()!=1u)return false;
    const std::array<VkDescriptorBufferInfo,2> infos{raw,target};
    std::array<VkWriteDescriptorSet,2> writes{};
    for(std::uint32_t i=0u;i<2u;++i){
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
    pool=std::move(new_pool);
    sets=std::move(assigned);
    shape=requested;
    storage={raw.range,target.range};
    output=target;
    ready=true;
    return true;
}
bool NrRdna2PreblockPool::Record(){
    if(!ready||record_attempted||sets.size()!=1u||
       !NR::CanPreblockPool(shape,storage))return false;
    record_attempted=true;
    const VkPipeline native=*pipeline;
    const VkPipelineLayout native_layout=*layout;
    const auto set=sets[0],pc=shape;
    const auto gx=(shape.width/2u+7u)/8u;
    const auto gy=(shape.height/2u+7u)/8u;
    scheduler.RequestOutsideRenderPassOperationContext();
    scheduler.Record([native,native_layout,set,pc,gx,gy](
        vk::CommandBuffer cmd){
        cmd.BindPipeline(VK_PIPELINE_BIND_POINT_COMPUTE,native);
        cmd.BindDescriptorSets(VK_PIPELINE_BIND_POINT_COMPUTE,
                               native_layout,0,set,{});
        cmd.PushConstants(native_layout,VK_SHADER_STAGE_COMPUTE_BIT,
                          0,sizeof(pc),&pc);
        cmd.Dispatch(gx,gy,1u);
        cmd.PipelineBarrier(VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                            0,kVisible);
    });
    return true;
}
} // namespace Vulkan
