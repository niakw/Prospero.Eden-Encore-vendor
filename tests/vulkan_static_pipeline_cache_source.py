#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Exact-source receipt for Lichii #2100 static Vulkan cache hardening."""
from pathlib import Path
import hashlib

ROOT = Path(__file__).resolve().parents[1]
text = (ROOT / "encore-active/eden/src/video_core/renderer_vulkan/vulkan_device.cpp").read_text()

def git_blob(value: str) -> str:
    data = value.encode()
    return hashlib.sha1(b"blob " + str(len(data)).encode() + b"\0" + data).hexdigest()

load_after = '''    try {
        create(data.size(), data.empty() ? nullptr : data.data());
    } catch (const vk::Exception& e) {
        LOG_ERROR(Render_Vulkan,
                  "Driver rejected the static pipeline cache ({}), starting empty", e.what());
        create(0, nullptr);
    }
}'''
load_before = '''    create(data.size(), data.empty() ? nullptr : data.data());
}'''
save_after = '''    // Lichii #2100: keep the last known-good cache if the app exits or
    // crashes while a replacement is being written.
    auto temporary = filename;
    temporary += ".tmp";
    try {
        std::ofstream file(temporary, std::ios::binary | std::ios::trunc);
        file.exceptions(std::ofstream::failbit);
        if (!file.is_open()) {
            return;
        }
        file.write(STATIC_CACHE_MAGIC_NUMBER.data(), STATIC_CACHE_MAGIC_NUMBER.size())
            .write(reinterpret_cast<const char*>(&STATIC_CACHE_VERSION),
                   sizeof(STATIC_CACHE_VERSION))
            .write(data.data(), static_cast<std::streamsize>(size));
        file.close();
    } catch (const std::ios_base::failure& e) {
        Common::FS::RemoveFile(temporary);
        return;
    }
    std::error_code error;
    std::filesystem::rename(temporary, filename, error);
    if (error) {
        LOG_ERROR(Render_Vulkan, "Couldn't replace the static pipeline cache: {}", error.message());
        Common::FS::RemoveFile(temporary);
    }
}'''
save_before = '''    try {
        std::ofstream file(filename, std::ios::binary | std::ios::trunc);
        file.exceptions(std::ofstream::failbit);
        if (!file.is_open()) {
            return;
        }
        file.write(STATIC_CACHE_MAGIC_NUMBER.data(), STATIC_CACHE_MAGIC_NUMBER.size())
            .write(reinterpret_cast<const char*>(&STATIC_CACHE_VERSION),
                   sizeof(STATIC_CACHE_VERSION))
            .write(data.data(), static_cast<std::streamsize>(size));
    } catch (const std::ios_base::failure& e) {
        Common::FS::RemoveFile(filename);
    }
}'''
assert text.count(load_after) == 1
assert text.count(save_after) == 1
load = text.split("void Device::LoadStaticPipelineCache()", 1)[1].split(
    "void Device::SaveStaticPipelineCache()", 1)[0]
save = text.split("void Device::SaveStaticPipelineCache()", 1)[1].split(
    "\nVkFormat Device::GetSupportedFormat", 1)[0]
catch = load.index("catch (const vk::Exception& e)")
assert catch < load.index("create(0, nullptr);", catch)
assert save.index("std::ofstream file(temporary") < save.index("file.close();") < save.index(
    "std::filesystem::rename(temporary, filename, error);")
assert "Common::FS::RemoveFile(filename);" not in save
predecessor = text.replace(load_after, load_before, 1).replace(save_after, save_before, 1)
assert git_blob(predecessor) == "50578df547b769bd45cf4964ffa3f793c1749273", (
    "unreviewed vulkan_device.cpp drift around Lichii #2100")
pinned = (ROOT / "sources/eden/src/video_core/vulkan_common/vulkan_device.cpp").read_text()
assert load_after not in pinned and save_after not in pinned
print("VULKAN_STATIC_PIPELINE_CACHE_2100_SOURCE_PASS atomic_replace=1 driver_reject_empty=1 predecessor_exact=1 pinned_immutable=1")
