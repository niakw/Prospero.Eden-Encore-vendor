// SPDX-License-Identifier: MIT
// Complete single ViT QKV→attention GPU edge: FP8 GEMM→FP32 normalize
// →packed E4M3 Q/K/V→global attention. Not a full ViT block/network.
#pragma once
#include "video_core/renderer_vulkan/nr_rdna2_projection.h"
#include "video_core/renderer_vulkan/nr_rdna2_vit_qkv_norm.h"
#include "video_core/renderer_vulkan/nr_rdna2_vit_attention.h"
#include "video_core/vulkan_common/vulkan_wrapper.h"
namespace Vulkan {
class Device;
class Scheduler;
struct NrRdna2VitQkvAttentionBuffers {
    VkDescriptorBufferInfo input{},weights{},fp32{},temperatures{};
    VkDescriptorBufferInfo query{},key{},value{},context{};
};
class NrRdna2VitQkvAttentionLink final {
public:
    NrRdna2VitQkvAttentionLink(const Device&,Scheduler&);
    ~NrRdna2VitQkvAttentionLink();
    [[nodiscard]] bool Initialize(
        std::uint32_t tokens,const NrRdna2VitQkvAttentionBuffers&);
    [[nodiscard]] bool Record();
private:
    const Device& device;
    NrRdna2Projection gemm;
    NrRdna2VitQkvNorm normalize;
    NrRdna2VitAttention attention;
    vk::DescriptorPool pool;
    vk::DescriptorSets sets;
    Eden::Graphics::NeuralRDNA2::Projection gemm_shape{};
    Eden::Graphics::NeuralRDNA2::Storage gemm_storage{};
    Eden::Graphics::NeuralRDNA2::VitQkvNorm norm_shape{};
    Eden::Graphics::NeuralRDNA2::VitQkvNormStorage norm_storage{};
    Eden::Graphics::NeuralRDNA2::VitAttention attention_shape{};
    Eden::Graphics::NeuralRDNA2::VitAttentionStorage attention_storage{};
    bool ready{};
};
}
