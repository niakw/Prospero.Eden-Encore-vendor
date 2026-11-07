// SPDX-License-Identifier: MIT
#include "video_core/renderer_vulkan/nr_rdna2_unet_skip_cache.h"
#include "video_core/vulkan_common/vulkan_device.h"
#include "video_core/vulkan_common/vulkan_memory_allocator.h"
#include <array>
#include <cstdint>
#include <utility>
namespace Vulkan {
using Eden::Graphics::NeuralRDNA2::NativeUNetSkipMap;
NrRdna2UNetSkipCache::NrRdna2UNetSkipCache(
    const Device& d,MemoryAllocator& a):device{d},allocator{a}{}
NrRdna2UNetSkipCache::~NrRdna2UNetSkipCache()=default;
bool NrRdna2UNetSkipCache::Initialize(
    const NativeUNetSkipMap& requested,
    std::array<NrRdna2SkipRasterDimensions,4> geometry){
    if(ready||requested.count!=4u)return false;
    std::array<std::uint64_t,4> checked{};
    if(!Eden::Graphics::NeuralRDNA2::CanNativeUNetSkipSurfaces(
           requested,geometry,device.GetMaxStorageBufferRange(),checked))
        return false;
    std::array<VkDeviceSize,4> sizes{};
    for(std::size_t i=0;i<4u;++i)sizes[i]=checked[i];
    std::array<vk::Buffer,4> created{};
    for(std::size_t i=0;i<4u;++i){
        const VkBufferCreateInfo ci{
            .sType=VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
            .pNext=nullptr,.flags=0,.size=sizes[i],
            .usage=VK_BUFFER_USAGE_STORAGE_BUFFER_BIT|
                   VK_BUFFER_USAGE_TRANSFER_SRC_BIT|
                   VK_BUFFER_USAGE_TRANSFER_DST_BIT,
            .sharingMode=VK_SHARING_MODE_EXCLUSIVE,
            .queueFamilyIndexCount=0,.pQueueFamilyIndices=nullptr,
        };
        created[i]=allocator.CreateBuffer(ci,MemoryUsage::DeviceLocal);
        if(!created[i])return false;
    }
    buffers=std::move(created);
    map=requested;
    dimensions=geometry;
    byte_sizes=sizes;
    ready=true;
    return true;
}
VkDescriptorBufferInfo NrRdna2UNetSkipCache::Describe(
    std::size_t i) const noexcept{
    return VkDescriptorBufferInfo{
        .buffer=ready&&buffers[i]?*buffers[i]:VK_NULL_HANDLE,
        .offset=0,.range=ready?byte_sizes[i]:0u,
    };
}
bool NrRdna2UNetSkipCache::BindProducer(
    std::uint32_t down_block,NrRdna2FSwinChain& producer){
    if(!ready||!producer.Ready()||
       !producer.RequiresDownsampleOutput()||
       (scheduler&&scheduler!=producer.SchedulerIdentity()))
        return false;
    for(std::size_t i=0;i<map.count;++i){
        if(map.edges[i].down_block!=down_block)continue;
        if(bound[i]||producer.Channels()!=map.edges[i].channels||
           producer.OutputWindowLayout().windows_x!=
               map.edges[i].skip_windows_x||
           producer.OutputWindowLayout().windows_y!=
               map.edges[i].skip_windows_y)
            return false;
        if(!producer.BindSkipRasterOutput(
               Describe(i),dimensions[i].width,dimensions[i].height))
            return false;
        scheduler=producer.SchedulerIdentity();
        bound[i]=true;
        return true;
    }
    return false;
}
bool NrRdna2UNetSkipCache::RecordProducer(
    std::uint32_t down_block,NrRdna2FSwinChain& producer){
    if(!ready||!producer.Ready()||
       scheduler!=producer.SchedulerIdentity())return false;
    for(std::size_t i=0;i<map.count;++i){
        if(map.edges[i].down_block!=down_block)continue;
        if(!bound[i]||attempted[i]||recorded[i]||
           producer.ResultSkipRaster().buffer!=*buffers[i]||
           !producer.RequiresDownsampleOutput())
            return false;
        // Guard partial command-buffer submission against silent replay.
        attempted[i]=true;
        if(!producer.Record())return false;
        recorded[i]=true;
        return true;
    }
    return false;
}
VkDescriptorBufferInfo NrRdna2UNetSkipCache::PreparedForUpsample(
    std::uint32_t up_block) const noexcept {
    if(!ready||!scheduler)return {};
    for(std::size_t i=0;i<map.count;++i)
        if(bound[i]&&map.edges[i].up_block==up_block)
            return Describe(i);
    return {};
}
VkDescriptorBufferInfo NrRdna2UNetSkipCache::ForUpsample(
    std::uint32_t up_block) const noexcept {
    if(!ready)return {};
    for(std::size_t i=0;i<map.count;++i)
        if(recorded[i]&&map.edges[i].up_block==up_block)
            return Describe(i);
    return {};
}
} // namespace Vulkan
