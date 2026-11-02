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
#include <vector>

#include "shader_recompiler/environment.h"
#include "shader_recompiler/shader_info.h"
#include "video_core/native_qmd.h"

namespace Shader::Ampere {

// The reader belongs to the currently authenticated GMMU/guest process.
// It MUST return false for an unmapped or partially mapped 32-bit word.
using NativeQmdRead = bool (*)(void* context, u64 gpu_addr,
                               void* destination, std::size_t bytes);

// A descriptor plan is NOT a GPU binding: the renderer must still validate
// the targets against the active GMMU and upload/couple Vulkan descriptors.
// The original per-resource layout and access direction are preserved.
struct NativeQmdUniformBinding {
    u32 qmd_index{};
    u64 gpu_address{};
    u32 bytes{};
};
struct NativeQmdStorageBinding {
    u64 gpu_address{};
    u32 bytes{};
    bool is_written{};
    bool known_size{};
};
struct NativeQmdResourcePlan {
    std::vector<NativeQmdUniformBinding> uniform_buffers;
    std::vector<NativeQmdStorageBinding> storage_buffers;
    bool requires_tic_tsc{};
    bool requires_memory_layout{};
    bool uses_global_memory{};

    // This only validates the CPU-facing descriptor plan. The Vulkan owner
    // MUST still create/upload buffer objects, acquire image views, publish
    // the compute pipeline and fence the dispatch before reporting success.
    [[nodiscard]] bool HasResolvedBufferBindings() const noexcept {
        if (requires_tic_tsc || requires_memory_layout || uses_global_memory)
            return false;
        for (const auto& cbuf : uniform_buffers)
            if (!cbuf.bytes) return false;
        for (const auto& storage : storage_buffers)
            if (!storage.known_size || !storage.bytes || !storage.gpu_address)
                return false;
        return true;
    }

    template <typename IsMapped>
    [[nodiscard]] bool ValidateGpuMappings(IsMapped&& mapped) const {
        if (!HasResolvedBufferBindings()) return false;
        constexpr u64 gpu_limit = u64{1} << 49;
        for (const auto& cbuf : uniform_buffers) {
            if (cbuf.gpu_address >= gpu_limit ||
                cbuf.bytes > gpu_limit - cbuf.gpu_address ||
                !mapped(cbuf.gpu_address, cbuf.bytes))
                return false;
        }
        for (const auto& storage : storage_buffers) {
            if (storage.gpu_address >= gpu_limit ||
                storage.bytes > gpu_limit - storage.gpu_address ||
                !mapped(storage.gpu_address, storage.bytes))
                return false;
        }
        return true;
    }
};

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

    [[nodiscard]] std::optional<NativeQmdResourcePlan> BuildResourcePlan(
        const Shader::Info& shader_info) {
        constexpr u64 gpu_limit = u64{1} << 49;
        NativeQmdResourcePlan result{};
        // The native QMD carries exactly 8 CBUFs; Maxwell's synthetic
        // SPIR-V metadata can address up to 18, but those extra bindings
        // cannot be silently redirected to a Switch 1 constant buffer.
        if ((shader_info.constant_buffer_mask & ~u32{0xff}) != 0 ||
            shader_info.uses_cbuf_indirect)
            return std::nullopt;
        u32 declared_banks{};
        for (const auto& desc : shader_info.constant_buffer_descriptors) {
            if (!desc.count || desc.index >= launch.constant_buffers.size() ||
                desc.count > launch.constant_buffers.size() - desc.index)
                return std::nullopt;
            for (u32 i = 0; i < desc.count; ++i) {
                const auto index = desc.index + i;
                const u32 bit = u32{1} << index;
                const auto& cbuf = launch.constant_buffers[index];
                if ((declared_banks & bit) || !cbuf.enabled || !cbuf.size ||
                    shader_info.constant_buffer_used_sizes[index] > cbuf.size)
                    return std::nullopt;
                declared_banks |= bit;
                result.uniform_buffers.push_back({index, cbuf.gpu_address, cbuf.size});
            }
        }
        // A required shader bank without a matching CBUF descriptor cannot
        // be bound. Reject instead of accepting a partial layout.
        if (shader_info.constant_buffer_mask & ~declared_banks)
            return std::nullopt;
        // Shader SSBO descriptors are 16-byte records inside the QMD CBUF
        // (target VA low, target VA high, packed length, tag). The maintained
        // BufferCache uses that exact guest descriptor layout. Read the same
        // words through our 49-bit, mapping-checked QMD Compute Environment.
        try {
            for (const auto& desc : shader_info.storage_buffers_descriptors) {
                if (desc.count != 1 || desc.cbuf_index >= launch.constant_buffers.size() ||
                    desc.cbuf_offset > 0xfffffff0U)
                    return std::nullopt;
                const u32 lo = ReadCbufValue(desc.cbuf_index, desc.cbuf_offset);
                const u32 hi = ReadCbufValue(desc.cbuf_index, desc.cbuf_offset + 4);
                const u32 bytes = ReadCbufValue(desc.cbuf_index, desc.cbuf_offset + 8);
                const u32 tag = ReadCbufValue(desc.cbuf_index, desc.cbuf_offset + 12);
                if ((hi & ~u32{0x1ffff}) != 0)
                    return std::nullopt;
                const u64 target = (u64{hi} << 32) | lo;
                if (target >= gpu_limit || (bytes && bytes > gpu_limit - target))
                    return std::nullopt;
                // A nonzero tag/zero size uses upstream's memory-layout
                // fallback. Do not invent its length or call it bound.
                const bool known_size = tag == 0 && bytes != 0;
                result.storage_buffers.push_back(
                    {target, known_size ? bytes : 0U, desc.is_written, known_size});
                result.requires_memory_layout |= !known_size;
            }
        } catch (const std::exception&) {
            return std::nullopt;
        }
        // Native TIC/TSC layout, image views and SSBO backing still require
        // the Vulkan texture/buffer caches; a decoded handle is not a bound
        // image or a permission to submit the dispatch.
        result.requires_tic_tsc = !shader_info.texture_descriptors.empty() ||
            !shader_info.texture_buffer_descriptors.empty() ||
            !shader_info.image_descriptors.empty() ||
            !shader_info.image_buffer_descriptors.empty();
        result.uses_global_memory = shader_info.uses_global_memory ||
            shader_info.stores_global_memory;
        return result;
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
