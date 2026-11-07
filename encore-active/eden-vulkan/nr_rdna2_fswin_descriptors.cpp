// SPDX-License-Identifier: MIT
#include "video_core/renderer_vulkan/nr_rdna2_fswin_descriptors.h"
#include "video_core/renderer_vulkan/nr_rdna2_fswin_qkv_alias_policy.h"
#include "video_core/renderer_vulkan/present/util.h"
#include "video_core/vulkan_common/vulkan_device.h"
#include <array>
#include <cstddef>
#include <cstdint>
namespace Vulkan {
using Eden::Graphics::NeuralRDNA2::CanFSwinGraph;
using Eden::Graphics::NeuralRDNA2::FSwinGraph;
namespace {
bool ValidFSwinBuffers(
    const FSwinGraph& p,const FSwinGraphBuffers& b,VkDeviceSize max_range){
    if(!CanFSwinGraph(p))return false;
    const bool c32=p.dense[0].channels==32u;
    const std::array<VkDescriptorBufferInfo,10> all{
        b.input,b.expanded,b.middle,b.contracted,b.query,b.key,
        b.value,b.context,b.output,b.weights};
    // Exact, disjoint subranges only: partial aliasing or an overlapping
    // Q/K/V view must never reach a Vulkan descriptor update.
    const bool reused=b.query.buffer==b.expanded.buffer||
                      b.key.buffer==b.expanded.buffer||
                      b.value.buffer==b.expanded.buffer;
    if(reused &&
       (b.query.buffer!=b.expanded.buffer||
        b.key.buffer!=b.expanded.buffer||
        b.value.buffer!=b.expanded.buffer||
        !Eden::Graphics::NeuralRDNA2::CanFSwinQkvReuseExpanded(
            {b.query.range,b.key.range,b.value.range,b.expanded.range})||
        b.query.offset!=0u||
        b.key.offset!=b.query.range||
        b.value.offset!=b.query.range+b.key.range))
        return false;
    for(std::size_t i=0;i<all.size();++i){
        if(i==2u&&c32)continue;
        const bool qkv=i>=4u&&i<=6u;
        if(!all[i].buffer||(all[i].offset!=0u&&!(reused&&qkv))||
           !all[i].range||all[i].range==VK_WHOLE_SIZE||
           all[i].range>max_range)return false;
        // Expanded is dead at QKV time, but Q, K and V are read
        // concurrently by attention: only their exact disjoint views
        // may share storage. Every other buffer pair remains isolated.
        for(std::size_t j=0;j<i;++j){
            if(j==2u&&c32)continue;
            if(all[j].buffer==all[i].buffer &&
               !(reused&&qkv&&(j==1u||(j>=4u&&j<=6u))))
                return false;
        }
    }
    const auto fits=[](const VkDescriptorBufferInfo& info,
                       std::uint64_t required){
        return info.range>=required;
    };
    for(unsigned i=0u;i<4u;++i){
        if(i==1u&&c32)continue;
        const auto& st=p.dense_storage[i];
        const auto& in=i==0u?b.input:i==1u?b.expanded:
            i==2u?(c32?b.expanded:b.middle):b.context;
        const auto& out=i==0u?b.expanded:i==1u?b.middle:
            i==2u?b.contracted:b.output;
        const auto& residual=i==3u?b.contracted:b.input;
        if(!fits(in,st.input_bytes)||!fits(out,st.output_bytes)||
           !fits(b.weights,st.weight_bytes))return false;
        if(i>=2u&&(!fits(residual,st.residual_bytes)||
                     !fits(b.weights,st.scales_bytes)))return false;
    }
    const auto& q=p.qkv_storage;
    if(!fits(b.contracted,q.activation_bytes)||
       !fits(b.weights,q.weight_bytes)||!fits(b.weights,q.scale_bytes)||
       !fits(b.query,q.q_bytes)||!fits(b.key,q.k_bytes)||
       !fits(b.value,q.v_bytes))return false;
    const auto& a=p.attention_storage;
    return fits(b.query,a.q_bytes)&&fits(b.key,a.k_bytes)&&
           fits(b.value,a.v_bytes)&&fits(b.context,a.out_bytes)&&
           fits(b.weights,a.bias_bytes);
}
}
NrRdna2FSwinDescriptors::NrRdna2FSwinDescriptors(const Device& device_)
    :device{device_}{}
NrRdna2FSwinDescriptors::~NrRdna2FSwinDescriptors()=default;
bool NrRdna2FSwinDescriptors::Initialize(
    const NrRdna2FSwinGraph& kernels,const FSwinGraph& plan,
    const FSwinGraphBuffers& b){
    if(pool||!ValidFSwinBuffers(
        plan,b,device.GetMaxStorageBufferRange()))return false;
    constexpr std::size_t kMaxDescriptors=31u; // 4*5+6+5
    vk::DescriptorPool new_pool=CreateWrappedDescriptorPool(
        device,kMaxDescriptors,6u,{VK_DESCRIPTOR_TYPE_STORAGE_BUFFER});
    std::array<VkDescriptorSetLayout,6> layouts{
        kernels.DenseDescriptorLayout(),kernels.DenseDescriptorLayout(),
        kernels.DenseDescriptorLayout(),kernels.DenseDescriptorLayout(),
        kernels.QkvDescriptorLayout(),kernels.AttentionDescriptorLayout()};
    vk::DescriptorSets new_sets=CreateWrappedDescriptorSets(
        new_pool,vk::Span<VkDescriptorSetLayout>{layouts.data(),layouts.size()});
    if(new_sets.IsOutOfPoolMemory()||new_sets.size()!=6u)return false;
    FSwinGraphDescriptorSets ready{};
    for(unsigned i=0u;i<4u;++i)ready.dense[i]=new_sets[i];
    ready.qkv=new_sets[4];
    ready.attention=new_sets[5];
    std::array<VkDescriptorBufferInfo,kMaxDescriptors> infos{};
    std::array<VkWriteDescriptorSet,kMaxDescriptors> writes{};
    std::size_t count=0u;
    const auto put=[&](VkDescriptorSet set,std::uint32_t binding,
                       const VkDescriptorBufferInfo& info) {
        infos[count]=info;
        writes[count]=VkWriteDescriptorSet{
            .sType=VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
            .pNext=nullptr,
            .dstSet=set,.dstBinding=binding,.dstArrayElement=0,
            .descriptorCount=1,.descriptorType=VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
            .pImageInfo=nullptr,.pBufferInfo=&infos[count],.pTexelBufferView=nullptr};
        ++count;
    };
    const bool c32=plan.dense[0].channels==32u;
    for(unsigned i=0u;i<4u;++i){
        if(i==1u&&c32)continue;
        const auto input=i==0u?b.input:i==1u?b.expanded:
            i==2u?(c32?b.expanded:b.middle):b.context;
        const auto residual=i==3u?b.contracted:b.input;
        const auto output=i==0u?b.expanded:i==1u?b.middle:
            i==2u?b.contracted:b.output;
        put(ready.dense[i],0u,input);
        put(ready.dense[i],1u,b.weights);
        put(ready.dense[i],2u,residual);
        put(ready.dense[i],3u,b.weights);
        put(ready.dense[i],4u,output);
    }
    put(ready.qkv,0u,b.contracted);
    put(ready.qkv,1u,b.weights);
    put(ready.qkv,2u,b.weights);
    put(ready.qkv,3u,b.query);
    put(ready.qkv,4u,b.key);
    put(ready.qkv,5u,b.value);
    put(ready.attention,0u,b.query);
    put(ready.attention,1u,b.key);
    put(ready.attention,2u,b.value);
    put(ready.attention,3u,b.weights); // GLSL binding 3 = relative bias
    put(ready.attention,4u,b.context); // GLSL binding 4 = output
    device.GetLogical().UpdateDescriptorSets(
        vk::Span<VkWriteDescriptorSet>{writes.data(),count},{});
    pool=std::move(new_pool);
    allocations=std::move(new_sets);
    assigned=ready;
    return true;
}
} // namespace Vulkan
