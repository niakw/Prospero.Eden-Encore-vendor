// SPDX-License-Identifier: GPL-3.0-or-later
#include "shader_recompiler/frontend/ampere/maxwell_environment.h"
#include <stdexcept>
#include <utility>

namespace Shader::Ampere {
namespace {
// Metadata is consulted independently by Maxwell IR lowering. Never combine
// instructions captured from one GPU header with stage/CBUF context from
// another shader, including after GMMU remapping at the same guest VA.
[[nodiscard]] bool MatchesCapturedHeader(const Shader::Environment& source,
                                          const MaxwellShaderImage& image) {
    constexpr std::size_t header_bytes = sizeof(Shader::ProgramHeader);
    static_assert(header_bytes == 0x50);
    if (image.code.size() < header_bytes / sizeof(u64)) return false;
    const auto* raw = reinterpret_cast<const unsigned char*>(&source.SPH());
    for (std::size_t i = 0; i < header_bytes; ++i) {
        if (raw[i] != static_cast<unsigned char>(
                image.code[i / sizeof(u64)] >> (8 * (i % sizeof(u64)))))
            return false;
    }
    return true;
}
} // namespace

NativeMaxwellEnvironment::NativeMaxwellEnvironment(
    std::unique_ptr<Shader::Environment> owned_source,
    Shader::Environment& source, MaxwellShaderImage translated)
    : owned{std::move(owned_source)}, original{source}, image{std::move(translated)} {
    // Feed the existing Maxwell CFG its expected SPH, stage, passthrough
    // mask and first real 64-bit instruction address (after 0x50 SPH).
    sph = source.SPH();
    stage = source.ShaderStage();
    gp_passthrough_mask = source.GpPassthroughMask();
    // Graphics PipelineCache adds sizeof(ProgramHeader) to StartAddress().
    // The image already contains the 0x50-byte header; avoid a double
    // offset that would begin Maxwell CFG at 0xA0 instead of 0x50.
    static_assert(sizeof(Shader::ProgramHeader) == 0x50);
    start_address = image.first_instruction_address - sizeof(Shader::ProgramHeader);
    is_proprietary_driver = source.IsProprietaryDriver();
}
std::unique_ptr<NativeMaxwellEnvironment> NativeMaxwellEnvironment::Create(
    Shader::Environment& source, MaxwellShaderImage image) {
    if (!image.ValidForCompiler() ||
        source.ShaderStage() == Shader::Stage::Compute ||
        static_cast<u32>(source.ShaderStage()) > static_cast<u32>(Shader::Stage::VertexA) ||
        image.native_stage != static_cast<u32>(source.ShaderStage()) ||
        image.first_instruction_address != 0x50 ||
        !image.ReadMaxwellInstruction(0x50) ||
        !MatchesCapturedHeader(source, image))
        return {};
    return std::unique_ptr<NativeMaxwellEnvironment>(
        new NativeMaxwellEnvironment({}, source, std::move(image)));
}
std::unique_ptr<NativeMaxwellEnvironment> NativeMaxwellEnvironment::CreateOwned(
    std::unique_ptr<Shader::Environment> source, MaxwellShaderImage image) {
    if (!source || !image.ValidForCompiler() ||
        source->ShaderStage() == Shader::Stage::Compute ||
        static_cast<u32>(source->ShaderStage()) > static_cast<u32>(Shader::Stage::VertexA) ||
        image.native_stage != static_cast<u32>(source->ShaderStage()) ||
        image.first_instruction_address != 0x50 ||
        !image.ReadMaxwellInstruction(0x50) ||
        !MatchesCapturedHeader(*source, image)) {
        // Some C++ ABIs destroy by-value parameters at the end of the
        // caller's full expression, not at function return. Release the
        // rejected graphics context here so failure is deterministic.
        source.reset();
        return {};
    }
    auto& ref = *source;
    return std::unique_ptr<NativeMaxwellEnvironment>(
        new NativeMaxwellEnvironment(std::move(source), ref, std::move(image)));
}
u64 NativeMaxwellEnvironment::ReadInstruction(u32 address) {
    // Fail closed instead of falling back to *native* GMMU bytes interpreted
    // as Maxwell instructions. Bounds and schedule slots are enforced by
    // MaxwellShaderImage::ReadMaxwellInstruction.
    auto instruction = image.ReadMaxwellInstruction(address);
    if (!instruction)
        throw std::out_of_range{"NS2 Maxwell environment: invalid shader instruction address"};
    return *instruction;
}
u32 NativeMaxwellEnvironment::ReadCbufValue(u32 index, u32 offset) {
    return original.ReadCbufValue(index, offset);
}
Shader::TextureType NativeMaxwellEnvironment::ReadTextureType(u32 handle) {
    return original.ReadTextureType(handle);
}
Shader::TexturePixelFormat NativeMaxwellEnvironment::ReadTexturePixelFormat(u32 handle) {
    return original.ReadTexturePixelFormat(handle);
}
bool NativeMaxwellEnvironment::IsTexturePixelFormatInteger(u32 handle) {
    return original.IsTexturePixelFormatInteger(handle);
}
u32 NativeMaxwellEnvironment::ReadViewportTransformState() {
    return original.ReadViewportTransformState();
}
u32 NativeMaxwellEnvironment::TextureBoundBuffer() const {
    return original.TextureBoundBuffer();
}
u32 NativeMaxwellEnvironment::LocalMemorySize() const {
    return original.LocalMemorySize();
}
u32 NativeMaxwellEnvironment::SharedMemorySize() const {
    return original.SharedMemorySize();
}
std::array<u32, 3> NativeMaxwellEnvironment::WorkgroupSize() const {
    return original.WorkgroupSize();
}
bool NativeMaxwellEnvironment::HasHLEMacroState() const {
    return original.HasHLEMacroState();
}
std::optional<Shader::ReplaceConstant> NativeMaxwellEnvironment::GetReplaceConstBuffer(
    u32 bank, u32 offset) {
    return original.GetReplaceConstBuffer(bank, offset);
}
void NativeMaxwellEnvironment::Dump(u64, u64) {
    // Explicitly disabled: forwarding would incorrectly dump native guest
    // bytes or assign a Switch 1 cache identity to an NS2 shader.
}
} // namespace Shader::Ampere
