// SPDX-License-Identifier: MIT
// Model-driven native decoder: FP8 1024->512 GEMM -> FP16 -> skip/upsample
// -> FP8 output. All activation buffers are independent VkBuffers.
#pragma once
#include "video_core/renderer_vulkan/nr_rdna2_decoder_model_loader.h"
#include "video_core/renderer_vulkan/nr_rdna2_split_decoder_skip_repack.h"
#include "video_core/renderer_vulkan/nr_rdna2_decoder_fp32_to_fp16.h"
#include "video_core/renderer_vulkan/nr_rdna2_decoder_upsample.h"
#include "video_core/renderer_vulkan/nr_rdna2_projection.h"
#include "video_core/renderer_vulkan/nr_rdna2_gpu_weights.h"
#include <cstdint>
#include <memory>
namespace Vulkan {
class Device;
class MemoryAllocator;
class Scheduler;
class NrRdna2NativeDecoderLink final {
public:
    NrRdna2NativeDecoderLink(const Device&,MemoryAllocator&,Scheduler&);
    ~NrRdna2NativeDecoderLink();
    NrRdna2NativeDecoderLink(const NrRdna2NativeDecoderLink&)=delete;
    NrRdna2NativeDecoderLink& operator=(const NrRdna2NativeDecoderLink&)=delete;
    [[nodiscard]] bool InitializeWithModel(
        const Eden::Graphics::NeuralRDNA2::NeuralModelFile&,
        std::uint32_t decoder_block,std::uint32_t width,std::uint32_t height,
        VkDescriptorBufferInfo tiled_vit_input,
        VkDescriptorBufferInfo fp32_scratch,
        VkDescriptorBufferInfo fp16_projection,
        VkDescriptorBufferInfo source_split_skip_windows,
        VkDescriptorBufferInfo tiled_split_skip,
        VkDescriptorBufferInfo tiled_output);
    [[nodiscard]] bool Record();
    [[nodiscard]] VkDescriptorBufferInfo Result() const noexcept {
        return ready?output:VkDescriptorBufferInfo{};
    }
private:
    const Device& device;
    MemoryAllocator& allocator;
    Scheduler& scheduler;
    NrRdna2Projection gemm;
    NrRdna2DecoderFp32ToFp16 fp16;
    NrRdna2DecoderUpsample upsample;
    std::unique_ptr<NrRdna2SplitDecoderSkipRepack> skip_repack;
    vk::DescriptorPool descriptor_pool;
    vk::DescriptorSets descriptor_sets;
    std::unique_ptr<NrRdna2GpuWeights> weights,gain;
    VkDescriptorBufferInfo source{},scratch{},projected{},skip{},output{};
    Eden::Graphics::NeuralRDNA2::DecoderProjectionBatches projection_plan{};
    Eden::Graphics::NeuralRDNA2::DecoderUpsample upsample_plan{};
    Eden::Graphics::NeuralRDNA2::DecoderStorage upsample_storage{};
    bool ready{};
};
} // namespace Vulkan
