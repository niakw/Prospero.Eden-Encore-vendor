#!/usr/bin/env python3
"""Host-only cache telemetry contract and parser regressions. Not a PS5 build."""
from __future__ import annotations

import importlib.util
from pathlib import Path

root = Path(__file__).resolve().parents[1]
source = (root / "encore-active/eden-vulkan/vk_pipeline_cache_cost.cpp").read_text()
reader = (root / "encore-active/eden-core/src/video_core/shader_cache_reader.inc").read_text()
writer = (root / "encore-active/eden-core/src/video_core/shader_environment.cpp").read_text()
module_path = root / "tools/ps5_shader_cache_log_triage.py"
spec = importlib.util.spec_from_file_location("ps5_cache_triage", module_path)
assert spec and spec.loader
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)

assert source.count("EDEN_PS5_SHADER_CACHE_DISK") == 1
assert source.count("EDEN_PS5_SHADER_CACHE_ADMISSION") == 1
cache_owner = source.split("void PipelineCache::LoadDiskResources(", 1)[1].split(
    "void PipelineCache::QueueVulkanPipelineCacheFlush(", 1)[0]
assert cache_owner.count("std::filesystem::file_size(path, error)") == 1
assert "pipeline_cache_filename = base_dir / \"vulkan.bin\";" in cache_owner
assert "vulkan_pipeline_cache_filename = base_dir / \"vulkan_pipelines.bin\";" in cache_owner
assert "env_before = probe_cache(pipeline_cache_filename)" in cache_owner
assert "driver_before = use_vulkan_pipeline_cache" in cache_owner
assert "env_after = probe_cache(pipeline_cache_filename)" in cache_owner
assert "++state.disk_compute_records;" in cache_owner
assert "++state.disk_graphics_records;" in cache_owner
assert "++state.disk_graphics_admitted;" in cache_owner
assert "state.disk_graphics_records - state.disk_graphics_admitted" in cache_owner
assert "state.disk_compute_records, state.disk_graphics_records" in cache_owner
assert "state.total, unsigned(CACHE_VERSION)" in cache_owner
assert cache_owner.index("std::filesystem::file_size(path, error)") < cache_owner.index(
    "const auto load_compute{")
assert cache_owner.index("++state.disk_graphics_records;") < cache_owner.index(
    "++state.disk_graphics_admitted;")
assert cache_owner.index("workers.WaitForRequests();") < cache_owner.index(
    "EDEN_PS5_SHADER_CACHE_ADMISSION")
assert "EDEN_PS5_SHADER_CACHE_DISK" not in source.split(
    "GraphicsPipeline* PipelineCache::CurrentGraphicsPipeline", 1)[1].split(
    "void PipelineCache::LoadDiskResources(", 1)[0]
assert "MAGIC_NUMBER" in reader and "cache_version != expected_cache_version" in reader
assert "std::filesystem::resize_file(filename" in reader
assert "GenericEnvironment::CanBeSerialized" in writer
assert "SerializePipeline(" in writer

def session(before_present=1, before_bytes=48, after_present=1,
            after_bytes=48, graphics=2, filtered=0, compute=1,
            queued=3, valid=3):
    return [
        "EDEN_PS5_SHADER_CACHE_DISK title=0400C3F00006E000 stage=before "
        f"env_present={before_present} env_bytes={before_bytes} "
        "driver_enabled=1 driver_present=1 driver_bytes=57 cache_version=23",
        "EDEN_PS5_SHADER_PREWARM queued=3 completed=3 "
        f"valid={valid} rejected=0 prebuild_ms=10 limit=32",
        "EDEN_PS5_SHADER_CACHE_ADMISSION title=0400C3F00006E000 "
        f"env_before_present={before_present} env_before_bytes={before_bytes} "
        f"env_after_present={after_present} env_after_bytes={after_bytes} "
        f"compute_records={compute} graphics_records={graphics} "
        f"graphics_filtered={filtered} queued={queued} cache_version=23"
    ]


def classify(lines):
    sessions = module.summarize(lines)
    assert len(sessions) == 1
    assert sessions[0]["title"] == "0400C3F00006E000"
    return sessions[0]["assessment"]

assert classify(session(before_present=0, before_bytes=0)) == (
    "environment_cache_missing_at_start")
assert classify(session(before_bytes=12)) == (
    "environment_cache_header_only_or_empty")
assert classify(session(after_present=0, after_bytes=0)) == (
    "environment_cache_removed_during_load")
assert classify(session(after_bytes=30)) == (
    "environment_cache_truncated_during_load")
assert classify(session(graphics=3, filtered=3, compute=0, queued=0)) == (
    "all_graphics_records_filtered_before_queue")
assert classify(session(graphics=0, filtered=0, compute=0, queued=0)) == (
    "populated_file_yielded_no_records_check_version_or_damage")
assert classify(session(graphics=1, filtered=0, compute=0, queued=1, valid=0)) == (
    "pipelines_queued_but_none_executable")
assert classify(session()) == "cache_admitted_verify_reuse_and_frame_latency"
assert module.summarize(["old game log only"]) == []

print("PS5_SHADER_CACHE_TRIAGE_HOST_PASS parser_cases=8 "
      "load_once=1 reader_owner=1 runtime=unverified")
