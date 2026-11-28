// SPDX-License-Identifier: MIT
#include "video_core/renderer_vulkan/nr_rdna2_vit_model_buffers.h"
#include "video_core/vulkan_common/vulkan_device.h"
#include "video_core/vulkan_common/vulkan_memory_allocator.h"
#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <utility>
namespace Vulkan {
NrRdna2VitModelBuffers::NrRdna2VitModelBuffers(
    const Device& d,MemoryAllocator& a):device{d},allocator{a}{}
NrRdna2VitModelBuffers::~NrRdna2VitModelBuffers()=default;
bool NrRdna2VitModelBuffers::Initialize(
    const Eden::Graphics::NeuralRDNA2::NeuralModelFile& model,
    std::string_view prefix){
    if(weights||temperatures)return false;
    Eden::Graphics::NeuralRDNA2::ViTQkvModelPayload payload;
    if(!Eden::Graphics::NeuralRDNA2::PrepareViTQkvModelWeights(
           model,prefix,payload))return false;
    std::array<std::uint8_t,32u*4u> scale_bytes{};
    for(std::size_t i=0u;i<payload.temperatures.size();++i){
        const auto raw=std::bit_cast<std::uint32_t>(payload.temperatures[i]);
        for(unsigned j=0u;j<4u;++j)
            scale_bytes[i*4u+j]=static_cast<std::uint8_t>(raw>>(8u*j));
    }
    // Construct transactionally. Neither half of the pair becomes visible
    // unless both VMA allocations and uploads succeed.
    auto proposed_weights=std::make_unique<NrRdna2GpuWeights>(
        device,allocator);
    if(!proposed_weights->Initialize(payload.row_major_qkv))return false;
    auto proposed_scales=std::make_unique<NrRdna2GpuWeights>(
        device,allocator);
    if(!proposed_scales->Initialize(scale_bytes))return false;
    weights=std::move(proposed_weights);
    temperatures=std::move(proposed_scales);
    return true;
}
}
