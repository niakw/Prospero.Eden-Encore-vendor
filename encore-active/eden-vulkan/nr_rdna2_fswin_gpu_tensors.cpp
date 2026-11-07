// SPDX-License-Identifier: MIT
#include "video_core/renderer_vulkan/nr_rdna2_fswin_gpu_tensors.h"
#include "video_core/renderer_vulkan/nr_rdna2_fswin_qkv_alias_policy.h"
#include "video_core/vulkan_common/vulkan_device.h"
#include "video_core/vulkan_common/vulkan_memory_allocator.h"
#include <algorithm>
#include <array>
#include <cstring>
#include <cstdint>
#include <utility>
namespace Vulkan {
using Eden::Graphics::NeuralRDNA2::CanFSwinGraph;
using Eden::Graphics::NeuralRDNA2::FSwinGraph;
NrRdna2FSwinGpuTensors::NrRdna2FSwinGpuTensors(
    const Device& d,MemoryAllocator& a):device{d},allocator{a}{}
NrRdna2FSwinGpuTensors::~NrRdna2FSwinGpuTensors()=default;
bool NrRdna2FSwinGpuTensors::Initialize(const FSwinGraph& p) {
    if(ready||!CanFSwinGraph(p))return false;
    const bool c32=p.dense[0].channels==32u;
    // Buffer indices: 0 input, 1 expanded, 2 middle (optional),
    // 3 contracted, 4/5/6 Q/K/V *views inside expanded*,
    // 7 context, 8 output. Dense contraction finishes reading expanded
    // before QKV overwrites its first three disjoint segments.
    const std::array<VkDeviceSize,9> requested{
        std::max(p.dense_storage[0].input_bytes,
                 p.dense_storage[2].residual_bytes),
        p.dense_storage[0].output_bytes,
        c32?0u:p.dense_storage[1].output_bytes,
        std::max({p.dense_storage[2].output_bytes,
                  p.dense_storage[3].residual_bytes,
                  p.qkv_storage.activation_bytes}),
        0u,0u,0u, // Q/K/V alias distinct slices of expanded
        std::max(p.attention_storage.out_bytes,
                 p.dense_storage[3].input_bytes),
        p.dense_storage[3].output_bytes,
    };
    const std::array<VkDeviceSize,3> qkv_sizes{
        std::max(p.qkv_storage.q_bytes,p.attention_storage.q_bytes),
        std::max(p.qkv_storage.k_bytes,p.attention_storage.k_bytes),
        std::max(p.qkv_storage.v_bytes,p.attention_storage.v_bytes)};
    if(!Eden::Graphics::NeuralRDNA2::CanFSwinQkvReuseExpanded(
           {qkv_sizes[0],qkv_sizes[1],qkv_sizes[2],requested[1]}))
        return false;
    std::uint64_t total=0u;
    constexpr std::uint64_t kMaxTotal=512ull<<20;
    for(std::size_t i=0u;i<requested.size();++i){
        const auto bytes=requested[i];
        if((bytes==0u && !(i==2u&&c32) &&
            !(i>=4u&&i<=6u))||
           bytes>device.GetMaxStorageBufferRange()||
           bytes>(256ull<<20)||(bytes&3u)!=0u||
           total>kMaxTotal-bytes)return false;
        total+=bytes;
    }
    std::array<vk::Buffer,9> created{};
    for(std::size_t i=0u;i<requested.size();++i){
        if(requested[i]==0u)continue;
        const VkBufferCreateInfo ci{
            .sType=VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
            .pNext=nullptr,.flags=0,.size=requested[i],
            .usage=VK_BUFFER_USAGE_STORAGE_BUFFER_BIT|
                   VK_BUFFER_USAGE_TRANSFER_SRC_BIT|
                   VK_BUFFER_USAGE_TRANSFER_DST_BIT,
            .sharingMode=VK_SHARING_MODE_EXCLUSIVE,
            .queueFamilyIndexCount=0,.pQueueFamilyIndices=nullptr,
        };
        created[i]=allocator.CreateBuffer(
            ci,i==0u?MemoryUsage::Upload:MemoryUsage::DeviceLocal);
        if(!created[i]||(i==0u&&
           created[i].Mapped().size()<requested[i]))return false;
    }
    buffers=std::move(created);
    sizes=requested;
    sizes[4]=qkv_sizes[0];
    sizes[5]=qkv_sizes[1];
    sizes[6]=qkv_sizes[2];
    ready=true;
    return true;
}
bool NrRdna2FSwinGpuTensors::UploadInput(
    std::span<const std::uint8_t> fp8_tokens) {
    if(!ready||fp8_tokens.size()!=sizes[0])return false;
    const auto mapped=buffers[0].Mapped();
    if(mapped.size()<fp8_tokens.size())return false;
    std::memcpy(mapped.data(),fp8_tokens.data(),fp8_tokens.size());
    buffers[0].Flush();
    return true;
}
VkDescriptorBufferInfo NrRdna2FSwinGpuTensors::Info(
    std::size_t i) const noexcept {
    // Storage descriptor offsets are relative shader origins. They are
    // aligned, within expanded, and never overlap other live Q/K/V views.
    const bool qkv=i>=4u&&i<=6u;
    const std::size_t owner=qkv?1u:i;
    const VkDeviceSize offset=!qkv||i==4u?0u:
        i==5u?sizes[4]:sizes[4]+sizes[5];
    return VkDescriptorBufferInfo{
        .buffer=ready&&buffers[owner]?*buffers[owner]:VK_NULL_HANDLE,
        .offset=ready?offset:0u,.range=ready?sizes[i]:0};
}
FSwinGraphBuffers NrRdna2FSwinGpuTensors::Views(
    VkDescriptorBufferInfo weight_buffer) const noexcept {
    if(!ready)return {};
    return FSwinGraphBuffers{
        .input=Info(0),.expanded=Info(1),.middle=Info(2),
        .contracted=Info(3),.query=Info(4),.key=Info(5),
        .value=Info(6),.context=Info(7),.output=Info(8),
        .weights=weight_buffer};
}
} // namespace Vulkan
