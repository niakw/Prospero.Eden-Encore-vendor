// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Native Ampere QMD3 Compute metadata for the reviewed Maxwell/SPIR-V bridge.
// No Switch 1 Kepler launch-descriptor reinterpretation: those legacy fields
// truncate NS2 constant-buffer addresses above 40 bits and local memory sizes.
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>
#include <optional>
#include <stdexcept>
#include <type_traits>
#include <utility>
#include <vector>

#include "shader_recompiler/environment.h"
#include "shader_recompiler/shader_info.h"
#include "video_core/native_qmd.h"
#include "video_core/textures/texture.h"

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
// Descriptor words, NOT guest TIC/TSC table entries and NOT Vulkan image views.
// Keep Maxwell/Ampere interpretation outside this QMD CBUF reader.
enum class NativeQmdImageKind : std::uint8_t {
    TextureBuffer, ImageBuffer, SampledTexture, StorageImage
};
struct NativeQmdImageHandle {
    NativeQmdImageKind kind{};
    u32 primary_word{};
    u32 secondary_word{};
    u32 primary_shift{};
    u32 secondary_shift{};
    bool has_secondary{};
    bool is_written{};
};
struct NativeQmdResourcePlan {
    std::vector<NativeQmdUniformBinding> uniform_buffers;
    std::vector<NativeQmdStorageBinding> storage_buffers;
    std::vector<NativeQmdImageHandle> image_handles;
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

    // Renderer handoff for the Vulkan BufferCache owner. First preflight
    // EVERY GMMU span; then bind in descriptor-layout order (CBUFs before
    // SSBOs, as ComputePipeline::Configure does). On any refusal discard all
    // staged bindings. No Vulkan command is submitted here, and the caller
    // must recheck the engine epoch before committing/scheduling the QMD.
    template <typename IsMapped, typename BindUniform, typename BindStorage,
              typename Rollback>
    [[nodiscard]] bool StageVulkanBufferBindings(
        IsMapped&& mapped, BindUniform&& bind_uniform,
        BindStorage&& bind_storage, Rollback&& rollback) const {
        if (!ValidateGpuMappings(mapped)) return false;
        bool staged{};
        try {
            u32 descriptor_index{};
            for (const auto& cbuf : uniform_buffers) {
                staged = true;
                if (!bind_uniform(descriptor_index++, cbuf.qmd_index,
                                  cbuf.gpu_address, cbuf.bytes)) {
                    rollback();
                    return false;
                }
            }
            descriptor_index = 0;
            for (const auto& storage : storage_buffers) {
                staged = true;
                if (!bind_storage(descriptor_index++, storage.gpu_address,
                                  storage.bytes, storage.is_written)) {
                    rollback();
                    return false;
                }
            }
            return true;
        } catch (...) {
            if (staged) rollback();
            return false;
        }
    }
};

// Shared NS2 GMMU preflight for real guest buffers and texture image spans.
// GpuToCpuAddress(addr, size) in the older memory manager merely searches for
// ANY mapped page, not full coverage. Check all 4 KiB boundaries and the
// final byte, including physical contiguity. Do not truncate 49-bit VAs.
template <typename Map>
[[nodiscard]] bool NativeQmdContiguousGpuSpan(u64 gpu_addr, u32 bytes,
                                               Map&& map) {
    constexpr u64 gpu_limit = u64{1} << 49;
    constexpr u64 max_bytes = 256ULL * 1024 * 1024;
    if (!bytes || bytes > max_bytes || !gpu_addr ||
        gpu_addr >= gpu_limit || bytes > gpu_limit - gpu_addr)
        return false;
    const auto start = map(gpu_addr);
    if (!start || *start > ~u64{0} - bytes) return false;
    for (u64 delta = 4096 - (gpu_addr & 4095U); delta < bytes; delta += 4096) {
        const auto page = map(gpu_addr + delta);
        if (!page || *page != *start + delta) return false;
    }
    const auto last = map(gpu_addr + bytes - 1);
    return last && *last == *start + bytes - 1;
}

