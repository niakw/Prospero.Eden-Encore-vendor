// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Explicit bridge into the maintained Shader::Environment contract.
// The existing Maxwell control-flow and IR translator can consume this
// interface; only the vetted generated instruction image is exposed.
// Opt-in research source, never selected by normal Switch 1 builds.
#include <memory>
#include "shader_recompiler/environment.h"
#include "shader_recompiler/frontend/ampere/maxwell_image.h"

namespace Shader::Ampere {

class NativeMaxwellEnvironment final : public Shader::Environment {
public:
    [[nodiscard]] static std::unique_ptr<NativeMaxwellEnvironment> Create(
        Shader::Environment& original, MaxwellShaderImage image);
    // Ownership-preserving constructor for deferred CFG/IR compilation.
    // The wrapped source remains alive until the generated environment dies.
    [[nodiscard]] static std::unique_ptr<NativeMaxwellEnvironment> CreateOwned(
        std::unique_ptr<Shader::Environment> original, MaxwellShaderImage image);

    [[nodiscard]] u64 ReadInstruction(u32 address) override;
    [[nodiscard]] u32 ReadCbufValue(u32 index, u32 offset) override;
    [[nodiscard]] Shader::TextureType ReadTextureType(u32 raw_handle) override;
    [[nodiscard]] Shader::TexturePixelFormat ReadTexturePixelFormat(u32 raw_handle) override;
    [[nodiscard]] bool IsTexturePixelFormatInteger(u32 raw_handle) override;
    [[nodiscard]] u32 ReadViewportTransformState() override;
    [[nodiscard]] u32 TextureBoundBuffer() const override;
    [[nodiscard]] u32 LocalMemorySize() const override;
    [[nodiscard]] u32 SharedMemorySize() const override;
    [[nodiscard]] std::array<u32, 3> WorkgroupSize() const override;
    [[nodiscard]] bool HasHLEMacroState() const override;
    [[nodiscard]] std::optional<Shader::ReplaceConstant> GetReplaceConstBuffer(
        u32 bank, u32 offset) override;
    void Dump(u64 pipeline_hash, u64 shader_hash) override;

    [[nodiscard]] const MaxwellShaderImage& Image() const noexcept { return image; }

private:
    NativeMaxwellEnvironment(std::unique_ptr<Shader::Environment> owned,
                             Shader::Environment& original, MaxwellShaderImage image);
    // Keep this declaration before original and image: it owns all
    // delegated texture/CBUF metadata throughout the wrapper lifetime.
    std::unique_ptr<Shader::Environment> owned;
    Shader::Environment& original;
    MaxwellShaderImage image;
};
} // namespace Shader::Ampere
