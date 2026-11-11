#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Extract the ACTUAL opt-in FSwin shared-scratch tensor methods.

This creates a translation-unit with fake VkBuffer/VMA handles, but compiles
the real native InitializeWithScratch + Info bodies unchanged under GCC/Clang.
It tests allocation/descriptor identity without building or claiming PS5.
"""
from __future__ import annotations
import hashlib
import sys
from pathlib import Path

SOURCE="encore-active/eden-vulkan/nr_rdna2_fswin_gpu_tensors.cpp"

def method(source:str,name:str)->str:
    if source.count(name)!=1:
        raise ValueError(f"Expected one native method: {name}, got {source.count(name)}")
    pos=source.index(name)
    start=source.rfind("\n",0,pos)+1
    body=source.index("{",pos)
    depth=0
    end=None
    for i in range(body,len(source)):
        if source[i]=="{":depth+=1
        if source[i]=="}":
            depth-=1
            if depth==0:
                end=i+1
                break
    if end is None:raise ValueError("Unbalanced native method "+name)
    return source[start:end]

def main()->None:
    if len(sys.argv)!=3:
        raise SystemExit("usage: extract_r09_fswin_shared_tensor.py <native-CPP> <output.h>")
    src=Path(sys.argv[1]).read_text(encoding="utf-8")
    if "#if defined(EDEN_R09_MIN_FSWIN_SCRATCH_POOL_EXPERIMENTAL)" not in src:
        raise ValueError("Opt-in guard missing from pinned native source")
    init=method(src,"NrRdna2FSwinGpuTensors::InitializeWithScratch(")
    info=method(src,"NrRdna2FSwinGpuTensors::Info(")
    required=(
        "shared_scratch=scratch;",
        "shared_scratch_active=true;",
        "created[i]=allocator.CreateBuffer(",
        "i==0u?MemoryUsage::Upload:MemoryUsage::DeviceLocal",
        "requested[1]!=shape.capacity[0]",
        "requested[7]!=shape.capacity[3]",
    )
    if any(x not in init for x in required):
        raise ValueError("Native borrow-allocator source drift")
    if "shared_scratch[slot].buffer" not in info:
        raise ValueError("Native descriptor borrowed-handle source drift")
    header=r'''// Generated from REAL native vendor method. Never hand-edit.
#pragma once
#include "rdna2_nr_fswin_scratch_pool_policy.h"
#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <utility>
#include <vector>
using VkDeviceSize=std::uint64_t;
using VkBuffer=std::uint64_t;
inline constexpr VkBuffer VK_NULL_HANDLE=0ull;
inline constexpr unsigned VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO=12u;
inline constexpr unsigned VK_BUFFER_USAGE_STORAGE_BUFFER_BIT=1u;
inline constexpr unsigned VK_BUFFER_USAGE_TRANSFER_SRC_BIT=2u;
inline constexpr unsigned VK_BUFFER_USAGE_TRANSFER_DST_BIT=4u;
inline constexpr unsigned VK_SHARING_MODE_EXCLUSIVE=0u;
struct VkDescriptorBufferInfo{
    VkBuffer buffer{};VkDeviceSize offset{},range{};
};
struct VkBufferCreateInfo{
    unsigned sType{};const void* pNext{};unsigned flags{};
    VkDeviceSize size{};unsigned usage{},sharingMode{};
    unsigned queueFamilyIndexCount{};const void* pQueueFamilyIndices{};
};
enum class MemoryUsage{Upload,DeviceLocal};
namespace vk{
struct Buffer {
    VkBuffer id{};std::uint64_t requested{};
    explicit operator bool()const noexcept{return id!=0u;}
    [[nodiscard]] VkBuffer operator*()const noexcept{return id;}
    struct Mapping{
        std::uint64_t bytes{};
        [[nodiscard]] std::uint64_t size()const noexcept{return bytes;}
    };
    [[nodiscard]] Mapping Mapped()const noexcept{return {requested};}
};
}
struct Device {
    std::uint64_t max_range{256ull<<20};
    [[nodiscard]] std::uint64_t GetMaxStorageBufferRange()const noexcept{
        return max_range;
    }
};
struct FakeAllocator {
    std::uint64_t next_id{1000ull},calls{},fail_on{};
    std::vector<std::uint64_t> requested{};
    std::vector<MemoryUsage> usages{};
    [[nodiscard]] vk::Buffer CreateBuffer(
        const VkBufferCreateInfo& ci,MemoryUsage usage){
        ++calls;
        if(ci.size==0u||(fail_on!=0u&&calls==fail_on))return {};
        requested.push_back(ci.size);
        usages.push_back(usage);
        return {++next_id,ci.size};
    }
};
namespace Eden::Graphics::NeuralRDNA2 {
struct GraphDense {std::uint32_t channels{},windows{};};
struct GraphDenseStorage {
    std::uint64_t input_bytes{},output_bytes{},residual_bytes{};
};
struct GraphQkvStorage {
    std::uint64_t q_bytes{},k_bytes{},v_bytes{},activation_bytes{};
};
struct GraphAttentionStorage {
    std::uint64_t q_bytes{},k_bytes{},v_bytes{},out_bytes{},
                  input_bytes{};
};
struct FSwinGraph {
    std::array<GraphDense,4> dense{};
    std::array<GraphDenseStorage,4> dense_storage{};
    GraphQkvStorage qkv_storage{};
    GraphAttentionStorage attention_storage{};
};
[[nodiscard]] inline bool CanFSwinGraph(const FSwinGraph& g)noexcept {
    return g.dense[0].channels==32u||g.dense[0].channels==64u||
           g.dense[0].channels==128u||g.dense[0].channels==256u;
}
struct FSwinQkvAliasing {std::uint64_t q,k,v,expanded;};
[[nodiscard]] inline bool CanFSwinQkvReuseExpanded(
    FSwinQkvAliasing p)noexcept {
    return p.q!=0u&&p.k!=0u&&p.v!=0u&&
           p.q<=p.expanded&&p.k<=p.expanded-p.q&&
           p.v<=p.expanded-p.q-p.k;
}
}
namespace Vulkan{
using Eden::Graphics::NeuralRDNA2::FSwinGraph;
using Eden::Graphics::NeuralRDNA2::CanFSwinGraph;
struct NrRdna2FSwinGpuTensors{
    const Device& device;
    FakeAllocator& allocator;
    std::array<vk::Buffer,9u> buffers{};
    std::array<VkDeviceSize,9u> sizes{};
    std::array<VkDescriptorBufferInfo,4u> shared_scratch{};
    bool shared_scratch_active{},ready{};
    NrRdna2FSwinGpuTensors(const Device& d,FakeAllocator& a):
        device{d},allocator{a}{}
    [[nodiscard]] bool InitializeWithScratch(
        const FSwinGraph& p,
        const std::array<VkDescriptorBufferInfo,4u>& scratch);
    [[nodiscard]] VkDescriptorBufferInfo Info(std::size_t index)const noexcept;
};
'''
    text=header+"\n"+init+"\n"+info+"\n} // namespace Vulkan\n"
    out=Path(sys.argv[2])
    out.parent.mkdir(parents=True,exist_ok=True)
    out.write_text(text,encoding="utf-8")
    print("R09_FSWIN_NATIVE_TENSOR_EXTRACT_PASS sha256="+
          hashlib.sha256((init+info).encode()).hexdigest()[:20])

if __name__=="__main__":
    main()
