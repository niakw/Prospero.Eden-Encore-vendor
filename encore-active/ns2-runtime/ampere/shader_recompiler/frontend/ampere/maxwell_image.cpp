// SPDX-License-Identifier: GPL-3.0-or-later
#include "shader_recompiler/frontend/ampere/maxwell_image.h"
#include <limits>
#include "shader_recompiler/frontend/ampere/program_address_map.h"

namespace Shader::Ampere {
namespace {
constexpr u32 MaxwellHeaderSize = 0x50;
constexpr u32 MaxwellSchedulingPeriod = 0x20;
constexpr u64 ImageSalt = 0x4e53325053354d31ULL; // NS2PS5M1 (versioned)
constexpr u64 FnvPrime = 0x100000001b3ULL;

[[nodiscard]] u64 ImageKey(std::span<const u8> header,
                           std::span<const u8> native, u32 stage) {
    // The key is deliberately distinct from Switch 1 shader cache keys.
    // This is a diagnostic identity, not yet wired into disk-cache lookup.
    u64 h = 0xcbf29ce484222325ULL ^ ImageSalt;
    const auto mix = [&h](u8 b) { h = (h ^ b) * FnvPrime; };
    for (u32 shift = 0; shift < 64; shift += 8)
        mix(static_cast<u8>(ImageSalt >> shift));
    for (u32 shift = 0; shift < 32; shift += 8)
        mix(static_cast<u8>(stage >> shift));
    for (u32 shift = 0; shift < 32; shift += 8)
        mix(static_cast<u8>(native.size() >> shift));
    for (u8 byte : header) mix(byte);
    for (u8 byte : native) mix(byte);
    return h;
}
} // namespace

bool MaxwellShaderImage::ValidForCompiler() const {
    if (!Ready() || first_instruction_address != MaxwellHeaderSize ||
        instruction_end_address <= first_instruction_address ||
        instruction_end_address % sizeof(u64) ||
        code.size() > std::numeric_limits<u32>::max() / sizeof(u64) ||
        !HasNativeAddressMap(code) || code.size() < 5) return false;
    const u64 count = code[code.size() - 2];
    if (!count || count > 4096 || count + 3 > code.size()) return false;
    const auto map_start = code.size() - static_cast<std::size_t>(count) - 2;
    if (map_start != static_cast<std::size_t>(instruction_end_address / sizeof(u64)) + 1 ||
        cached_highest_address != (code.size() - 1) * sizeof(u64) ||
        code[map_start - 1] != 0) return false;
    u64 previous = 0;
    for (std::size_t i = map_start; i < code.size() - 2; ++i) {
        const u64 address = code[i];
        if (address < first_instruction_address ||
            address >= instruction_end_address ||
            address % sizeof(u64) || address % MaxwellSchedulingPeriod == 0 ||
            address < previous) return false;
        previous = address;
    }
    return true;
}

std::optional<u64> MaxwellShaderImage::ReadMaxwellInstruction(u32 addr) const {
    if (!Ready() || addr < first_instruction_address ||
        addr >= instruction_end_address || (addr & 7) ||
        (addr % MaxwellSchedulingPeriod) == 0 ||
        addr / sizeof(u64) >= code.size())
        return {};
    return code[addr / sizeof(u64)];
}

std::optional<u32> MaxwellShaderImage::TranslateNativeAddress(u32 address) const {
    if (!Ready() || !HasNativeAddressMap(code)) return {};
    const auto target = NativeProgramAddress(code, address);
    if (!target || !ReadMaxwellInstruction(*target)) return {};
    return target;
}

MaxwellShaderImage BuildMaxwellShaderImage(
    std::span<const u8> maxwell_header, std::span<const u8> native_code,
    u32 shader_stage) {
    MaxwellShaderImage image;
    if (maxwell_header.size() != MaxwellHeaderSize) return image;
    image.status = MaxwellImageStatus::LoweringFailed;
    auto lowered = LowerReviewedToMaxwell(native_code);
    image.lowering_status = lowered.status;
    image.failed_native_offset = lowered.failed_native_byte_offset;
    if (!lowered.Ready()) return image;

    // Maxwell 3-instruction groups carry an 8-byte scheduling word before
    // the following group. Reproduce the actual shader_environment ABI.
    std::vector<u64> code(MaxwellHeaderSize / sizeof(u64), 0);
    for (u32 i = 0; i != maxwell_header.size(); ++i) {
        code[i / sizeof(u64)] |= u64{maxwell_header[i]} << ((i % 8) * 8);
    }
    for (u64 instruction : lowered.maxwell_instructions) {
        if ((code.size() * sizeof(u64)) % MaxwellSchedulingPeriod == 0)
            code.push_back(0);
        code.push_back(instruction);
    }
    const auto instruction_end = code.size() * sizeof(u64);
    // Existing environment appends one unreachable padding word then the
    // map. Never hand a schedule slot / map entry to the instruction reader.
    code.push_back(0);
    AppendNativeAddressMap(code, lowered.source_instruction_offsets);
    if (code.size() > std::numeric_limits<u32>::max() / sizeof(u64))
        return MaxwellShaderImage{MaxwellImageStatus::InvalidLayout};

    const auto maxwell_end = static_cast<u32>(instruction_end);
    for (u32 source = 0; source < lowered.source_instruction_offsets.size(); ++source) {
        const u32 index = lowered.source_instruction_offsets[source];
        const u64 absolute = 0x50ULL + u64{index} * 8ULL +
                             u64{(index + 1) / 3} * 8ULL;
        if (absolute >= maxwell_end || (absolute % 32) == 0)
            return MaxwellShaderImage{MaxwellImageStatus::InvalidLayout};
    }

    image.status = MaxwellImageStatus::Ready;
    image.consumed_native_bytes = lowered.consumed_native_bytes;
    image.instruction_end_address = maxwell_end;
    image.cached_highest_address =
        static_cast<u32>((code.size() - 1) * sizeof(u64));
    image.diagnostic_cache_key = ImageKey(maxwell_header, native_code, shader_stage);
    image.native_stage = shader_stage;
    image.code = std::move(code);
    for (u32 source = 0; source < lowered.source_instruction_offsets.size(); ++source) {
        const auto target = image.TranslateNativeAddress(source * 16);
        if (!target) return MaxwellShaderImage{MaxwellImageStatus::InvalidLayout};
    }
    if (!image.ValidForCompiler()) return MaxwellShaderImage{MaxwellImageStatus::InvalidLayout};
    return image;
}
} // namespace Shader::Ampere
