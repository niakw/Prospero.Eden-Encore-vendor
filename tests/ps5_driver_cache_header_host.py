#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Compile the exact native cache reader with a mock Vulkan device boundary."""
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
source = (ROOT / "encore-active/eden-vulkan/vk_pipeline_cache_cost.cpp").read_text()
helper = source.split("// ENCORE_DRIVER_CACHE_HEADER_BEGIN", 1)[1].split(
    "// ENCORE_DRIVER_CACHE_HEADER_END", 1)[0]
begin = "vk::PipelineCache PipelineCache::LoadVulkanPipelineCache("
reader = begin + source.split(begin, 1)[1].split("\n} // namespace Vulkan", 1)[0]
assert reader.index("cache_size > kNativeDriverCacheStagingLimit") < reader.index("cache_data.resize(cache_size)")
assert reader.index("NativeDriverCacheHeaderMatches(driver_header") < reader.index("cache_data.resize(cache_size)")
assert reader.index("file.seekg(static_cast<std::streamoff>(header_size)") < reader.index("file.read(cache_data.data(), cache_size)")
save = source.split("void PipelineCache::SerializeVulkanPipelineCache(", 1)[1].split(begin, 1)[0]
native_save = save.split("#ifdef PS5_NATIVE", 1)[1].split("#else", 1)[0]
assert native_save.index("cache_size > kNativeDriverCacheStagingLimit") < native_save.index("cache_data.resize(cache_size)")
assert native_save.index("std::filesystem::remove(staging, cleanup_error);") < native_save.index("std::filesystem::rename(staging, filename, rename_error);")
cpp = r"""
#include <algorithm>
#include <array>
#include <cassert>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <new>
#include <stdexcept>
#include <string>
#include <vector>
using u32 = std::uint32_t;
using std::size_t;
#define PS5_NATIVE 1
#define EDEN_NS2_PIPELINE_CACHE_GUARD 1
#define LOG_WARNING(...) ((void)0)
#define LOG_ERROR(...) ((void)0)
#define LOG_INFO(...) ((void)0)
constexpr u32 VK_STRUCTURE_TYPE_PIPELINE_CACHE_CREATE_INFO = 17;
constexpr u32 VK_PIPELINE_CACHE_HEADER_VERSION_ONE = 1;
struct VkPhysicalDeviceProperties {
    u32 vendorID = 0x1002, deviceID = 0x1234;
    unsigned char pipelineCacheUUID[16] = {1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16};
};
struct VkPipelineCacheCreateInfo {
    u32 sType; const void* pNext; u32 flags; size_t initialDataSize; const void* pInitialData;
};
namespace Common::FS {
bool RemoveFile(const std::filesystem::path& p) { return std::filesystem::remove(p); }
}
namespace vk { struct PipelineCache { size_t size; }; }
struct Device {
    VkPhysicalDeviceProperties properties{};
    unsigned calls = 0;
    bool reject_nonempty = false;
    const Device& GetPhysical() const { return *this; }
    const VkPhysicalDeviceProperties& GetProperties() const { return properties; }
    Device& GetLogical() { return *this; }
    vk::PipelineCache CreatePipelineCache(VkPipelineCacheCreateInfo info) {
        ++calls;
        assert((info.initialDataSize == 0) == (info.pInitialData == nullptr));
        if (info.initialDataSize && reject_nonempty) throw std::runtime_error("mock driver reject");
        if (info.initialDataSize) {
            const auto* data = static_cast<const unsigned char*>(info.pInitialData);
            assert(data[0] == 32 && data[4] == 1 && data[8] == 2 && data[9] == 16);
            assert(data[32] == 0xAB);
        }
        return {info.initialDataSize};
    }
};
constexpr std::array<char,8> VULKAN_CACHE_MAGIC_NUMBER{'y','u','z','u','v','k','c','h'};
struct PipelineCache {
    Device device;
    vk::PipelineCache LoadVulkanPipelineCache(const std::filesystem::path&, u32);
};
""" + helper + reader + r"""
using Header = std::array<unsigned char,32>;
void write_blob(const std::filesystem::path& file, Header header, bool body=true) {
    std::ofstream out(file, std::ios::binary | std::ios::trunc);
    const u32 version=19;
    out.write(VULKAN_CACHE_MAGIC_NUMBER.data(),8);
    out.write(reinterpret_cast<const char*>(&version),4);
    if (body) {
        out.write(reinterpret_cast<const char*>(header.data()),header.size());
        out.put(static_cast<char>(0xAB));
    }
}
int main() {
    const std::filesystem::path file="driver.bin";
    PipelineCache cache;
    Header valid{};
    valid[0]=32; valid[4]=1; valid[8]=2; valid[9]=16;
    valid[12]=0x34; valid[13]=0x12;
    std::copy(std::begin(cache.device.properties.pipelineCacheUUID),
              std::end(cache.device.properties.pipelineCacheUUID), valid.begin()+16);
    assert(cache.LoadVulkanPipelineCache(file,19).size==0);
    write_blob(file,valid,false);
    assert(cache.LoadVulkanPipelineCache(file,19).size==0);
    for (const size_t index : {0u,4u,8u,12u,31u}) {
        Header wrong=valid; wrong[index]^=1;
        write_blob(file,wrong);
        unsigned before=cache.device.calls;
        assert(cache.LoadVulkanPipelineCache(file,19).size==0);
        assert(cache.device.calls==before+1);
        assert(std::filesystem::exists(file));
    }
    write_blob(file,valid);
    assert(cache.LoadVulkanPipelineCache(file,19).size==33);
    cache.device.reject_nonempty=true;
    unsigned before=cache.device.calls;
    assert(cache.LoadVulkanPipelineCache(file,19).size==0);
    assert(cache.device.calls==before+2);
    cache.device.reject_nonempty=false;
    std::filesystem::resize_file(file,12+ kNativeDriverCacheStagingLimit+1);
    before=cache.device.calls;
    assert(cache.LoadVulkanPipelineCache(file,19).size==0);
    assert(cache.device.calls==before+1 && std::filesystem::exists(file));
    std::filesystem::remove(file);
}
"""
with tempfile.TemporaryDirectory(prefix="driver-cache-native-") as tmp:
    work = Path(tmp)
    file = work / "cache.cpp"
    file.write_text(cpp)
    for opt in ("-O0", "-O2"):
        exe = work / "cache"
        subprocess.run(["c++", "-std=c++20", opt, "-fsanitize=undefined",
                        "-fno-sanitize-recover=all", str(file), "-o", str(exe)],
                       check=True, cwd=work, timeout=60)
        subprocess.run([str(exe)], check=True, cwd=work, timeout=30)
print("NATIVE_DRIVER_CACHE_HEADER_HOST_PASS valid=1 mismatch=5 oversized_sparse=1 driver_reject=1 O0=1 O2=1 mock_driver=1")
