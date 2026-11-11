// SPDX-License-Identifier: MIT
// Compile and execute the EXACT extracted native InitializeWithScratch()
// and Info(). Fake VkBuffer and VMA IDs, no real Vulkan or PS5 runtime.
#include "r09_fswin_shared_tensor_extracted.h"
#include <array>
#include <cassert>
#include <cstdint>
#include <iostream>
namespace NR=Eden::Graphics::NeuralRDNA2;
using Vulkan::NrRdna2FSwinGpuTensors;

static NR::FSwinGraph Graph(std::uint32_t channels,std::uint32_t windows) {
    const auto p=NR::MakeFswinSharedScratchPlan(channels,windows,2u);
    assert(p.valid);
    NR::FSwinGraph g{};
    g.dense[0]={channels,windows};
    g.dense_storage[0].input_bytes=p.private_input_bytes;
    g.dense_storage[0].output_bytes=p.capacity[0];
    g.dense_storage[1].output_bytes=p.capacity[1];
    g.dense_storage[2].output_bytes=p.capacity[2];
    g.dense_storage[2].residual_bytes=p.private_input_bytes;
    g.dense_storage[3].input_bytes=p.capacity[3];
    g.dense_storage[3].output_bytes=p.private_output_bytes;
    g.dense_storage[3].residual_bytes=p.capacity[2];
    const auto one=p.private_input_bytes;
    g.qkv_storage={one,one,one,one};
    g.attention_storage={one,one,one,one,one};
    return g;
}
static std::array<VkDescriptorBufferInfo,4u>
Scratch(std::uint32_t channels,std::uint32_t windows) {
    const auto p=NR::MakeFswinSharedScratchPlan(channels,windows,2u);
    assert(p.valid);
    std::array<VkDescriptorBufferInfo,4u> views{};
    for(std::size_t i=0u;i<views.size();++i){
        if(p.capacity[i])
            views[i]={10ull+i,0u,p.capacity[i]};
    }
    return views;
}
static void Positive(std::uint32_t channels,std::uint32_t windows){
    const auto g=Graph(channels,windows);
    const auto borrowed=Scratch(channels,windows);
    Device d{};
    FakeAllocator a{};
    NrRdna2FSwinGpuTensors first{d,a};
    assert(first.InitializeWithScratch(g,borrowed));
    assert(first.ready&&first.shared_scratch_active);
    assert(a.calls==2u&&a.requested.size()==2u);
    assert(a.usages[0]==MemoryUsage::Upload);
    assert(a.usages[1]==MemoryUsage::DeviceLocal);
    assert(first.Info(0u).buffer!=first.Info(8u).buffer);
    assert(first.Info(0u).buffer!=borrowed[0].buffer);
    assert(first.Info(8u).buffer!=borrowed[0].buffer);
    assert(first.Info(1u).buffer==borrowed[0].buffer);
    assert(first.Info(2u).buffer==borrowed[1].buffer);
    assert(first.Info(3u).buffer==borrowed[2].buffer);
    assert(first.Info(7u).buffer==borrowed[3].buffer);
    if(channels==32u)
        assert(!first.Info(2u).buffer&&!first.Info(2u).range);
    const auto one=std::uint64_t(windows)*64ull*channels;
    assert(first.Info(4u).buffer==borrowed[0].buffer);
    assert(first.Info(5u).buffer==borrowed[0].buffer);
    assert(first.Info(6u).buffer==borrowed[0].buffer);
    assert(first.Info(4u).offset==0u);
    assert(first.Info(5u).offset==one);
    assert(first.Info(6u).offset==2u*one);
    assert(first.Info(6u).offset+first.Info(6u).range<=
           borrowed[0].range);
    NrRdna2FSwinGpuTensors next{d,a};
    assert(next.InitializeWithScratch(g,borrowed));
    assert(a.calls==4u&&a.requested.size()==4u);
    // Common intermediate storage, but two independent source/dest
    // pairs. LinkInputFrom still has distinct VkBuffer handles.
    assert(next.Info(1u).buffer==first.Info(1u).buffer);
    assert(next.Info(3u).buffer==first.Info(3u).buffer);
    assert(next.Info(7u).buffer==first.Info(7u).buffer);
    assert(next.Info(0u).buffer!=first.Info(0u).buffer);
    assert(next.Info(8u).buffer!=first.Info(8u).buffer);
    assert(next.Info(0u).buffer!=first.Info(8u).buffer);
    std::cout<<"R09_FSWIN_REAL_BORROWED_NATIVE_METHOD_PASS channels="
             <<channels<<" windows="<<windows
             <<" two_layers_owned_buffers="<<a.calls
             <<" shared_scratch_buffers="<<(channels==32u?3u:4u)
             <<" gpu=FAKE runtime=OFF\n";
}
static void Negative(std::uint32_t channels,std::uint32_t windows){
    const auto g=Graph(channels,windows);
    const auto good=Scratch(channels,windows);
    Device d{};
    for(std::uint64_t fail=1u;fail<=2u;++fail){
        FakeAllocator allocator{};
        allocator.fail_on=fail;
        NrRdna2FSwinGpuTensors owner{d,allocator};
        assert(!owner.InitializeWithScratch(g,good));
        assert(!owner.ready&&!owner.shared_scratch_active);
        assert(allocator.calls==fail);
    }
    {
        FakeAllocator a{};
        NrRdna2FSwinGpuTensors bad{d,a};
        auto views=good;
        views[0].range-=4u;
        assert(!bad.InitializeWithScratch(g,views));
        assert(a.calls==0u&&!bad.ready);
    }
    {
        FakeAllocator a{};
        NrRdna2FSwinGpuTensors bad{d,a};
        auto views=good;
        views[0].buffer=views[2].buffer;
        assert(!bad.InitializeWithScratch(g,views));
        assert(a.calls==0u&&!bad.ready);
    }
    {
        FakeAllocator a{};
        NrRdna2FSwinGpuTensors bad{d,a};
        auto views=good;
        views[0].offset=4u;
        assert(!bad.InitializeWithScratch(g,views));
        assert(a.calls==0u&&!bad.ready);
    }
    {
        FakeAllocator a{};
        NrRdna2FSwinGpuTensors bad{d,a};
        auto views=good;
        views[3].buffer=0u;
        assert(!bad.InitializeWithScratch(g,views));
        assert(a.calls==0u&&!bad.ready);
    }
    if(channels==32u){
        FakeAllocator a{};
        NrRdna2FSwinGpuTensors bad{d,a};
        auto views=good;
        views[1]={99u,0u,4u};
        assert(!bad.InitializeWithScratch(g,views));
        assert(a.calls==0u&&!bad.ready);
    }
    std::cout<<"R09_FSWIN_REAL_BORROWED_NATIVE_NEGATIVE_PASS channels="
             <<channels<<" injected_owner_failure=2"
             <<" invalid_alias_capacity_offset_middle_rejected=YES\n";
}
int main() {
    for(const auto [channels,windows]:std::array<std::array<std::uint32_t,2u>,4u>{{
        {{32u,3600u}},{{64u,920u}},{{128u,240u}},{{256u,60u}}
    }}){
        Positive(channels,windows);
        Negative(channels,windows);
    }
}
