#!/usr/bin/env python3
"""Source contract for PS5 learned shader-environment warm manifest.

The manifest is driver-independent environment data only. It must survive the
compiled cache directory, validate the legacy environment header/version before
restore, mirror only successfully admitted first-use records, and keep ordinary
startup prewarm bounded.
"""
from pathlib import Path

ROOT=Path(__file__).resolve().parents[1]
CPP=(ROOT/"encore-active/eden-vulkan/vk_pipeline_cache_cost.cpp").read_text()
HDR=(ROOT/"encore-active/eden-vulkan/include/video_core/renderer_vulkan/vk_pipeline_cache.h").read_text()

assert 'std::filesystem::path warm_pipeline_cache_filename;' in HDR
assert 'shader_dir.parent_path() / "shader-warm"' in CPP
assert 'warm_pipeline_cache_filename = warm_dir / "vulkan.bin";' in CPP
assert "constexpr std::array<char, 8> kEnvironmentMagic" in CPP
assert "'y', 'u', 'z', 'u', 'c', 'a', 'c', 'h'" in CPP
assert 'version == CACHE_VERSION' in CPP
assert 'std::filesystem::copy_file(' in CPP
assert 'restored_from_warm_manifest = !copy_error;' in CPP
assert 'kNormalStartupPrewarmPipelines = 32' in CPP
assert 'kRestoredStartupPrewarmPipelines = 128' in CPP
assert 'state.total == startup_prewarm_limit' in CPP
assert 'EDEN_PS5_SHADER_WARM_MANIFEST' in CPP

# Successful first-use graphics and compute records are mirrored on the
# serialization worker; no driver VkPipelineCache blob is copied to warm state.
assert CPP.count('SerializePipeline(key, env_ptrs, warm_pipeline_cache_filename, CACHE_VERSION);') == 2
assert 'SerializeVulkanPipelineCache(warm_pipeline_cache_filename' not in CPP
assert 'vulkan_pipelines.bin' in CPP

# The feature is PS5-only and must not alter desktop cache paths.
assert '#ifdef PS5_NATIVE\n    // Keep a driver-independent record' in CPP
assert '#ifdef PS5_NATIVE\n        if (!warm_pipeline_cache_filename.empty())' in CPP

print("PS5_COLD_SHADER_WARM_MANIFEST_SOURCE_PASS normal=32 restored=128 driver_blob=excluded")
