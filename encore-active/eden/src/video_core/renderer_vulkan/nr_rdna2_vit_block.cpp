// SPDX-License-Identifier: MIT
#include "video_core/renderer_vulkan/nr_rdna2_vit_block.h"
#include "video_core/renderer_vulkan/nr_rdna2_vit_projection_stage.h"
#include "video_core/renderer_vulkan/nr_rdna2_vit_qkv_attention_link.h"
#include "video_core/vulkan_common/vulkan_device.h"
#include <array>
#include <cstdint>
#include <memory>
#include <string>
#include <utility>
namespace Vulkan {
namespace NR=Eden::Graphics::NeuralRDNA2;
namespace {
[[nodiscard]] bool CanOwnVitBuffers(
    const NrRdna2VitBlockBuffers& buffers,VkDeviceSize limit){
    const std::array<VkDescriptorBufferInfo,9> all{
        buffers.input,buffers.hidden,buffers.ffwd,
        buffers.attention_context,buffers.output,buffers.scratch,
        buffers.query,buffers.key,buffers.value};
    for(std::size_t i=0u;i<all.size();++i){
        const auto& b=all[i];
        if(!b.buffer||b.offset||!b.range||
           b.range==VK_WHOLE_SIZE||b.range>limit)
            return false;
        for(std::size_t j=0u;j<i;++j)
            if(all[j].buffer==b.buffer)return false;
    }
    return true;
}
}
struct NrRdna2VitBlock::State {
    NrRdna2VitProjectionStage expand,contract,projection;
    NrRdna2VitQkvAttentionLink qkv;
    State(const Device& d,MemoryAllocator& allocator,Scheduler& scheduler)
      :expand{d,allocator,scheduler,NR::VitProjectionKind::Expand},
       contract{d,allocator,scheduler,NR::VitProjectionKind::Contract},
       projection{d,allocator,scheduler,NR::VitProjectionKind::ResidualProjection},
       qkv{d,scheduler}{}
};
NrRdna2VitBlock::NrRdna2VitBlock(
    const Device& d,MemoryAllocator& a,Scheduler& s)
    :device{d},allocator{a},scheduler{s}{}
NrRdna2VitBlock::~NrRdna2VitBlock()=default;
bool NrRdna2VitBlock::InitializeWithModel(
    const NR::NeuralModelFile& model,std::uint32_t model_block,
    std::uint32_t tokens,const NrRdna2VitBlockBuffers& b){
    if(ready||state||!model.IsOpen()||model_block>511u||
       !CanOwnVitBuffers(b,device.GetMaxStorageBufferRange())||
       !tokens||tokens>4096u)return false;
    auto prepared=std::make_unique<State>(device,allocator,scheduler);
    // Four typed subgraphs implement five MODEL layers:
    // L0 FFN Expand+Act, L1 FFN Contract+Residual(input),
    // L2 QKV, L3 global attention, L4 Projection+Residual(L1).
    if(!prepared->expand.InitializeWithModel(
        model,model_block,tokens,b.input,b.scratch,b.hidden))
        return false;
    if(!prepared->contract.InitializeWithModel(
        model,model_block,tokens,b.hidden,b.scratch,b.ffwd,b.input))
        return false;
    const std::string qkv_prefix="unpacked-vit/block"+
        std::to_string(model_block)+".layer2.layer";
    NrRdna2VitQkvAttentionBuffers qkv_buffers{
        b.ffwd,{},b.scratch,{},b.query,b.key,b.value,b.attention_context};
    if(!prepared->qkv.InitializeWithModel(
           tokens,model,qkv_prefix,allocator,qkv_buffers))
        return false;
    if(!prepared->projection.InitializeWithModel(
        model,model_block,tokens,b.attention_context,b.scratch,
        b.output,b.ffwd))return false;
    output_info=b.output;
    state=std::move(prepared);
    ready=true;
    return true;
}
bool NrRdna2VitBlock::Record(){
    if(!ready||!state)return false;
    if(!state->expand.Record())return false;
    if(!state->contract.Record())return false;
    if(!state->qkv.Record())return false;
    return state->projection.Record();
}
} // namespace Vulkan