// P2.2: an authenticated GPU channel owns the TIC/TSC table registers.
// Capture actual 32-byte guest entries through the channel's checked GMMU
// reader; never interpret a CBUF handle as an image or sampler by itself.
// No Vulkan descriptors, cache allocations or guest writes occur here.
struct NativeQmdImageTableRecord {
    NativeQmdImageKind kind{};
    u32 raw_handle{};
    u32 tic_index{};
    u32 tsc_index{};
    bool has_sampler{};
    bool is_written{};
    std::array<u32, 8> tic_words{};
    std::array<u32, 8> tsc_words{};
};
struct NativeQmdTextureTables {
    u64 tic_base{};
    u64 tsc_base{};
    u32 tic_limit{};
    u32 tsc_limit{};
    bool linked_tsc{};

    template <typename Read>
    [[nodiscard]] std::optional<std::vector<NativeQmdImageTableRecord>>
    Capture(const NativeQmdResourcePlan& plan, Read&& read) const {
        if (plan.image_handles.empty())
            return plan.requires_tic_tsc
                ? std::nullopt
                : std::optional<std::vector<NativeQmdImageTableRecord>>{
                      std::vector<NativeQmdImageTableRecord>{}};
        if (!plan.requires_tic_tsc || plan.image_handles.size() > 256)
            return std::nullopt;
        constexpr u64 gpu_limit = u64{1} << 49;
        constexpr u64 entry_bytes = 32; // TICEntry and TSCEntry owner ABI
        const auto entry_address = [&](u64 base, u32 limit,
                                       u32 index) -> std::optional<u64> {
            if (!base || base >= gpu_limit || (base & (entry_bytes - 1)) ||
                index > limit)
                return std::nullopt;
            const u64 offset = u64{index} * entry_bytes;
            if (offset > gpu_limit - base ||
                entry_bytes > gpu_limit - base - offset)
                return std::nullopt;
            return base + offset;
        };
        std::vector<NativeQmdImageTableRecord> captured;
        captured.reserve(plan.image_handles.size());
        for (const auto& handle : plan.image_handles) {
            if (handle.primary_shift >= 32 ||
                (handle.has_secondary && handle.secondary_shift >= 32))
                return std::nullopt;
            const u64 primary = u64{handle.primary_word} << handle.primary_shift;
            const u64 secondary = handle.has_secondary
                ? u64{handle.secondary_word} << handle.secondary_shift : 0;
            const u64 raw64 = primary | secondary;
            if (raw64 > 0xffffffffU)
                return std::nullopt; // no silent truncation to a wrong guest handle
            const u32 raw = static_cast<u32>(raw64);
            const u32 tic = linked_tsc ? raw : (raw & 0xfffffU);
            const u32 tsc = linked_tsc ? raw : (raw >> 20);
            const auto tic_addr = entry_address(tic_base, tic_limit, tic);
            if (!tic_addr)
                return std::nullopt;
            NativeQmdImageTableRecord record{};
            record.kind = handle.kind;
            record.raw_handle = raw;
            record.tic_index = tic;
            record.tsc_index = tsc;
            record.is_written = handle.is_written;
            if (!read(*tic_addr, record.tic_words.data(),
                      sizeof(record.tic_words)))
                return std::nullopt;
            record.has_sampler = handle.kind == NativeQmdImageKind::SampledTexture;
            if (record.has_sampler) {
                const auto tsc_addr = entry_address(tsc_base, tsc_limit, tsc);
                if (!tsc_addr ||
                    !read(*tsc_addr, record.tsc_words.data(),
                          sizeof(record.tsc_words)))
                    return std::nullopt;
            }
            captured.push_back(record);
        }
        return captured;
    }

