// SPDX-License-Identifier: MIT
// Eden Encore R09 RDNA2 portable neural projection primitive, not DLSS.
// No cooperative matrix FP8 or HIP. No proprietary model code or learned weights.
#include <array>
#include <cstdint>
#include "video_core/renderer_vulkan/nr_rdna2_projection.h"
#include "video_core/renderer_vulkan/present/util.h"
#include "video_core/renderer_vulkan/vk_scheduler.h"
#include "video_core/vulkan_common/vulkan_device.h"
#include "video_core/host_shaders/rdna2_nr_fp8_proj_comp_spv.h"

namespace Vulkan {
namespace {
using Eden::Graphics::NeuralRDNA2::Projection;
static_assert(sizeof(Projection) == 7u * sizeof(std::uint32_t));

struct alignas(4) Push {
    u32 tokens;
    u32 channels;
    u32 inner;
    u32 flags{};
    u32 lda_bytes;
    u32 ldw_bytes;
    u32 a_base_bytes;
    u32 w_base_bytes;
};
static_assert(sizeof(Push) == 32);
constexpr VkMemoryBarrier kProjectionReadBarrier{
    .sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER,
    .pNext = nullptr,
    .srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT,
    .dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT,
};
} // namespace

NrRdna2Projection::NrRdna2Projection(const Device& device, Scheduler& scheduler_)
    : scheduler{scheduler_} {
    constexpr std::array<VkDescriptorType, 3> descriptor_types{
        VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
        VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
        VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
    };
    descriptor_layout = CreateWrappedDescriptorSetLayout(
        device, descriptor_types, VK_SHADER_STAGE_COMPUTE_BIT);
    constexpr VkPushConstantRange range{
        .stageFlags = VK_SHADER_STAGE_COMPUTE_BIT,
        .offset = 0,
        .size = sizeof(Push),
    };
    layout = device.GetLogical().CreatePipelineLayout({
        .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
        .pNext = nullptr,
        .flags = 0,
        .setLayoutCount = 1,
        .pSetLayouts = descriptor_layout.address(),
        .pushConstantRangeCount = 1,
        .pPushConstantRanges = &range,
    });
    shader = CreateWrappedShaderModule(device, RDNA2_NR_FP8_PROJ_COMP_SPV);
    pipeline = CreateWrappedComputePipeline(device, layout, *shader);
}

NrRdna2Projection::~NrRdna2Projection() = default;

bool NrRdna2Projection::Record(
    VkDescriptorSet validated_descriptor_set,
    const Eden::Graphics::NeuralRDNA2::Projection& p,
    const Eden::Graphics::NeuralRDNA2::Storage& buffers) {
    if (!validated_descriptor_set ||
        !Eden::Graphics::NeuralRDNA2::CanDispatch(p, buffers)) {
        return false;
    }
    // Only the network's graph planner can safely supply buffer descriptors:
    // the function never interprets a DLL or trusts unbounded byte offsets.
    const Push pc{
        .tokens = p.tokens,
        .channels = p.channels,
        .inner = p.inner,
        .flags = 0,
        .lda_bytes = p.lda_bytes,
        .ldw_bytes = p.ldw_bytes,
        .a_base_bytes = p.a_base_bytes,
        .w_base_bytes = p.w_base_bytes,
    };
    const VkPipeline native_pipeline = *pipeline;
    const VkPipelineLayout native_layout = *layout;
    constexpr u32 tile = Eden::Graphics::NeuralRDNA2::kLocal;
    const u32 groups_x = (p.channels + tile - 1) / tile;
    const u32 groups_y = (p.tokens + tile - 1) / tile;
    scheduler.RequestOutsideRenderPassOperationContext();
    scheduler.Record([native_pipeline, native_layout, validated_descriptor_set,
                      pc, groups_x, groups_y](vk::CommandBuffer cmd) {
        cmd.BindPipeline(VK_PIPELINE_BIND_POINT_COMPUTE, native_pipeline);
        cmd.BindDescriptorSets(VK_PIPELINE_BIND_POINT_COMPUTE, native_layout,
                               0, validated_descriptor_set, {});
        cmd.PushConstants(native_layout, VK_SHADER_STAGE_COMPUTE_BIT,
                          0, sizeof(Push), &pc);
        cmd.Dispatch(groups_x, groups_y, 1);
        cmd.PipelineBarrier(VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0,
                            kProjectionReadBarrier);
    });
    return true;
}
} // namespace Vulkan
