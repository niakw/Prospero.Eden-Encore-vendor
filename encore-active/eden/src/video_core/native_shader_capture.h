// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
// Actual Tegra GMMU-based reader for the isolated NS2 shader capture.
// This is compiled only for opt-in research, not hooked to shipping renderer.
#include <memory>
#include <cstddef>
#include <optional>
#include "shader_recompiler/frontend/ampere/capture.h"
namespace Shader { class Environment; }
namespace Tegra { class MemoryManager; namespace Engines { class Maxwell3D; } }
namespace VideoCommon {
// Stage-owned native CBUF snapshot; unlike legacy ReadCbufValue,
// an unmapped or out-of-range word is not silently replaced by zero.
struct Ns2GraphicsCbufContext {
    Tegra::MemoryManager* memory{};
    Tegra::Engines::Maxwell3D* engine{};
    u32 stage_index{};
};
[[nodiscard]] std::optional<u32> ReadNs2GraphicsCbuf(
    void* context, u32 bank, u32 offset);

// Same bounded GMMU mapping-checking reader used by production and host tests.
[[nodiscard]] bool ReadNs2GpuShader(void* context, u64 address, void* dest, std::size_t size);
[[nodiscard]] Shader::Ampere::NativeCaptureResult CaptureNs2GraphicsShaderFromGpu(
    Tegra::MemoryManager& memory, u64 gpu_program_address, u32 stage,
    Shader::Ampere::NativeTranslationCache* cache = nullptr,
    const Ns2GraphicsCbufContext* cbuf = nullptr);

// Explicit opt-in bridge from verified guest GMMU bytes into the actual
// Shader::Environment virtual interface used by the existing Maxwell CFG.
// Never auto-detect by title name, GPU address or Switch 1 shader extension.
[[nodiscard]] std::unique_ptr<Shader::Environment> CreateNs2GraphicsShaderEnvironment(
    Tegra::MemoryManager& memory, std::unique_ptr<Shader::Environment> original,
    u64 gpu_program_address, u32 stage,
    Shader::Ampere::NativeTranslationCache* cache = nullptr,
    const Ns2GraphicsCbufContext* cbuf = nullptr);
} // namespace VideoCommon
