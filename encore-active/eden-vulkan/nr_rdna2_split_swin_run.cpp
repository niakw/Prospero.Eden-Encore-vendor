// SPDX-License-Identifier: MIT
#include "video_core/renderer_vulkan/nr_rdna2_split_swin_run.h"
#include <cstdint>
#include <memory>
#include <utility>
#include <vector>
namespace Vulkan {
namespace NR=Eden::Graphics::NeuralRDNA2;
namespace {
[[nodiscard]] bool SameBuffer(
    VkDescriptorBufferInfo a,VkDescriptorBufferInfo b) noexcept {
    return a.buffer&&b.buffer&&a.buffer==b.buffer&&
           a.offset==b.offset&&a.range==b.range&&
           a.range!=0u&&a.range!=VK_WHOLE_SIZE;
}
}
NrRdna2SplitSwinRun::NrRdna2SplitSwinRun(
    const Device& d,MemoryAllocator& a,Scheduler& s)
    :device{d},allocator{a},scheduler{s}{}
NrRdna2SplitSwinRun::~NrRdna2SplitSwinRun()=default;
bool NrRdna2SplitSwinRun::InitializeWithModel(
    const NR::NeuralModelFile& model,const NR::NativeSplitRoutes& routes,
    std::uint32_t run_index,
    std::span<const NrRdna2SplitSwinBlockBuffers> buffers){
    if(ready||!model.IsOpen()||routes.run_count!=2u||
       run_index>=routes.run_count)return false;
    const auto& run=routes.runs[run_index];
    if(!run.count||run.start>=routes.count||
       run.start+run.count>routes.count||
       buffers.size()!=run.count)return false;
    std::vector<std::unique_ptr<NrRdna2SplitSwinBlock>> staged;
    staged.reserve(run.count);
    for(std::uint32_t i=0u;i<run.count;++i){
        const auto& current=routes.blocks[run.start+i];
        const auto& descriptors=buffers[i];
        if(current.first_row>=512u||current.layer_count<4u||
           current.layer_count>5u||
           (current.plan.has_pooled_projection!=(current.layer_count==5u)))
            return false;
        if(i>0u){
            const auto& preceding=routes.blocks[run.start+i-1u];
            if(current.source_block!=preceding.block||
               preceding.plan.has_pooled_projection||
               !SameBuffer(descriptors.input,buffers[i-1u].projected))
                return false;
        } else if(current.source_block!=run.entry_block){
            return false;
        }
        // A pooled layer is terminal to the Split run: its FinalHead
        // output goes to ViT, never directly to the next Split block.
        if(current.plan.has_pooled_projection&&i+1u!=run.count)
            return false;
        auto block=std::make_unique<NrRdna2SplitSwinBlock>(
            device,allocator,scheduler);
        if(!block->InitializeWithModel(
              model,current.block,current.plan,descriptors))
            return false;
        staged.push_back(std::move(block));
    }
    const auto& last=routes.blocks[run.start+run.count-1u];
    VkDescriptorBufferInfo proposed=last.plan.has_pooled_projection?
        buffers.back().final_output:buffers.back().projected;
    if(!proposed.buffer||!proposed.range||
       proposed.range==VK_WHOLE_SIZE)return false;
    result=proposed;
    input_view=routes.blocks[run.start].input_view;
    output_view=last.output_view;
    blocks=std::move(staged);
    ready=true;
    return true;
}
bool NrRdna2SplitSwinRun::Record(){
    if(!ready||blocks.empty())return false;
    // Every block was initialized as a single transaction, and the
    // output of one is the exact next input VkBuffer descriptor.
    for(const auto& block:blocks)
        if(!block||!block->Record())return false;
    return true;
}
} // namespace Vulkan
