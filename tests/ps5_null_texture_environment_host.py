#!/usr/bin/env python3
"""Exercise the exact maintained TIC fallback and format-19 cache marker on host."""
import pathlib
import re
import subprocess
import tempfile

root = pathlib.Path(__file__).resolve().parents[1]
source = (root / "encore-active/eden-core/src/video_core/shader_environment.cpp").read_text()
enum = (root / "encore-active/eden-shader/src/shader_recompiler/shader_info.h").read_text()
enum = re.search(r"enum class TextureType\s*:\s*u32\s*\{.*?\};", enum, re.S).group()
helpers = source[source.index("constexpr u64 NULL_TEXTURE_2D_KEY"):source.index("static Shader::TextureType ConvertTextureType")]
convert = source[source.index("static Shader::TextureType ConvertTextureType"):source.index("static Shader::TexturePixelFormat ConvertTexturePixelFormat")]
read = source[source.index("Tegra::Texture::TICEntry GenericEnvironment::ReadTextureInfo"):source.index("GraphicsEnvironment::GraphicsEnvironment")]
serialize = source[source.index("void GenericEnvironment::Serialize"):source.index("std::optional<u64> GenericEnvironment::TryFindSize")]
deserialize = source[source.index("void FileEnvironment::Deserialize"):source.index("void FileEnvironment::Dump")]
assert "num_cbuf_values{static_cast<u64>(cbuf_values.size()) + 1}" in serialize
assert serialize.index("WriteNullTextureMarker(file);") < serialize.index("for (const auto& [key, type] : cbuf_replacements)")
assert deserialize.index("RestoreNullTextureMarker(cbuf_values, texture_types, texture_pixel_formats);") < deserialize.index("for (size_t i = 0; i < num_cbuf_replacement_values; ++i)")
with tempfile.TemporaryDirectory() as temp:
    temp = pathlib.Path(temp)
    (temp / "common").mkdir()
    (temp / "common/assert.h").write_text("#pragma once\n#include <cassert>\n#define ASSERT(a) assert(a)\n")
    cpp = r"""
#include <cassert>
#include <cstring>
#include <functional>
#include <sstream>
#include <unordered_map>
#include "video_core/textures/texture.h"
#define LOG_ERROR(...) ((void)0)
namespace Shader {
""" + enum + r"""
}
""" + helpers + convert + r"""
struct Memory {
    Tegra::Texture::TICEntry entry{};
    GPUVAddr address{};
    void ReadBlock(GPUVAddr addr, void* out, size_t size) {
        assert(addr == address && size == sizeof(entry));
        std::memcpy(out, &entry, size);
    }
};
struct GenericEnvironment {
    Memory* gpu_memory;
    Tegra::Texture::TICEntry ReadTextureInfo(GPUVAddr, u32, bool, u32);
};
""" + read + r"""
int main() {
    using namespace Tegra::Texture;
    Memory mem{};
    GenericEnvironment env{&mem};
    mem.address = 0x1000;
    auto entry = env.ReadTextureInfo(0x1000, 4, true, 0);
    assert(ConvertTextureType(entry) == Shader::TextureType::Color2D);
    assert(entry.format.Value() == TextureFormat::A8B8G8R8);
    assert(entry.r_type.Value() == ComponentType::UNORM);
    assert(entry.g_type.Value() == ComponentType::UNORM);
    assert(entry.b_type.Value() == ComponentType::UNORM);
    assert(entry.a_type.Value() == ComponentType::UNORM);
    assert(mem.entry.raw == decltype(mem.entry.raw){});
    mem.entry.format.Assign(TextureFormat::A8B8G8R8);
    mem.address += sizeof(TICEntry);
    entry = env.ReadTextureInfo(0x1000, 4, true, 1);
    assert(entry.raw == mem.entry.raw && ConvertTextureType(entry) == Shader::TextureType::Color1D);
    mem.entry.texture_type.Assign(TextureType::Texture2D);
    entry = env.ReadTextureInfo(0x1000, 4, true, 1);
    assert(entry.raw == mem.entry.raw && ConvertTextureType(entry) == Shader::TextureType::Color2DRect);
    std::stringstream bytes;
    WriteNullTextureMarker(bytes);
    assert(bytes.str().size() == sizeof(u64) + sizeof(u32));
    u64 key{}; u32 value{};
    bytes.read(reinterpret_cast<char*>(&key), sizeof(key)).read(reinterpret_cast<char*>(&value), sizeof(value));
    assert(key == NULL_TEXTURE_2D_KEY && value == 1);
    for (int mode = 0; mode < 3; ++mode) {
        std::unordered_map<u64,u32> cbufs{{12,34}};
        std::unordered_map<u32,Shader::TextureType> types{
            {0,Shader::TextureType::Color1D}, {9,Shader::TextureType::Color1D},
            {2,Shader::TextureType::Color2D}, {3,Shader::TextureType::Buffer}};
        std::unordered_map<u32,u32> formats{{0,1},{9,2},{2,3},{3,4}};
        if (mode) cbufs.emplace(key, mode == 1 ? value : 99);
        RestoreNullTextureMarker(cbufs,types,formats);
        assert(cbufs.size()==1 && cbufs.at(12)==34);
        assert(types.contains(0)==(mode==1) && formats.contains(0)==(mode==1));
        assert(types.contains(9)==(mode==1) && formats.contains(9)==(mode==1));
        assert(types.at(2)==Shader::TextureType::Color2D && formats.at(2)==3);
        assert(types.at(3)==Shader::TextureType::Buffer && formats.at(3)==4);
    }
}
"""
    (temp / "test.cpp").write_text(cpp)
    subprocess.run(["c++", "-std=c++20", "-O2", "-Wall", "-Wextra", "-I", str(temp),
                    "-I", str(root / "sources/eden/src"), str(temp / "test.cpp"),
                    "-o", str(temp / "test")], check=True)
    subprocess.run([str(temp / "test")], check=True)
print("NULL_TIC_HOST_PASS exact selected functions + pinned TIC ABI; live shader and PS5 unverified")
