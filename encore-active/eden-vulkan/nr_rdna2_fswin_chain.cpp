// SPDX-License-Identifier: MIT
#include "video_core/renderer_vulkan/nr_rdna2_fswin_chain.h"
#include "video_core/renderer_vulkan/vk_scheduler.h"
#include "video_core/vulkan_common/vulkan_device.h"
#include "video_core/vulkan_common/vulkan_memory_allocator.h"
#include <memory>
#include <utility>
namespace Vulkan {
using Eden::Graphics::NeuralRDNA2::NeuralModelFile;
NrRdna2FSwinChain::NrRdna2FSwinChain(
    const Device& d,MemoryAllocator& a,Scheduler& s)
    :device{d},allocator{a},scheduler{s}{}
NrRdna2FSwinChain::~NrRdna2FSwinChain()=default;
bool NrRdna2FSwinChain::Initialize(
    const NeuralModelFile& model,
    std::span<const FSwinLayerSpec> specs){
    // Prevent accidentally scheduling shifted/resampled layers as if
    // their window-token layouts were identical.
    if(ready||!model.IsOpen()||specs.empty()||specs.size()>160u)
        return false;
    const auto channels=specs[0].channels;
    const auto windows=specs[0].windows;
    for(const auto& spec:specs){
        if(spec.prefix.empty()||spec.channels!=channels||
           spec.windows!=windows)return false;
    }
    std::vector<std::unique_ptr<NrRdna2FSwinLayerRuntime>> prepared;
    prepared.reserve(specs.size());
    for(const auto& spec:specs){
        auto runtime=std::make_unique<NrRdna2FSwinLayerRuntime>(
            device,allocator,scheduler);
        if(!runtime->Initialize(model,spec.prefix,spec.channels,
                                spec.windows,spec.residual_last))
            return false;
        prepared.emplace_back(std::move(runtime));
    }
    layers=std::move(prepared);
    ready=true;
    return true;
}
bool NrRdna2FSwinChain::UploadInput(
    std::span<const std::uint8_t> tokens){
    return ready&&layers.front()->UploadInput(tokens);
}
bool NrRdna2FSwinChain::Record(){
    if(!ready)return false;
    for(std::size_t i=0u;i<layers.size();++i){
        if(i>0u&&!layers[i]->LinkInputFrom(*layers[i-1u]))return false;
        if(!layers[i]->Record())return false;
    }
    return true;
}
} // namespace Vulkan
