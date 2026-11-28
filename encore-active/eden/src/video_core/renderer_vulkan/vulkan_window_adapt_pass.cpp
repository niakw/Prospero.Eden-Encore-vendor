// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

// SPDX-FileCopyrightText: Copyright 2024 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <boost/container/small_vector.hpp>

#include "core/frontend/framebuffer_layout.h"
#include "hud.h"
#include "vulkan_hud_shaders.h"
#include "video_core/framebuffer_config.h"
#include "video_core/host_shaders/vulkan_present_vert_spv.h"
#include "video_core/renderer_vulkan/present/layer.h"
#include "video_core/renderer_vulkan/present/present_push_constants.h"
#include "video_core/renderer_vulkan/present/util.h"
#include "video_core/renderer_vulkan/present/window_adapt_pass.h"
#include "video_core/renderer_vulkan/vk_present_manager.h"
#include "video_core/renderer_vulkan/vk_shader_util.h"
#include "video_core/vulkan_common/vulkan_device.h"
#include "video_core/vulkan_common/vulkan_memory_allocator.h"

namespace Vulkan {

WindowAdaptPass::WindowAdaptPass(const Device& device, VkFormat frame_format, vk::Sampler&& sampler_, vk::ShaderModule&& fragment_shader_)
    : sampler(std::move(sampler_))
    , fragment_shader(std::move(fragment_shader_))
{
    CreateDescriptorSetLayout(device);
    CreatePipelineLayout(device);
    CreateVertexShader(device);
    CreateRenderPass(device, frame_format);
    CreatePipelines(device);
}

WindowAdaptPass::~WindowAdaptPass() = default;

void WindowAdaptPass::Draw(const Device& device, RasterizerVulkan& rasterizer, Scheduler& scheduler, size_t image_index,
                           std::list<Layer>& layers,
                           std::span<const Tegra::FramebufferConfig> configs,
                           const Layout::FramebufferLayout& layout, Frame* dst, bool draw_hud) {

    const VkFramebuffer host_framebuffer{*dst->framebuffer};
    const VkRenderPass renderpass{*render_pass};
    const VkPipelineLayout graphics_pipeline_layout{*pipeline_layout};
    const VkExtent2D render_area{
        .width = dst->width,
        .height = dst->height,
    };

    const size_t layer_count = configs.size();
    struct LayerDraw {
        PresentPushConstants push_constants;
        VkDescriptorSet descriptor_set;
        VkPipeline graphics_pipeline;
    };
    // The usual single-layer frame needs no heap storage. Larger frames
    // share one allocation, transferred to the deferred draw command.
    boost::container::small_vector<LayerDraw, 1> draws(layer_count);

    auto layer_it = layers.begin();
    for (size_t i = 0; i < layer_count; i++) {
        auto& draw = draws[i];
        switch (configs[i].blending) {
        case Tegra::BlendMode::Opaque:
        default:
            draw.graphics_pipeline = *opaque_pipeline;
            break;
        case Tegra::BlendMode::Premultiplied:
            draw.graphics_pipeline = *premultiplied_pipeline;
            break;
        case Tegra::BlendMode::Coverage:
            draw.graphics_pipeline = *coverage_pipeline;
            break;
        }

        layer_it->ConfigureDraw(device, &draw.push_constants, &draw.descriptor_set, rasterizer, *sampler,
                                image_index, configs[i], layout);
        layer_it++;
    }

    const auto hud = draw_hud ? Eden::GetVulkanHud() : Eden::HudSnapshot{};
    // The 3D jacket in the native launcher is the ONE authored loading
    // animation. The checked-in Vulkan SPIR-V includes an obsolete purple
    // loading scene; keep the shader binary pinned for normal FPS HUD only.
    // Rendering the loader without an overlay retains the GPU warm-up
    // swapchain presents, acquire/release order and first-frame handoff.
    const bool quiet_loading_handoff = hud.loading != 0;
    if (hud.width && !quiet_loading_handoff && !hud_pipeline) {
        const VkPushConstantRange range{VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(hud)};
        hud_layout = device.GetLogical().CreatePipelineLayout(VkPipelineLayoutCreateInfo{
            .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
            .pushConstantRangeCount = 1,
            .pPushConstantRanges = &range,
        });
        auto vertex = BuildShader(device, EDEN_HUD_VERT_SPV);
        auto fragment = BuildShader(device, EDEN_HUD_FRAG_SPV);
        hud_pipeline = CreateWrappedCoverageBlendingPipeline(
            device, render_pass, hud_layout, std::tie(vertex, fragment));
    }
    const VkPipeline hud_graphics_pipeline = *hud_pipeline;
    const VkPipelineLayout hud_pipeline_layout = *hud_layout;

    scheduler.Record([=, draws = std::move(draws)](vk::CommandBuffer cmdbuf) {
        // The launcher has already faded to this ink. Avoid a visibly
        // different second loading screen, without dropping empty-frame
        // presents while Vulkan/guest shaders initialize.
        const f32 bg_red = quiet_loading_handoff ? 0.024f :
            Settings::values.bg_red.GetValue() / 255.0f;
        const f32 bg_green = quiet_loading_handoff ? 0.035f :
            Settings::values.bg_green.GetValue() / 255.0f;
        const f32 bg_blue = quiet_loading_handoff ? 0.039f :
            Settings::values.bg_blue.GetValue() / 255.0f;
        const VkClearAttachment clear_attachment{
            .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
            .colorAttachment = 0,
            .clearValue =
                {
                    .color = {.float32 = {bg_red, bg_green, bg_blue, 1.0f}},
                },
        };
        const VkClearRect clear_rect{
            .rect =
                {
                    .offset = {0, 0},
                    .extent = render_area,
                },
            .baseArrayLayer = 0,
            .layerCount = 1,
        };

        BeginRenderPass(cmdbuf, renderpass, host_framebuffer, render_area);
        cmdbuf.ClearAttachments({clear_attachment}, {clear_rect});

        for (size_t i = 0; i < layer_count; i++) {
            const auto& draw = draws[i];
            cmdbuf.BindPipeline(VK_PIPELINE_BIND_POINT_GRAPHICS, draw.graphics_pipeline);
            cmdbuf.PushConstants(graphics_pipeline_layout, VK_SHADER_STAGE_VERTEX_BIT,
                                 draw.push_constants);
            cmdbuf.BindDescriptorSets(VK_PIPELINE_BIND_POINT_GRAPHICS, graphics_pipeline_layout, 0,
                                      draw.descriptor_set, {});
            cmdbuf.Draw(4, 1, 0, 0);
        }

        // The overlay is laid out for a picture 1080 rows high and scaled to this one.
        const auto hud_units = [rows = render_area.height](uint32_t value) { return value * rows / 1080u; };
        if (hud.width && !quiet_loading_handoff &&
            render_area.width >= hud_units(hud.width + 16) &&
            render_area.height >= hud_units(64)) {
            const VkRect2D scissor{
                {static_cast<int32_t>(hud_units(16)), static_cast<int32_t>(hud_units(16))},
                {hud_units(hud.width), hud_units(48)}};
            const VkViewport viewport{static_cast<float>(scissor.offset.x), static_cast<float>(scissor.offset.y),
                static_cast<float>(scissor.extent.width), static_cast<float>(scissor.extent.height), 0, 1};
            cmdbuf.SetViewport(0, viewport);
            cmdbuf.SetScissor(0, scissor);
            cmdbuf.BindPipeline(VK_PIPELINE_BIND_POINT_GRAPHICS, hud_graphics_pipeline);
            auto constants = hud;
            // Only the regular FPS overlay reaches this pass. No second
            // full-screen splash is submitted on the warm-up swapchain.
            constants.x = hud_units(hud.x);
            constants.y = hud_units(hud.y);
            constants.width = render_area.height;
            cmdbuf.PushConstants(hud_pipeline_layout, VK_SHADER_STAGE_FRAGMENT_BIT, constants);
            cmdbuf.Draw(4, 1, 0, 0);
        }

        cmdbuf.EndRenderPass();
    });
}

VkDescriptorSetLayout WindowAdaptPass::GetDescriptorSetLayout() {
    return *descriptor_set_layout;
}

VkRenderPass WindowAdaptPass::GetRenderPass() {
    return *render_pass;
}

void WindowAdaptPass::CreateDescriptorSetLayout(const Device& device) {
    descriptor_set_layout =
        CreateWrappedDescriptorSetLayout(device, {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER});
}

void WindowAdaptPass::CreatePipelineLayout(const Device& device) {
    const VkPushConstantRange range{
        .stageFlags = VK_SHADER_STAGE_VERTEX_BIT,
        .offset = 0,
        .size = sizeof(PresentPushConstants),
    };

    pipeline_layout = device.GetLogical().CreatePipelineLayout(VkPipelineLayoutCreateInfo{
        .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
        .pNext = nullptr,
        .flags = 0,
        .setLayoutCount = 1,
        .pSetLayouts = descriptor_set_layout.address(),
        .pushConstantRangeCount = 1,
        .pPushConstantRanges = &range,
    });
}

void WindowAdaptPass::CreateVertexShader(const Device& device) {
    vertex_shader = BuildShader(device, VULKAN_PRESENT_VERT_SPV);
}

void WindowAdaptPass::CreateRenderPass(const Device& device, VkFormat frame_format) {
    render_pass = CreateWrappedRenderPass(device, frame_format, VK_IMAGE_LAYOUT_UNDEFINED);
}

void WindowAdaptPass::CreatePipelines(const Device& device) {
    opaque_pipeline = CreateWrappedPipeline(device, render_pass, pipeline_layout,
                                            std::tie(vertex_shader, fragment_shader));
    premultiplied_pipeline = CreateWrappedPremultipliedBlendingPipeline(
        device, render_pass, pipeline_layout, std::tie(vertex_shader, fragment_shader));
    coverage_pipeline = CreateWrappedCoverageBlendingPipeline(
        device, render_pass, pipeline_layout, std::tie(vertex_shader, fragment_shader));
}

} // namespace Vulkan
