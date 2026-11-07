// SPDX-License-Identifier: MIT
#include "video_core/renderer_vulkan/nr_rdna2_texture_proxy.h"
#include "video_core/renderer_vulkan/present/util.h"
#include "video_core/renderer_vulkan/vk_scheduler.h"
#include "video_core/vulkan_common/vulkan_device.h"
#include "video_core/host_shaders/rdna2_nr_texture_proxy_comp_spv.h"
#include <array>
#include <cstdint>
#include <utility>
namespace Vulkan {
namespace NR=Eden::Graphics::NeuralRDNA2;
namespace {
static_assert(sizeof(NR::NRTextureProxyShape)==16u);
constexpr VkMemoryBarrier kVisible{
    .sType=VK_STRUCTURE_TYPE_MEMORY_BARRIER,.pNext=nullptr,
    .srcAccessMask=VK_ACCESS_SHADER_WRITE_BIT,
    .dstAccessMask=VK_ACCESS_SHADER_READ_BIT|VK_ACCESS_SHADER_WRITE_BIT,
};
}
NrRdna2TextureProxy::NrRdna2TextureProxy(
    const Device& d,Scheduler& s):device{d},scheduler{s}{
    descriptor_layout=CreateWrappedDescriptorSetLayout(
        d,{VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
           VK_DESCRIPTOR_TYPE_STORAGE_BUFFER},VK_SHADER_STAGE_COMPUTE_BIT);
    constexpr VkPushConstantRange push{
        .stageFlags=VK_SHADER_STAGE_COMPUTE_BIT,.offset=0,
        .size=sizeof(NR::NRTextureProxyShape)};
    layout=d.GetLogical().CreatePipelineLayout({
        .sType=VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
        .pNext=nullptr,.flags=0u,.setLayoutCount=1u,
        .pSetLayouts=descriptor_layout.address(),
        .pushConstantRangeCount=1u,.pPushConstantRanges=&push});
    shader=CreateWrappedShaderModule(d,RDNA2_NR_TEXTURE_PROXY_COMP_SPV);
    pipeline=CreateWrappedComputePipeline(d,layout,*shader);
}
NrRdna2TextureProxy::~NrRdna2TextureProxy()=default;
bool NrRdna2TextureProxy::Initialize(
    NR::NRTextureProxyShape request,VkImageView view,VkSampler sampler,
    VkFormat format,VkExtent2D extent,VkDescriptorBufferInfo out){
    if(attempted)return false;
    attempted=true;
    if(view==VK_NULL_HANDLE||sampler==VK_NULL_HANDLE||
       !out.buffer||out.offset||!out.range||
       out.range==VK_WHOLE_SIZE||
       out.range>device.GetMaxStorageBufferRange()||
       !NR::CanNRTextureProxy(
           request,{extent.width,extent.height},out.range))
        return false;
    // Explicit VkImage format policy: sampling sRGB would decode colour,
    // and accepting a random texture as HDR would corrupt the proxy.
    const bool ldr=request.color_mode==0u &&
        (format==VK_FORMAT_R8G8B8A8_UNORM||
         format==VK_FORMAT_B8G8R8A8_UNORM||
         format==VK_FORMAT_A8B8G8R8_UNORM_PACK32);
    const bool hdr=request.color_mode==1u &&
        (format==VK_FORMAT_R16G16B16A16_SFLOAT||
         format==VK_FORMAT_R32G32B32A32_SFLOAT);
    if(!ldr&&!hdr)
        return false;
    const auto properties=device.GetPhysical().GetFormatProperties(format);
    if(!(properties.optimalTilingFeatures&VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT))
        return false;
    auto proposed_pool=CreateWrappedDescriptorPool(device,2u,1u,{
        VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
        VK_DESCRIPTOR_TYPE_STORAGE_BUFFER});
    std::array<VkDescriptorSetLayout,1> layouts{*descriptor_layout};
    auto allocated=CreateWrappedDescriptorSets(
        proposed_pool,vk::Span<VkDescriptorSetLayout>{
            layouts.data(),layouts.size()});
    if(allocated.IsOutOfPoolMemory()||allocated.size()!=1u)return false;
    const VkDescriptorImageInfo info{
        .sampler=sampler,.imageView=view,.imageLayout=VK_IMAGE_LAYOUT_GENERAL};
    std::array<VkWriteDescriptorSet,2> writes{};
    writes[0]=VkWriteDescriptorSet{
        .sType=VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
        .pNext=nullptr,.dstSet=allocated[0],.dstBinding=0,
        .dstArrayElement=0u,.descriptorCount=1u,
        .descriptorType=VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
        .pImageInfo=&info,.pBufferInfo=nullptr,.pTexelBufferView=nullptr};
    writes[1]=VkWriteDescriptorSet{
        .sType=VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
        .pNext=nullptr,.dstSet=allocated[0],.dstBinding=1,
        .dstArrayElement=0u,.descriptorCount=1u,
        .descriptorType=VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
        .pImageInfo=nullptr,.pBufferInfo=&out,.pTexelBufferView=nullptr};
    device.GetLogical().UpdateDescriptorSets(
        vk::Span<VkWriteDescriptorSet>{writes.data(),writes.size()},{});
    pool=std::move(proposed_pool);
    sets=std::move(allocated);
    shape=request;
    output=out;
    ready=true;
    return true;
}
bool NrRdna2TextureProxy::Record(){
    if(!ready||record_attempted||sets.size()!=1u)return false;
    record_attempted=true;
    const VkPipeline native=*pipeline;
    const VkPipelineLayout native_layout=*layout;
    const auto pc=shape;
    const auto set=sets[0];
    const std::uint32_t gx=(pc.raster_width+7u)/8u;
    const std::uint32_t gy=(pc.raster_height+7u)/8u;
    scheduler.RequestOutsideRenderPassOperationContext();
    scheduler.Record([native,native_layout,pc,set,gx,gy](
                     vk::CommandBuffer cmd){
        cmd.BindPipeline(VK_PIPELINE_BIND_POINT_COMPUTE,native);
        cmd.BindDescriptorSets(VK_PIPELINE_BIND_POINT_COMPUTE,
                               native_layout,0u,set,{});
        cmd.PushConstants(native_layout,VK_SHADER_STAGE_COMPUTE_BIT,
                          0u,sizeof(pc),&pc);
        cmd.Dispatch(gx,gy,1u);
        cmd.PipelineBarrier(VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,0u,kVisible);
    });
    recorded=true;
    return true;
}
} // namespace Vulkan
