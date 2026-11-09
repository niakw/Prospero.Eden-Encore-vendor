// SPDX-License-Identifier: MIT
// Portable token layout adapter from MIT source, not proprietary model code or weights.
#include <array>
#include "video_core/renderer_vulkan/nr_rdna2_token_repack.h"
#include "video_core/renderer_vulkan/present/util.h"
#include "video_core/renderer_vulkan/vk_scheduler.h"
#include "video_core/vulkan_common/vulkan_device.h"
#include "video_core/host_shaders/rdna2_nr_repack_tokens_comp_spv.h"

namespace Vulkan {
namespace {
using Eden::Graphics::NeuralRDNA2::TokenRepack;
static_assert(sizeof(TokenRepack) == sizeof(u32)*6);
constexpr VkMemoryBarrier kRepackReadBarrier{
    .sType=VK_STRUCTURE_TYPE_MEMORY_BARRIER,
    .pNext=nullptr,
    .srcAccessMask=VK_ACCESS_SHADER_WRITE_BIT,
    .dstAccessMask=VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT,
};
}

NrRdna2TokenRepack::NrRdna2TokenRepack(
    const Device& device, Scheduler& scheduler_) : scheduler{scheduler_} {
    descriptor_layout=CreateWrappedDescriptorSetLayout(
        device, {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER}, VK_SHADER_STAGE_COMPUTE_BIT);
    constexpr VkPushConstantRange range{
        .stageFlags=VK_SHADER_STAGE_COMPUTE_BIT,
        .offset=0,
        .size=sizeof(TokenRepack),
    };
    layout=device.GetLogical().CreatePipelineLayout({
        .sType=VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
        .pNext=nullptr,
        .flags=0,
        .setLayoutCount=1,
        .pSetLayouts=descriptor_layout.address(),
        .pushConstantRangeCount=1,
        .pPushConstantRanges=&range,
    });
    shader=CreateWrappedShaderModule(device, RDNA2_NR_REPACK_TOKENS_COMP_SPV);
    pipeline=CreateWrappedComputePipeline(device, layout, *shader);
}

NrRdna2TokenRepack::~NrRdna2TokenRepack() = default;

bool NrRdna2TokenRepack::Record(
    VkDescriptorSet validated_set,
    const Eden::Graphics::NeuralRDNA2::TokenRepack& request,
    std::uint64_t arena_bytes) {
    if (!validated_set ||
        !Eden::Graphics::NeuralRDNA2::CanRepack(request, arena_bytes)) return false;
    const VkPipeline native_pipeline=*pipeline;
    const VkPipelineLayout native_layout=*layout;
    const std::uint64_t invocations=
        std::uint64_t(request.width)*request.height*(request.channels/16u);
    const u32 groups=static_cast<u32>((invocations+63u)/64u);
    scheduler.RequestOutsideRenderPassOperationContext();
    scheduler.Record([validated_set,native_pipeline,native_layout,groups,request](
                         vk::CommandBuffer cmd) {
        cmd.BindPipeline(VK_PIPELINE_BIND_POINT_COMPUTE,native_pipeline);
        cmd.BindDescriptorSets(VK_PIPELINE_BIND_POINT_COMPUTE,native_layout,
                               0,validated_set,{});
        cmd.PushConstants(native_layout,VK_SHADER_STAGE_COMPUTE_BIT,
                          0,sizeof(TokenRepack),&request);
        cmd.Dispatch(groups,1,1);
        cmd.PipelineBarrier(VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,0,
                            kRepackReadBarrier);
    });
    return true;
}
} // namespace Vulkan
