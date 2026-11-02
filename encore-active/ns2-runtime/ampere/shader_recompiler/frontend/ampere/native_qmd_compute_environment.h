// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Native Ampere QMD3 Compute metadata for the reviewed Maxwell/SPIR-V bridge.
// No Switch 1 Kepler launch-descriptor reinterpretation: those legacy fields
// truncate NS2 constant-buffer addresses above 40 bits and local memory sizes.
#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <stdexcept>
#include <utility>

#include "shader_recompiler/environment.h"
#include "video_core/native_qmd.h"

namespace Shader::Ampere {

// The reader belongs to the currently authenticated GMMU/guest process.
// It MUST return false for an unmapped or partially mapped 32-bit word.
using NativeQmdRead = bool (*)(void* context, u64 gpu_addr,
                               void* destination, std::size_t bytes);

class NativeQmdComputeEnvironment final : public Shader::Environment {
public:
    NativeQmdComputeEnvironment(
        Tegra::NativeComputeLaunch launch_,
        NativeQmdRead read_, void* context_,
        std::unique_ptr<Shader::Environment> texture_source_)
        : launch{std::move(launch_)}, read{read_}, context{context_},
          texture_source{std::move(texture_source_)} {
        stage = Shader::Stage::Compute;
        // Native code has no guest graphics SPH; NativeMaxwellEnvironment
        // supplies its own reviewed synthetic prefix after shader capture.
        start_address = 0;
        is_proprietary_driver = true;
        if (!read || !texture_source ||
            texture_source->ShaderStage() != Shader::Stage::Compute ||
            !launch.program_address ||
            launch.program_address >= (u64{1} << 49))
            throw std::invalid_argument{"invalid native QMD Compute environment"};
        for (const auto dim : launch.block)
            if (!dim) throw std::invalid_argument{"zero native Compute block dimension"};
        for (const auto dim : launch.grid)
            if (!dim) throw std::invalid_argument{"zero native Compute grid dimension"};
    }

    [[nodiscard]] u64 ReadInstruction(u32) override {
        // Native 128-bit Ampere instructions are never Maxwell words.
        // Only NativeMaxwellEnvironment may supply the translated image.
        throw std::logic_error{"raw QMD3 instruction cannot enter Maxwell decoder"};
    }

    [[nodiscard]] u32 ReadCbufValue(u32 index, u32 offset) override {
        if (index >= launch.constant_buffers.size() || (offset & 3U))
            throw std::out_of_range{"invalid native Compute CBUF word"};
        const auto& cbuf = launch.constant_buffers[index];
        if (!cbuf.enabled || !cbuf.size || offset > cbuf.size ||
            sizeof(u32) > cbuf.size - offset)
            throw std::out_of_range{"native Compute CBUF is disabled or too short"};
        constexpr u64 limit = u64{1} << 49;
        if (cbuf.gpu_address >= limit ||
            offset > limit - cbuf.gpu_address ||
            sizeof(u32) > limit - cbuf.gpu_address - offset)
            throw std::out_of_range{"native Compute CBUF crosses GPU VA limit"};
        u32 value{};
        if (!read(context, cbuf.gpu_address + offset, &value, sizeof(value)))
            throw std::out_of_range{"native Compute CBUF GMMU is unmapped"};
        return value;
    }

    [[nodiscard]] Shader::TextureType ReadTextureType(u32 handle) override {
        return texture_source->ReadTextureType(handle);
    }
    [[nodiscard]] Shader::TexturePixelFormat ReadTexturePixelFormat(u32 handle) override {
        return texture_source->ReadTexturePixelFormat(handle);
    }
    [[nodiscard]] bool IsTexturePixelFormatInteger(u32 handle) override {
        return texture_source->IsTexturePixelFormatInteger(handle);
    }
    [[nodiscard]] u32 ReadViewportTransformState() override {
        return texture_source->ReadViewportTransformState();
    }
    [[nodiscard]] u32 TextureBoundBuffer() const override {
        return texture_source->TextureBoundBuffer();
    }
    [[nodiscard]] u32 LocalMemorySize() const override {
        return launch.local_pos_bytes + launch.local_crs_bytes;
    }
    [[nodiscard]] u32 SharedMemorySize() const override {
        return launch.shared_bytes;
    }
    [[nodiscard]] std::array<u32, 3> WorkgroupSize() const override {
        return launch.block;
    }
    [[nodiscard]] bool HasHLEMacroState() const override {
        return texture_source->HasHLEMacroState();
    }
    [[nodiscard]] std::optional<Shader::ReplaceConstant> GetReplaceConstBuffer(
        u32 bank, u32 offset) override {
        return texture_source->GetReplaceConstBuffer(bank, offset);
    }
    void Dump(u64, u64) override {
        // Never use the Switch 1 shader dumper on NS2 Ampere code.
    }

    [[nodiscard]] const Tegra::NativeComputeLaunch& Launch() const noexcept {
        return launch;
    }

private:
    Tegra::NativeComputeLaunch launch;
    NativeQmdRead read{};
    void* context{};
    std::unique_ptr<Shader::Environment> texture_source;
};
} // namespace Shader::Ampere
