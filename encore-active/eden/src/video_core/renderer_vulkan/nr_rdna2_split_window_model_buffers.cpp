// SPDX-License-Identifier: MIT
#include "video_core/renderer_vulkan/nr_rdna2_split_window_model_buffers.h"
#include "video_core/vulkan_common/vulkan_device.h"
#include "video_core/vulkan_common/vulkan_memory_allocator.h"
#include <array>
#include <bit>
#include <cstdint>
#include <cstddef>
#include <memory>
#include <span>
#include <vector>
#include <utility>
namespace Vulkan {
NrRdna2SplitWindowModelBuffers::NrRdna2SplitWindowModelBuffers(
    const Device& d,MemoryAllocator& a):device{d},allocator{a}{}
NrRdna2SplitWindowModelBuffers::~NrRdna2SplitWindowModelBuffers()=default;
bool NrRdna2SplitWindowModelBuffers::Initialize(
    const Eden::Graphics::NeuralRDNA2::NeuralModelFile& model,
    std::string_view prefix) {
    if(weights||scales||bias)return false;
    Eden::Graphics::NeuralRDNA2::SplitWindowModelPayload payload;
    if(!Eden::Graphics::NeuralRDNA2::PrepareSplitWindowModelWeights(
          model,prefix,payload))return false;
    const auto floats_to_bytes=[](std::span<const float> values){
        std::vector<std::uint8_t> bytes(values.size()*4u);
        for(std::size_t i=0u;i<values.size();++i){
            const auto word=std::bit_cast<std::uint32_t>(values[i]);
            for(unsigned j=0u;j<4u;++j)
                bytes[4u*i+j]=static_cast<std::uint8_t>(word>>(8u*j));
        }
        return bytes;
    };
    const auto scale_bytes=floats_to_bytes(payload.temperatures);
    const auto bias_bytes=floats_to_bytes(payload.bias_row_major);
    auto new_weights=std::make_unique<NrRdna2GpuWeights>(device,allocator);
    auto new_scales=std::make_unique<NrRdna2GpuWeights>(device,allocator);
    auto new_bias=std::make_unique<NrRdna2GpuWeights>(device,allocator);
    if(!new_weights->Initialize(payload.qkv_head_major)||
       !new_scales->Initialize(scale_bytes)||
       !new_bias->Initialize(bias_bytes))return false;
    weights=std::move(new_weights);
    scales=std::move(new_scales);
    bias=std::move(new_bias);
    return true;
}
}