    // The first native admission boundary: validate actual typed TIC/TSC
    // guest records against the same Maxwell descriptor ABI as TextureCache,
    // but use the authenticated Ampere 49-bit GMMU callback for image spans.
    // No image-view allocation, sampler publication or Vulkan submission.
    template <typename Read, typename Admit>
    [[nodiscard]] std::optional<std::vector<NativeQmdImageTableRecord>>
    CaptureAdmissible(const NativeQmdResourcePlan& plan, Read&& read,
                      Admit&& admit_image) const {
        auto records = Capture(plan, std::forward<Read>(read));
        if (!records) return std::nullopt;
        for (const auto& record : *records) {
            Tegra::Texture::TICEntry tic{};
            static_assert(sizeof(tic) == sizeof(record.tic_words));
            std::memcpy(&tic, record.tic_words.data(), sizeof(tic));
            if (static_cast<u32>(tic.header_version.Value()) >
                    static_cast<u32>(Tegra::Texture::TICHeaderVersion::BlockLinearColorKey) ||
                static_cast<u32>(tic.texture_type.Value()) >
                    static_cast<u32>(Tegra::Texture::TextureType::TextureCubeArray) ||
                !tic.Address() || tic.Address() >= (u64{1} << 49) ||
                tic.is_sparse || !tic.Width() || !tic.Height() || !tic.Depth() ||
                tic.res_min_mip_level > tic.res_max_mip_level ||
                tic.res_max_mip_level > tic.max_mip_level ||
                (tic.IsBuffer() != (tic.texture_type == Tegra::Texture::TextureType::Texture1DBuffer)))
                return std::nullopt;
            const auto type = static_cast<u32>(tic.texture_type.Value());
            if (((type == static_cast<u32>(Tegra::Texture::TextureType::Texture2D) ||
                   type == static_cast<u32>(Tegra::Texture::TextureType::Texture2DNoMipmap) ||
                   type == static_cast<u32>(Tegra::Texture::TextureType::TextureCubemap)) &&
                  tic.Depth() != 1) ||
                (type == static_cast<u32>(Tegra::Texture::TextureType::Texture3D) &&
                 tic.BaseLayer() != 0))
                return std::nullopt;
            // Prevent ImageInfo's u32 size calculations from wrapping on a
            // huge guest allocation, before reaching the shared cache.
            const u64 pixel_budget = u64{tic.Width()} * tic.Height() *
                tic.Depth() * (u64{tic.BaseLayer()} + 1);
            // Keep u32 ImageInfo size math below overflow, including
            // amplified MSAA allocations, until native texture lifetime is
            // proven. Large images are refused rather than truncated.
            const u32 sample_mode = static_cast<u32>(tic.msaa_mode.Value());
            if (sample_mode == 7 || sample_mode > 11)
                return std::nullopt;
            const u64 budget = sample_mode == 0
                ? 64ULL * 1024 * 1024 : 4ULL * 1024 * 1024;
            if (pixel_budget > budget)
                return std::nullopt;
            if (record.has_sampler) {
                bool any_sampler_word{};
                for (const u32 word : record.tsc_words) any_sampler_word |= word != 0;
                if (!any_sampler_word) return std::nullopt;
            }
            if (!admit_image(tic)) return std::nullopt;
        }
        return records;
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
                // Flatten 16-byte native QMD3 SSBO arrays in reflection order.
                // Reject a truncated CBUF or a descriptor count above 32.
                if (!desc.count || result.storage_buffers.size() > 32 ||
                    desc.count > 32 - result.storage_buffers.size() ||
                    desc.cbuf_index >= launch.constant_buffers.size() ||
                    (desc.cbuf_offset & 3U) ||
                    desc.cbuf_offset > 0xfffffff0U - 16U * (desc.count - 1U))
                    return std::nullopt;
                for (u32 index = 0; index < desc.count; ++index) {
                    const u32 offset = desc.cbuf_offset + index * 16U;
                    const u32 lo = ReadCbufValue(desc.cbuf_index, offset);
                    const u32 hi = ReadCbufValue(desc.cbuf_index, offset + 4);
                    const u32 bytes = ReadCbufValue(desc.cbuf_index, offset + 8);
                    const u32 tag = ReadCbufValue(desc.cbuf_index, offset + 12);
                    if ((hi & ~u32{0x1ffff}) != 0)
                        return std::nullopt;
                    const u64 target = (u64{hi} << 32) | lo;
                    if (target >= gpu_limit || (bytes && bytes > gpu_limit - target))
                        return std::nullopt;
                    const bool known_size = tag == 0 && bytes != 0;
                    result.storage_buffers.push_back(
                        {target, known_size ? bytes : 0U, desc.is_written, known_size});
                    result.requires_memory_layout |= !known_size;
                }
            }
        } catch (const std::exception&) {
            return std::nullopt;
        }
        // P2.2: read reflected texture handle WORDS through this generation's
        // authenticated, 49-bit QMD CBUF, in the SAME descriptor order as
        // the shared Vulkan ComputePipeline. Do not guess Ampere TIC/TSC
        // table layouts, reinterpret them as Kepler, or invent image views.
        // Every image still fails closed until the cache owns the table and
        // validates formats, mappings, sampler state and GPU lifetime.
        const auto capture_images = [&](const auto& descriptors,
                                        NativeQmdImageKind kind) -> bool {
            for (const auto& desc : descriptors) {
                constexpr u32 max_handles = 256; // preflight resource budget
                if (!desc.count ||
                    result.image_handles.size() > max_handles ||
                    desc.count > max_handles - result.image_handles.size() ||
                    desc.size_shift >= 32 || (desc.cbuf_offset & 3U))
                    return false;
                using Descriptor = std::decay_t<decltype(desc)>;
                constexpr bool has_pair =
                    std::is_same_v<Descriptor, Shader::TextureDescriptor> ||
                    std::is_same_v<Descriptor, Shader::TextureBufferDescriptor>;
                bool separate = false;
                if constexpr (has_pair) {
                    separate = desc.has_secondary;
                    if (desc.shift_left >= 32 ||
                        (separate && (desc.secondary_shift_left >= 32 ||
                                      (desc.secondary_cbuf_offset & 3U))))
                        return false;
                }
                for (u32 index = 0; index < desc.count; ++index) {
                    const u64 stride = u64{index} << desc.size_shift;
                    const u64 first = u64{desc.cbuf_offset} + stride;
                    if (first > u64{0xffffffff} - sizeof(u32) + 1U)
                        return false;
                    NativeQmdImageHandle record{};
                    record.kind = kind;
                    record.primary_word = ReadCbufValue(desc.cbuf_index,
                                                         static_cast<u32>(first));
                    if constexpr (has_pair) {
                        record.primary_shift = desc.shift_left;
                        record.has_secondary = separate;
                        if (separate) {
                            const u64 second =
                                u64{desc.secondary_cbuf_offset} + stride;
                            if (second > u64{0xffffffff} - sizeof(u32) + 1U)
                                return false;
                            record.secondary_word = ReadCbufValue(
                                desc.secondary_cbuf_index, static_cast<u32>(second));
                            record.secondary_shift = desc.secondary_shift_left;
                        }
                    }
                    if constexpr (std::is_same_v<Descriptor, Shader::ImageDescriptor> ||
                                  std::is_same_v<Descriptor, Shader::ImageBufferDescriptor>)
                        record.is_written = desc.is_written;
                    result.image_handles.push_back(record);
                }
            }
            return true;
        };
        try {
            if (!capture_images(shader_info.texture_buffer_descriptors,
                                NativeQmdImageKind::TextureBuffer) ||
                !capture_images(shader_info.image_buffer_descriptors,
                                NativeQmdImageKind::ImageBuffer) ||
                !capture_images(shader_info.texture_descriptors,
                                NativeQmdImageKind::SampledTexture) ||
                !capture_images(shader_info.image_descriptors,
                                NativeQmdImageKind::StorageImage))
                return std::nullopt;
        } catch (const std::exception&) {
            return std::nullopt; // unmapped/truncated CBUF must not dispatch
        }
        result.requires_tic_tsc = !result.image_handles.empty();
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
