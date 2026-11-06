// SPDX-License-Identifier: MIT
#include "video_core/renderer_vulkan/nr_rdna2_fswin_downsample_output.h"
#include "video_core/renderer_vulkan/present/util.h"
#include "video_core/vulkan_common/vulkan_device.h"
#include "video_core/vulkan_common/vulkan_memory_allocator.h"
#include <array>
#include <utility>
namespace Vulkan {
using Eden::Graphics::NeuralRDNA2::CanFSwinDownsample;
using Eden::Graphics::NeuralRDNA2::FSwinDownsample;
using Eden::Graphics::NeuralRDNA2::FSwinDownsampleStorage;
using Eden::Graphics::NeuralRDNA2::NeuralModelFile;
using Eden::Graphics::NeuralRDNA2::NeuralWeightArena;
using Eden::Graphics::NeuralRDNA2::NeuralWeightRange;
using Eden::Graphics::NeuralRDNA2::PrepareFSwinDownsampleWeights;

NrRdna2FSwinDownsampleOutput::NrRdna2FSwinDownsampleOutput(
    const Device& d,MemoryAllocator& allocator,Scheduler& scheduler)
    :device{d},shader{d,scheduler},weights{d,allocator}{}
NrRdna2FSwinDownsampleOutput::~NrRdna2FSwinDownsampleOutput()=default;

bool NrRdna2FSwinDownsampleOutput::Initialize(
    const NeuralModelFile& model,std::string_view prefix,
    VkDescriptorBufferInfo src,VkDescriptorBufferInfo dst,
    std::uint32_t width,std::uint32_t height,
    std::uint32_t channels,std::int32_t sx,std::int32_t sy){
    if(ready||!model.IsOpen()||prefix.empty()||
       !src.buffer||!dst.buffer||src.buffer==dst.buffer||
       src.offset||dst.offset||src.range==0u||dst.range==0u||
       src.range==VK_WHOLE_SIZE||dst.range==VK_WHOLE_SIZE||
       src.range>device.GetMaxStorageBufferRange()||
       dst.range>device.GetMaxStorageBufferRange())return false;
    FSwinDownsample planned{
        .width=width,.height=height,
        .windows_x=(width+7u)/8u,.windows_y=(height+7u)/8u,
        .channels=channels,.shift_x=sx,.shift_y=sy,
        .input_base_bytes=0u,.weight_base_bytes=0u,
        .output_base_bytes=0u,
    };
    // Check geometry BEFORE allocating or loading model weights.
    if(!CanFSwinDownsample(
           planned,{src.range,256ull<<20,dst.range}))return false;
    NeuralWeightArena staging;
    NeuralWeightRange range{};
    if(!PrepareFSwinDownsampleWeights(
           model,prefix,channels,staging,range))return false;
    if(staging.Bytes().empty()||staging.Bytes().size()%4u||
       range.length_bytes!=std::uint64_t(2u)*channels*channels||
       range.offset_bytes>staging.Bytes().size()||
       range.length_bytes>staging.Bytes().size()-range.offset_bytes)
        return false;
    const FSwinDownsampleStorage size{
        src.range,staging.Bytes().size(),dst.range};
    planned.weight_base_bytes=range.offset_bytes;
    if(!CanFSwinDownsample(planned,size)||
       staging.Bytes().size()>device.GetMaxStorageBufferRange())
        return false;
    if(!weights.Initialize(staging.Bytes()))return false;

    const std::array<VkDescriptorBufferInfo,3> infos{
        src,weights.Descriptor(),dst};
    auto new_pool=CreateWrappedDescriptorPool(
        device,3u,1u,{VK_DESCRIPTOR_TYPE_STORAGE_BUFFER});
    VkDescriptorSetLayout layout=shader.DescriptorLayout();
    auto allocated=CreateWrappedDescriptorSets(
        new_pool,vk::Span<VkDescriptorSetLayout>{&layout,1u});
    if(allocated.IsOutOfPoolMemory()||allocated.size()!=1u)return false;
    std::array<VkWriteDescriptorSet,3> writes{};
    for(std::uint32_t binding=0;binding<3u;++binding){
        writes[binding]=VkWriteDescriptorSet{
            .sType=VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
            .pNext=nullptr,.dstSet=allocated[0],
            .dstBinding=binding,.dstArrayElement=0,
            .descriptorCount=1,
            .descriptorType=VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
            .pImageInfo=nullptr,.pBufferInfo=&infos[binding],
            .pTexelBufferView=nullptr,
        };
    }
    device.GetLogical().UpdateDescriptorSets(
        vk::Span<VkWriteDescriptorSet>{writes.data(),writes.size()},{});
    pool=std::move(new_pool);
    sets=std::move(allocated);
    request=planned;
    storage=size;
    destination=dst;
    ready=true;
    return true;
}
bool NrRdna2FSwinDownsampleOutput::Record(){
    if(!ready||sets.size()!=1u)return false;
    return shader.Record(sets[0],request,storage);
}
} // namespace Vulkan
