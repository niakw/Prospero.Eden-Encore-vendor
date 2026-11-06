// SPDX-License-Identifier: MIT
#include "video_core/renderer_vulkan/nr_rdna2_fswin_upsample_output.h"
#include "video_core/renderer_vulkan/present/util.h"
#include "video_core/vulkan_common/vulkan_device.h"
#include "video_core/vulkan_common/vulkan_memory_allocator.h"
#include <array>
#include <cstddef>
#include <cstdint>
#include <utility>
namespace Vulkan {
using Eden::Graphics::NeuralRDNA2::CanFSwinUpsample;
using Eden::Graphics::NeuralRDNA2::FSwinUpsample;
using Eden::Graphics::NeuralRDNA2::FSwinUpsampleStorage;
using Eden::Graphics::NeuralRDNA2::NeuralModelFile;
using Eden::Graphics::NeuralRDNA2::NeuralWeightArena;
using Eden::Graphics::NeuralRDNA2::FSwinUpsampleWeightRanges;
using Eden::Graphics::NeuralRDNA2::PrepareFSwinUpsampleWeights;
NrRdna2FSwinUpsampleOutput::NrRdna2FSwinUpsampleOutput(
    const Device& d,MemoryAllocator& a,Scheduler& scheduler)
    :device{d},shader{d,scheduler},projection{d,a},gains{d,a}{}
NrRdna2FSwinUpsampleOutput::~NrRdna2FSwinUpsampleOutput()=default;

bool NrRdna2FSwinUpsampleOutput::Initialize(
    const NeuralModelFile& model,std::string_view prefix,
    VkDescriptorBufferInfo low,VkDescriptorBufferInfo skip,
    VkDescriptorBufferInfo dst,
    std::uint32_t width,std::uint32_t height,std::uint32_t channels) {
    if(ready||!model.IsOpen()||prefix.empty()||
       !low.buffer||!skip.buffer||!dst.buffer||
       low.buffer==skip.buffer||low.buffer==dst.buffer||
       skip.buffer==dst.buffer||
       low.offset||skip.offset||dst.offset||
       !low.range||!skip.range||!dst.range||
       low.range==VK_WHOLE_SIZE||skip.range==VK_WHOLE_SIZE||
       dst.range==VK_WHOLE_SIZE||
       low.range>device.GetMaxStorageBufferRange()||
       skip.range>device.GetMaxStorageBufferRange()||
       dst.range>device.GetMaxStorageBufferRange())
        return false;
    const FSwinUpsample planned{
        .width=width,.height=height,
        .windows_x=(width+7u)/8u,.windows_y=(height+7u)/8u,
        .channels=channels,
        .low_base_bytes=0u,.skip_base_bytes=0u,.weight_base_bytes=0u,
        .gain_base_floats=0u,.out_base_bytes=0u,
    };
    // Prove shape and memory bounds before touching user model files.
    if(!CanFSwinUpsample(planned,
        {low.range,skip.range,256ull<<20,256ull<<20,dst.range}))
        return false;
    NeuralWeightArena cpu;
    FSwinUpsampleWeightRanges ranges{};
    if(!PrepareFSwinUpsampleWeights(model,prefix,channels,cpu,ranges))
        return false;
    const auto bytes=cpu.Bytes();
    const auto contains=[&](const auto& range) {
        return range.length_bytes!=0u &&
            range.offset_bytes<=bytes.size() &&
            range.length_bytes<=bytes.size()-range.offset_bytes;
    };
    if(!contains(ranges.projection)||!contains(ranges.skip_gain)||
       ranges.channels!=channels||
       ranges.projection.length_bytes!=std::uint64_t(2u)*channels*channels||
       ranges.skip_gain.length_bytes!=std::uint64_t(channels)*sizeof(float))
        return false;
    const FSwinUpsampleStorage checked{
        low.range,skip.range,ranges.projection.length_bytes,
        ranges.skip_gain.length_bytes,dst.range,
    };
    if(!CanFSwinUpsample(planned,checked)||
       checked.weight_bytes>device.GetMaxStorageBufferRange()||
       checked.gain_bytes>device.GetMaxStorageBufferRange())
        return false;
    if(!projection.Initialize(bytes.subspan(
           ranges.projection.offset_bytes,ranges.projection.length_bytes))||
       !gains.Initialize(bytes.subspan(
           ranges.skip_gain.offset_bytes,ranges.skip_gain.length_bytes)))
        return false;
    const std::array<VkDescriptorBufferInfo,5> infos{
        low,skip,projection.Descriptor(),gains.Descriptor(),dst};
    auto candidate_pool=CreateWrappedDescriptorPool(
        device,5u,1u,{VK_DESCRIPTOR_TYPE_STORAGE_BUFFER});
    VkDescriptorSetLayout set_layout=shader.DescriptorLayout();
    auto allocated=CreateWrappedDescriptorSets(
        candidate_pool,vk::Span<VkDescriptorSetLayout>{&set_layout,1u});
    if(allocated.IsOutOfPoolMemory()||allocated.size()!=1u)return false;
    std::array<VkWriteDescriptorSet,5> writes{};
    for(std::uint32_t i=0;i<5u;++i) {
        writes[i]=VkWriteDescriptorSet{
            .sType=VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
            .pNext=nullptr,.dstSet=allocated[0],
            .dstBinding=i,.dstArrayElement=0,.descriptorCount=1u,
            .descriptorType=VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
            .pImageInfo=nullptr,.pBufferInfo=&infos[i],
            .pTexelBufferView=nullptr,
        };
    }
    device.GetLogical().UpdateDescriptorSets(
        vk::Span<VkWriteDescriptorSet>{writes.data(),writes.size()},{});
    pool=std::move(candidate_pool);
    sets=std::move(allocated);
    request=planned;
    storage=checked;
    destination=dst;
    ready=true;
    return true;
}
bool NrRdna2FSwinUpsampleOutput::Record(){
    if(!ready||sets.size()!=1u)return false;
    return shader.Record(sets[0],request,storage);
}
} // namespace Vulkan
