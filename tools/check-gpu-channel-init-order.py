#!/usr/bin/env python3
"""Regression: publish each GPU channel only after creating its payload.

Empty ChannelState::payload is normal between AllocateChannel and InitChannel.
The scheduler must never receive that empty state. PS5 retest still required.
"""
from pathlib import Path
root = Path(__file__).resolve().parents[1]
owners = (
    "encore-active/eden-core/src/video_core/gpu_vulkan.cpp",
    "encore-active/eden-core/src/video_core/gpu_gl_host.cpp",
    "encore-active/eden-core/src/video_core/gpu_gl_native.cpp",
    "encore-active/eden-core/src/video_core/gpu_gl_host_dev.cpp",
    "encore-active/eden-core/src/video_core/gpu_gl_native_dev.cpp",
    "sources/eden/src/video_core/gpu.cpp",
)
for path in owners:
    source = (root / path).read_text()
    create = source.split("std::shared_ptr<Control::ChannelState> CreateChannel(", 1)[1].split(
        "void BindChannel(", 1
    )[0]
    initialize = source.split("void InitChannel(Control::ChannelState& to_init, u64 program_id)", 1)[1].split(
        "void InitAddressSpace(", 1
    )[0]
    assert "scheduler.DeclareChannel" not in create, path
    assert (initialize.index("to_init.Init(system, program_id);") <
            initialize.index("channels.find(to_init.bind_id)") <
            initialize.index("scheduler.DeclareChannel(it->second);")), path
    assert "it->second.get() == &to_init" in initialize, path
print("GPU_CHANNEL_INIT_ORDER_OK", len(owners))
