// SPDX-License-Identifier: MIT
#include "video_core/renderer_vulkan/nr_rdna2_fswin_layer_runtime.h"
#include "video_core/renderer_vulkan/vk_scheduler.h"
#include "video_core/vulkan_common/vulkan_device.h"
#include "video_core/vulkan_common/vulkan_memory_allocator.h"
#include <utility>
namespace Vulkan {
using Eden::Graphics::NeuralRDNA2::FSwinGraph;
using Eden::Graphics::NeuralRDNA2::FSwinLayerWeightRanges;
using Eden::Graphics::NeuralRDNA2::NeuralWeightArena;
using Eden::Graphics::NeuralRDNA2::NeuralModelFile;
using Eden::Graphics::NeuralRDNA2::MakeFSwinGraphPlan;
using Eden::Graphics::NeuralRDNA2::PrepareFSwinLayerWeights;
using Eden::Graphics::NeuralRDNA2::BindFSwinGraphWeights;
NrRdna2FSwinLayerRuntime::NrRdna2FSwinLayerRuntime(
    const Device& device,MemoryAllocator& allocator,Scheduler& scheduler)
    :graph{device,scheduler},gpu_weights{device,allocator},
     tensors{device,allocator},descriptors{device}{}
NrRdna2FSwinLayerRuntime::~NrRdna2FSwinLayerRuntime()=default;
bool NrRdna2FSwinLayerRuntime::Initialize(
    const NeuralModelFile& model,std::string_view layer_prefix,
    std::uint32_t channels,std::uint32_t windows,bool residual_last){
    if(ready)return false;
    FSwinGraph proposed{};
    if(!MakeFSwinGraphPlan(channels,windows,proposed))return false;
    NeuralWeightArena staging{};
    FSwinLayerWeightRanges ranges{};
    if(!PrepareFSwinLayerWeights(model,layer_prefix,channels,
                                 residual_last,staging,ranges))return false;
    if(!BindFSwinGraphWeights(ranges,staging.Bytes().size(),
                              proposed))return false;
    if(!gpu_weights.Initialize(staging.Bytes()))return false;
    if(!tensors.Initialize(proposed))return false;
    if(!descriptors.Initialize(graph,proposed,
        tensors.Views(gpu_weights.Descriptor())))return false;
    plan=proposed;
    ready=true;
    return true;
}
bool NrRdna2FSwinLayerRuntime::UploadInput(
    std::span<const std::uint8_t> e4m3_window_tokens){
    if(!ready||!tensors.UploadInput(e4m3_window_tokens))return false;
    input_loaded=true;
    return true;
}
bool NrRdna2FSwinLayerRuntime::Record(){
    return ready&&input_loaded&&graph.Record(descriptors.Sets(),plan);
}
} // namespace Vulkan
