// SPDX-License-Identifier: MIT
#include "video_core/renderer_vulkan/nr_rdna2_gpu_weights.h"
#include "video_core/vulkan_common/vulkan_device.h"
#include "video_core/vulkan_common/vulkan_memory_allocator.h"
#include <cstring>
#include <utility>
namespace Vulkan {
NrRdna2GpuWeights::NrRdna2GpuWeights(
    const Device& device_,MemoryAllocator& allocator_)
    :device{device_},allocator{allocator_}{}
NrRdna2GpuWeights::~NrRdna2GpuWeights()=default;
bool NrRdna2GpuWeights::Initialize(std::span<const std::uint8_t> cpu_bytes){
    if(buffer||cpu_bytes.empty()||(cpu_bytes.size()&3u)!=0u||
       cpu_bytes.size()>(256ull<<20)||
       cpu_bytes.size()>device.GetMaxStorageBufferRange())return false;
    const VkBufferCreateInfo ci{
        .sType=VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        .pNext=nullptr,.flags=0,
        .size=cpu_bytes.size(),
        .usage=VK_BUFFER_USAGE_STORAGE_BUFFER_BIT|
               VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
        .sharingMode=VK_SHARING_MODE_EXCLUSIVE,
        .queueFamilyIndexCount=0,.pQueueFamilyIndices=nullptr,
    };
    vk::Buffer candidate=allocator.CreateBuffer(ci,MemoryUsage::Upload);
    auto mapped=candidate.Mapped();
    if(mapped.size()<cpu_bytes.size())return false;
    std::memcpy(mapped.data(),cpu_bytes.data(),cpu_bytes.size());
    candidate.Flush();
    bytes=cpu_bytes.size();
    buffer=std::move(candidate);
    return true;
}
} // namespace Vulkan
