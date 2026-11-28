// SPDX-License-Identifier: MIT
#include "video_core/renderer_vulkan/nr_rdna2_vit_gpu_tensors.h"
#include "video_core/vulkan_common/vulkan_device.h"
#include "video_core/vulkan_common/vulkan_memory_allocator.h"
#include <array>
#include <cstddef>
#include <cstdint>
#include <utility>
namespace Vulkan {
namespace NR=Eden::Graphics::NeuralRDNA2;
NrRdna2VitGpuTensors::NrRdna2VitGpuTensors(
    const Device& d,MemoryAllocator& a):device{d},allocator{a}{}
NrRdna2VitGpuTensors::~NrRdna2VitGpuTensors()=default;
bool NrRdna2VitGpuTensors::Initialize(std::uint32_t tokens){
    if(ready)return false;
    NR::VitQkvTensorLayout plan{};
    if(!NR::BuildVitQkvTensorLayout(
        tokens,device.GetMaxStorageBufferRange(),plan))return false;
    std::array<vk::Buffer,5> allocated{};
    for(std::size_t i=0u;i<allocated.size();++i){
        const VkBufferCreateInfo ci{
            .sType=VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
            .pNext=nullptr,.flags=0,.size=plan.sizes[i],
            .usage=VK_BUFFER_USAGE_STORAGE_BUFFER_BIT|
                   VK_BUFFER_USAGE_TRANSFER_SRC_BIT|
                   VK_BUFFER_USAGE_TRANSFER_DST_BIT,
            .sharingMode=VK_SHARING_MODE_EXCLUSIVE,
            .queueFamilyIndexCount=0,.pQueueFamilyIndices=nullptr};
        allocated[i]=allocator.CreateBuffer(ci,MemoryUsage::DeviceLocal);
        if(!allocated[i])return false;
    }
    buffers=std::move(allocated);
    sizes=plan.sizes;
    ready=true;
    return true;
}
VkDescriptorBufferInfo NrRdna2VitGpuTensors::Info(
    std::size_t i) const noexcept {
    if(!ready||i>=buffers.size()||!buffers[i])return {};
    return VkDescriptorBufferInfo{
        .buffer=*buffers[i],.offset=0,.range=sizes[i]};
}
NrRdna2VitQkvAttentionBuffers NrRdna2VitGpuTensors::Views(
    VkDescriptorBufferInfo input) const noexcept {
    if(!ready)return {};
    return NrRdna2VitQkvAttentionBuffers{
        .input=input,.weights={},.fp32=Info(0),.temperatures={},
        .query=Info(1),.key=Info(2),.value=Info(3),.context=Info(4)};
}
} // namespace Vulkan
