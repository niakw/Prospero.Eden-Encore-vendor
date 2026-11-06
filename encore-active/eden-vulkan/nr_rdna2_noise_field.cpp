// SPDX-License-Identifier: MIT
#include "video_core/renderer_vulkan/nr_rdna2_noise_field.h"
#include "video_core/renderer_vulkan/present/util.h"
#include "video_core/renderer_vulkan/vk_scheduler.h"
#include "video_core/vulkan_common/vulkan_device.h"
#include "video_core/host_shaders/rdna2_nr_noise_field_comp_spv.h"

namespace Vulkan {
namespace {
using Eden::Graphics::NeuralRDNA2::NoiseField;
static_assert(sizeof(NoiseField) == sizeof(u32)*5);
constexpr VkMemoryBarrier kNoiseVisibility{
    .sType=VK_STRUCTURE_TYPE_MEMORY_BARRIER,
    .pNext=nullptr,
    .srcAccessMask=VK_ACCESS_SHADER_WRITE_BIT,
    .dstAccessMask=VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT,
};
}
NrRdna2NoiseField::NrRdna2NoiseField(const Device& device, Scheduler& scheduler_)
    : scheduler{scheduler_} {
    descriptor_layout=CreateWrappedDescriptorSetLayout(
        device, {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER}, VK_SHADER_STAGE_COMPUTE_BIT);
    constexpr VkPushConstantRange range{
        .stageFlags=VK_SHADER_STAGE_COMPUTE_BIT, .offset=0,
        .size=sizeof(NoiseField),
    };
    layout=device.GetLogical().CreatePipelineLayout({
        .sType=VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
        .pNext=nullptr, .flags=0, .setLayoutCount=1,
        .pSetLayouts=descriptor_layout.address(),
        .pushConstantRangeCount=1, .pPushConstantRanges=&range,
    });
    shader=CreateWrappedShaderModule(device, RDNA2_NR_NOISE_FIELD_COMP_SPV);
    pipeline=CreateWrappedComputePipeline(device, layout, *shader);
}
NrRdna2NoiseField::~NrRdna2NoiseField()=default;
bool NrRdna2NoiseField::Record(
    VkDescriptorSet validated_set, const NoiseField& request,
    std::uint64_t output_storage_bytes) {
    if (!validated_set ||
        !Eden::Graphics::NeuralRDNA2::CanNoiseField(request, output_storage_bytes))
        return false;
    const VkPipeline native_pipeline=*pipeline;
    const VkPipelineLayout native_layout=*layout;
    const u32 group_x=request.width/8u;
    const u32 group_y=request.height/8u;
    scheduler.RequestOutsideRenderPassOperationContext();
    scheduler.Record([validated_set,native_pipeline,native_layout,
                      group_x,group_y,request](vk::CommandBuffer cmd) {
        cmd.BindPipeline(VK_PIPELINE_BIND_POINT_COMPUTE,native_pipeline);
        cmd.BindDescriptorSets(VK_PIPELINE_BIND_POINT_COMPUTE,native_layout,
                               0,validated_set,{});
        cmd.PushConstants(native_layout,VK_SHADER_STAGE_COMPUTE_BIT,
                          0,sizeof(NoiseField),&request);
        cmd.Dispatch(group_x,group_y,1);
        cmd.PipelineBarrier(VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,0,
                            kNoiseVisibility);
    });
    return true;
}
} // namespace Vulkan
