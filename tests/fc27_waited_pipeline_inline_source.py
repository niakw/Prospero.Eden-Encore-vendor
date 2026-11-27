#!/usr/bin/env python3
from pathlib import Path
R=Path(__file__).resolve().parents[1]
compute=(R/"encore-active/eden-vulkan/vk_compute_pipeline_cost.cpp").read_text()
graphics=(R/"encore-active/eden-vulkan/vk_graphics_pipeline_cost.cpp").read_text()
ch=(R/"encore-active/eden-vulkan/include/video_core/renderer_vulkan/vk_compute_pipeline.h").read_text()
gh=(R/"encore-active/eden-vulkan/include/video_core/renderer_vulkan/vk_graphics_pipeline.h").read_text()
cfg=(R/"sources/eden/src/shader_recompiler/frontend/maxwell/control_flow.cpp").read_text()
structured=(R/"encore-active/eden/src/shader_recompiler/frontend/maxwell/structured_control_flow.cpp").read_text()
for cls,src in (("ComputePipeline",compute),("GraphicsPipeline",graphics)):
    assert f"void {cls}::RunBuild()" in src
    assert "build_claimed.exchange(true, std::memory_order_acq_rel)" in src
    assert "scheduler.Record([this](vk::CommandBuffer) {\n            RunBuild();" in src
    assert "QueueWork([this]" in src
for src in (ch,gh):
    assert "#include <functional>" in src
    assert "std::function<void()> build_task;" in src
    assert "std::atomic_bool build_claimed{false};" in src
assert "function.blocks.upper_bound(pc, Compare{})" in cfg
assert "std::ranges::find_if(function.blocks" not in cfg
assert "for (Node left = left_sibling, right = right_sibling;;)" in structured
print("FC27_WAITED_PIPELINE_INLINE_SOURCE_PASS upstream=f8f29b5")
