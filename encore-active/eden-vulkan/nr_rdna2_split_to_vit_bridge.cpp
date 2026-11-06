// SPDX-License-Identifier: MIT
#include "video_core/renderer_vulkan/nr_rdna2_split_to_vit_bridge.h"
#include "video_core/renderer_vulkan/present/util.h"
#include "video_core/vulkan_common/vulkan_device.h"
#include <array>
#include <cstdint>
#include <utility>
namespace Vulkan {
namespace NR=Eden::Graphics::NeuralRDNA2;
NrRdna2SplitToVitBridge::NrRdna2SplitToVitBridge(
    const Device& d,Scheduler& s):device{d},transposer{d,s}{}
NrRdna2SplitToVitBridge::~NrRdna2SplitToVitBridge()=default;
bool NrRdna2SplitToVitBridge::Initialize(
    std::uint32_t wx,std::uint32_t wy,
    VkDescriptorBufferInfo input,VkDescriptorBufferInfo output){
    if(ready||!wx||!wy||wx>1023u||wy>1023u||
       !input.buffer||!output.buffer||input.buffer==output.buffer||
       input.offset||output.offset||!input.range||!output.range||
       input.range==VK_WHOLE_SIZE||output.range==VK_WHOLE_SIZE||
       input.range>device.GetMaxStorageBufferRange()||
       output.range>device.GetMaxStorageBufferRange())return false;
    const NR::FSwinRasterLayout plan{
        wx*8u,wy*8u,wx,wy,1024u,1u};
    const NR::FSwinRasterLayoutStorage capacity{
        input.range,output.range};
    if(!NR::CanFSwinRasterLayout(plan,capacity))return false;
    auto new_pool=CreateWrappedDescriptorPool(
        device,2u,1u,{VK_DESCRIPTOR_TYPE_STORAGE_BUFFER});
    std::array<VkDescriptorSetLayout,1> layouts{
        transposer.DescriptorLayout()};
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
    pool=std::move(new_pool);
    sets=std::move(allocated);
    shape=plan;storage=capacity;ready=true;
    return true;
}
bool NrRdna2SplitToVitBridge::Record(){
    return ready&&sets.size()==1u&&
           NR::CanFSwinRasterLayout(shape,storage)&&
           transposer.Record(sets[0],shape,storage);
}
} // namespace Vulkan
