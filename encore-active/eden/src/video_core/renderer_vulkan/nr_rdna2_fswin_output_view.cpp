// SPDX-License-Identifier: MIT
#include "video_core/renderer_vulkan/nr_rdna2_fswin_output_view.h"
#include "video_core/renderer_vulkan/present/util.h"
#include "video_core/vulkan_common/vulkan_device.h"
#include "video_core/vulkan_common/vulkan_memory_allocator.h"
#include <cstddef>
#include <cstdint>
#include <utility>
#include <vector>
namespace Vulkan {
using Eden::Graphics::NeuralRDNA2::FSwinOutputViewPlan;
using Eden::Graphics::NeuralRDNA2::FSwinWindowLayout;
using Eden::Graphics::NeuralRDNA2::MakeFSwinOutputViewPlan;
NrRdna2FSwinOutputView::NrRdna2FSwinOutputView(
    const Device& d,MemoryAllocator& a,Scheduler& s)
    :device{d},allocator{a},repacker{d,s},raster{d,s}{}
NrRdna2FSwinOutputView::~NrRdna2FSwinOutputView()=default;
bool NrRdna2FSwinOutputView::Initialize(
    VkDescriptorBufferInfo source,VkDescriptorBufferInfo destination,
    FSwinWindowLayout layout,std::uint32_t channels,
    std::uint32_t width,std::uint32_t height){
    if(ready||!source.buffer||!destination.buffer||
       source.buffer==destination.buffer||
       source.offset||source.range==0u||destination.range==0u||
       source.range>device.GetMaxStorageBufferRange()||
       destination.range>device.GetMaxStorageBufferRange())return false;
    FSwinOutputViewPlan proposed{};
    if(!MakeFSwinOutputViewPlan(layout,channels,width,height,
         source.range,destination.range,proposed))return false;
    const auto windows_bytes=proposed.raster_storage.input_bytes;
    if(windows_bytes>device.GetMaxStorageBufferRange())return false;
    vk::Buffer buffer;
    VkDescriptorBufferInfo canonical=source;
    if(proposed.shifted){
        const VkBufferCreateInfo ci{
            .sType=VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
            .pNext=nullptr,.flags=0,.size=windows_bytes,
            .usage=VK_BUFFER_USAGE_STORAGE_BUFFER_BIT|
                   VK_BUFFER_USAGE_TRANSFER_SRC_BIT|
                   VK_BUFFER_USAGE_TRANSFER_DST_BIT,
            .sharingMode=VK_SHARING_MODE_EXCLUSIVE,
            .queueFamilyIndexCount=0,.pQueueFamilyIndices=nullptr,
        };
        buffer=allocator.CreateBuffer(ci,MemoryUsage::DeviceLocal);
        if(!buffer)return false;
        canonical={.buffer=*buffer,.offset=0,.range=windows_bytes};
    }
    const std::uint32_t set_count=proposed.shifted?2u:1u;
    auto new_pool=CreateWrappedDescriptorPool(
        device,2u*set_count,set_count,
        {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER});
    std::vector<VkDescriptorSetLayout> layouts;
    if(proposed.shifted)layouts.push_back(repacker.DescriptorLayout());
    layouts.push_back(raster.DescriptorLayout());
    auto new_sets=CreateWrappedDescriptorSets(
        new_pool,vk::Span<VkDescriptorSetLayout>{
            layouts.data(),layouts.size()});
    if(new_sets.IsOutOfPoolMemory()||new_sets.size()!=set_count)
        return false;
    std::vector<VkDescriptorBufferInfo> infos;
    if(proposed.shifted){
        infos.push_back(source);
        infos.push_back(canonical);
    }
    infos.push_back(canonical);
    infos.push_back(destination);
    std::vector<VkWriteDescriptorSet> writes(2u*set_count);
    for(std::uint32_t set=0u;set<set_count;++set){
        for(std::uint32_t binding=0u;binding<2u;++binding){
            const auto index=2u*set+binding;
            writes[index]=VkWriteDescriptorSet{
                .sType=VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
                .pNext=nullptr,
                .dstSet=new_sets[set],
                .dstBinding=binding,
                .dstArrayElement=0,
                .descriptorCount=1,
                .descriptorType=VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
                .pImageInfo=nullptr,.pBufferInfo=&infos[index],
                .pTexelBufferView=nullptr,
            };
        }
    }
    device.GetLogical().UpdateDescriptorSets(
        vk::Span<VkWriteDescriptorSet>{writes.data(),writes.size()},{});
    scratch=std::move(buffer);
    descriptor_pool=std::move(new_pool);
    descriptor_sets=std::move(new_sets);
    plan=proposed;
    ready=true;
    return true;
}
bool NrRdna2FSwinOutputView::Record(){
    if(!ready)return false;
    if(plan.shifted &&
       !repacker.Record(descriptor_sets[0],plan.repack,
                        plan.repack_storage))return false;
    return raster.Record(descriptor_sets[plan.shifted?1u:0u],
                         plan.raster,plan.raster_storage);
}
} // namespace Vulkan
