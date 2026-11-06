// SPDX-License-Identifier: MIT
#include "video_core/renderer_vulkan/nr_rdna2_vit_qkv_subgraph.h"
#include "video_core/vulkan_common/vulkan_device.h"
#include "video_core/vulkan_common/vulkan_memory_allocator.h"
#include <cstdint>
#include <memory>
#include <utility>
namespace Vulkan {
NrRdna2VitQkvSubgraph::NrRdna2VitQkvSubgraph(
    const Device& d,MemoryAllocator& a,Scheduler& s)
    :device{d},allocator{a},scheduler{s}{}
NrRdna2VitQkvSubgraph::~NrRdna2VitQkvSubgraph()=default;
bool NrRdna2VitQkvSubgraph::Initialize(
    std::uint32_t tokens,
    const Eden::Graphics::NeuralRDNA2::NeuralModelFile& model,
    std::string_view prefix,VkDescriptorBufferInfo gpu_input) {
    if(tensors||link)return false;
    auto tmp_tensors=std::make_unique<NrRdna2VitGpuTensors>(
        device,allocator);
    if(!tmp_tensors->Initialize(tokens))return false;
    auto tmp_link=std::make_unique<NrRdna2VitQkvAttentionLink>(
        device,scheduler);
    if(!tmp_link->InitializeWithModel(
        tokens,model,prefix,allocator,tmp_tensors->Views(gpu_input)))
        return false;
    tensors=std::move(tmp_tensors);
    link=std::move(tmp_link);
    return true;
}
bool NrRdna2VitQkvSubgraph::Record(){
    return bool(tensors)&&bool(link)&&link->Record();
}
} // namespace Vulkan
