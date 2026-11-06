// SPDX-License-Identifier: MIT
#include "video_core/renderer_vulkan/nr_rdna2_vit_run.h"
#include <cstdint>
#include <memory>
#include <utility>
namespace Vulkan {
namespace NR=Eden::Graphics::NeuralRDNA2;
namespace {
[[nodiscard]] bool ExactBuffer(
    VkDescriptorBufferInfo a,VkDescriptorBufferInfo b) noexcept{
    return a.buffer&&a.buffer==b.buffer&&a.offset==b.offset&&
           a.range>0u&&a.range==b.range&&a.range!=VK_WHOLE_SIZE;
}
}
NrRdna2VitRun::NrRdna2VitRun(
    const Device& d,MemoryAllocator& a,Scheduler& s)
    :device{d},allocator{a},scheduler{s}{}
NrRdna2VitRun::~NrRdna2VitRun()=default;
bool NrRdna2VitRun::InitializeWithModel(
    const NR::NeuralModelFile& model,const NR::NativeVitRoute& route,
    std::span<const NrRdna2VitBlockBuffers> buffers) {
    if(ready||!model.IsOpen()||route.count!=8u||
       route.tokens==0u||route.tokens>4096u||
       buffers.size()!=route.count)return false;
    std::vector<std::unique_ptr<NrRdna2VitBlock>> prepared;
    prepared.reserve(route.count);
    for(std::uint32_t i=0u;i<route.count;++i){
        const auto& step=route.blocks[i];
        if(!step.block||step.tokens!=route.tokens||
           (i==0u?
            step.input_block!=route.input_split_block:
            (step.block!=route.blocks[i-1u].block+1u||
             step.input_block!=route.blocks[i-1u].block||
             !ExactBuffer(buffers[i].input,buffers[i-1u].output))))
            return false;
        auto block=std::make_unique<NrRdna2VitBlock>(
            device,allocator,scheduler);
        if(!block->InitializeWithModel(
             model,step.block,step.tokens,buffers[i]))
            return false;
        prepared.push_back(std::move(block));
    }
    if(!buffers.back().output.buffer||
       buffers.back().output.range==0u||
       buffers.back().output.range==VK_WHOLE_SIZE)return false;
    result=buffers.back().output;
    blocks=std::move(prepared);
    ready=true;
    return true;
}
bool NrRdna2VitRun::Record(){
    if(!ready||blocks.size()!=8u)return false;
    for(const auto& block:blocks)
        if(!block||!block->Record())return false;
    return true;
}
} // namespace Vulkan
