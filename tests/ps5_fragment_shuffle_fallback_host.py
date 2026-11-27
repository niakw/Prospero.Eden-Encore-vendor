#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""PS5 fragment-shuffle fallback: exact selected sources + 32/64-lane semantic witness."""
from pathlib import Path
import hashlib

ROOT=Path(__file__).resolve().parents[1]
profile=(ROOT/"encore-active/eden/src/shader_recompiler/profile.h").read_text()
warp=(ROOT/"encore-active/eden/src/shader_recompiler/backend/spirv/emit_spirv_warp.cpp").read_text()
pipeline=(ROOT/"encore-active/eden-vulkan/vk_pipeline_cache_cost.cpp").read_text()

# Newer Lichii/Eglantine architecture: broken fragment shuffles are lowered
# through broadcast/select rather than dropping the complete pipeline.
assert "bool has_broken_spirv_fragment_shuffle{};" in profile
assert """bool UseShuffleFallback(EmitContext& ctx) {
    return ctx.profile.has_broken_spirv_subgroup_shuffle ||
           (ctx.profile.has_broken_spirv_fragment_shuffle && ctx.stage == Stage::Fragment);
}""" in warp
assert "if (!UseShuffleFallback(ctx))" in warp
assert "if (!ctx.profile.support_shuffle_relative || UseShuffleFallback(ctx))" in warp
assert """const Id shuffled{UseShuffleFallback(ctx)
                          ? ShuffleAbsolute(ctx, value, HostThreadId(ctx, src_thread_id))
                          : ctx.OpGroupNonUniformShuffleXor""" in warp

# Keep Eden #4503 everywhere else. Only PS5 converts an exact HasBrokenPattern
# hit into a per-program fallback; unaffected shaders retain the normal profile.
assert "std::array<bool, Maxwell::MaxShaderProgram> fragment_shuffle_fallback{};" in pipeline
assert "Shader::Optimization::HasBrokenPattern(programs[index])" in pipeline
assert "fragment_shuffle_fallback[index] = true;" in pipeline
assert "using PS5 fragment shuffle fallback" in pipeline
assert "EDEN_FRAGMENT_SHUFFLE_FALLBACK count=%u hash=0x%016llx" in pipeline
assert "fallback_witness <= 8" in pipeline
assert "fragment_shuffle_witness.fetch_add(1, std::memory_order_relaxed)" in pipeline
assert "skipping fragment shuffle with known driver issue" in pipeline
assert "emit_profile.has_broken_spirv_fragment_shuffle = fragment_shuffle_fallback[index];" in pipeline
assert "EmitSPIRV(emit_profile, runtime_info, program, binding)" in pipeline

block=pipeline[pipeline.index("if (device.GetDriverID() != VK_DRIVER_ID_NVIDIA_PROPRIETARY"):pipeline.index("if (Settings::values.dump_guest_shaders)", pipeline.index("if (device.GetDriverID() != VK_DRIVER_ID_NVIDIA_PROPRIETARY"))]
assert "#ifdef PS5_NATIVE" in block and "#else" in block and "return nullptr;" in block
assert block.index("fragment_shuffle_fallback[index] = true;") < block.index("#else") < block.index("return nullptr;")

# Semantic oracle matching the broadcast/select fallback. Guest warps are
# 32 lanes even when the host subgroup is 64 lanes.
def host_thread_id(host_lane:int, guest_lane:int, subgroup:int)->int:
    return guest_lane if subgroup <= 32 else (host_lane // 32) * 32 + guest_lane

def read(values, src:int)->int:
    result=0
    for lane,value in enumerate(values):
        if lane == src:
            result=value
    return result

for subgroup in (32,64):
    values=[100+i for i in range(subgroup)]
    for host_lane in range(subgroup):
        guest=host_lane & 31
        partition=(host_lane//32)*32
        # index ^= 1
        target=host_thread_id(host_lane, guest ^ 1, subgroup)
        assert read(values,target)==100+partition+(guest^1)
        # up/down clamp to the 32-lane guest partition
        up=max(0,guest-1)
        down=min(31,guest+1)
        assert read(values,host_thread_id(host_lane,up,subgroup))==100+partition+up
        assert read(values,host_thread_id(host_lane,down,subgroup))==100+partition+down
        # butterfly is also routed through ShuffleAbsolute on PS5.
        butterfly=guest ^ 3
        assert read(values,host_thread_id(host_lane,butterfly,subgroup))==100+partition+butterfly

print("PS5_FRAGMENT_SHUFFLE_FALLBACK_PASS",
      "profile=1", "per_program=1", "desktop_skip=preserved",
      "subgroups=32,64", "index_up_down_butterfly=1")
