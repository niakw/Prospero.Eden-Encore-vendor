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
    // Buffer descriptors originate from writable guest CBUF memory.
    // Compare the complete captured binding plan, not only its mapping:
    // a guest rewrite can keep both old and new GPU ranges resident.
    // No hashing/padding comparisons or allocations in this validation.
    [[nodiscard]] bool SameBufferBindings(const NativeQmdResourcePlan& other) const noexcept {
        if (requires_tic_tsc != other.requires_tic_tsc ||
            requires_memory_layout != other.requires_memory_layout ||
            uses_global_memory != other.uses_global_memory ||
            uniform_buffers.size() != other.uniform_buffers.size() ||
            storage_buffers.size() != other.storage_buffers.size())
            return false;
        for (std::size_t i = 0; i < uniform_buffers.size(); ++i) {
            const auto& a = uniform_buffers[i];
            const auto& b = other.uniform_buffers[i];
            if (a.qmd_index != b.qmd_index || a.gpu_address != b.gpu_address ||
                a.bytes != b.bytes)
                return false;
        }
        for (std::size_t i = 0; i < storage_buffers.size(); ++i) {
            const auto& a = storage_buffers[i];
            const auto& b = other.storage_buffers[i];
            if (a.gpu_address != b.gpu_address || a.bytes != b.bytes ||
                a.is_written != b.is_written || a.known_size != b.known_size)
                return false;
        }
        return true;
    }

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

// Match the original Vulkan ComputePipeline::Configure image flattening:
// texture buffers, image buffers, sampled textures, then storage images.
// Require every QMD image handle to correspond to a reflected SPIR-V slot,
// preserving access direction and count; never silently reorder or truncate.
[[nodiscard]] inline bool NativeQmdImageLayoutMatches(
    const Shader::Info& info, const NativeQmdResourcePlan& plan) {
    constexpr std::size_t max_slots = 256;
    if (plan.image_handles.size() > max_slots ||
        plan.requires_tic_tsc != !plan.image_handles.empty())
        return false;
    std::size_t position{};
    const auto matches = [&](const auto& descriptions,
                             NativeQmdImageKind expected) {
        for (const auto& desc : descriptions) {
            if (!desc.count || desc.count > plan.image_handles.size() - position)
                return false;
            bool writable{};
            using Descriptor = std::remove_cvref_t<decltype(desc)>;
            if constexpr (std::is_same_v<Descriptor, Shader::ImageBufferDescriptor> ||
                          std::is_same_v<Descriptor, Shader::ImageDescriptor>)
                writable = desc.is_written;
            for (u32 i = 0; i < desc.count; ++i) {
                const auto& handle = plan.image_handles[position++];
                if (handle.kind != expected || handle.is_written != writable)
                    return false;
            }
        }
        return true;
    };
    return matches(info.texture_buffer_descriptors,
                   NativeQmdImageKind::TextureBuffer) &&
           matches(info.image_buffer_descriptors,
                   NativeQmdImageKind::ImageBuffer) &&
           matches(info.texture_descriptors,
                   NativeQmdImageKind::SampledTexture) &&
           matches(info.image_descriptors,
                   NativeQmdImageKind::StorageImage) &&
           position == plan.image_handles.size();
}

// Actual GPU retirement, independent of guest fence callback policy.
// GenericFenceManager::SignalFence may call its callback synchronously;
// SyncOperation instead runs only when the pending operations fence is
// retired. Always submit the recorded Vulkan work first and wait its exact
// timeline tick inside the deferred operation. The shared owner stays alive
// through the end of the wait even if the guest fence is stubbed.
template <typename Scheduler, typename FenceManager, typename Owner>
void RetainNativeComputeUntilGpuTick(Scheduler& scheduler,
                                     FenceManager& fences,
                                     std::shared_ptr<Owner> owner) {
    // Keep an independent reference until registration succeeds. If a
    // std::function allocation or guest-fence submission throws after the
    // Vulkan work was submitted, the registration lambda may be destroyed
    // before the GPU finishes. The fallback waits that exact submitted tick.
    auto emergency_owner = owner;
    const u64 tick = scheduler.Flush();
    try {
        fences.SyncOperation([owner = std::move(owner), tick,
                              gpu_scheduler = &scheduler] {
            gpu_scheduler->Wait(tick);
            static_cast<void>(owner);
        });
        fences.SignalReference();
    } catch (...) {
        scheduler.Wait(tick);
        static_cast<void>(emergency_owner);
        throw;
    }
}

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

// Check a multi-dimensional guest texture budget without overflowing u64.
// TIC 1D buffers can expose 32-bit widths, and a malformed layered descriptor
// can otherwise wrap an unchecked width*height*depth*layers product.
[[nodiscard]] constexpr bool NativeQmdWithinTexelBudget(
    u64 width, u64 height, u64 depth, u64 layers, u64 budget) noexcept {
    if (!budget) return false;
    for (const u64 dim : std::array<u64, 4>{width, height, depth, layers}) {
        if (!dim || dim > budget) return false;
        budget /= dim;
    }
    return true;
}

// Decode a sampler only after complete 32-byte TSC capture. The shared
// Vulkan sampler creator asserts on unrecognized filter/reduction values;
// reject malformed guest records instead of creating partial GPU objects.
[[nodiscard]] constexpr bool NativeQmdSamplerSupported(
    const Tegra::Texture::TSCEntry& sampler) noexcept {
    using namespace Tegra::Texture;
    const auto filter_ok = [](TextureFilter value) {
        return value == TextureFilter::Nearest || value == TextureFilter::Linear;
    };
    const auto mip_ok = [](TextureMipmapFilter value) {
        return value == TextureMipmapFilter::None ||
               value == TextureMipmapFilter::Nearest ||
               value == TextureMipmapFilter::Linear;
    };
    const auto reduction = sampler.reduction_filter.Value();
    // MaxwellToVK::Sampler::WrapMode marks MirrorOnceBorder unimplemented.
    // Its fallback is not an authentic guest sampler interpretation.
    const auto wrap_ok = [](WrapMode value) {
        return value != WrapMode::MirrorOnceBorder;
    };
    return wrap_ok(sampler.wrap_u.Value()) &&
           wrap_ok(sampler.wrap_v.Value()) &&
           wrap_ok(sampler.wrap_p.Value()) &&
           filter_ok(sampler.mag_filter.Value()) &&
           filter_ok(sampler.min_filter.Value()) &&
           mip_ok(sampler.mipmap_filter.Value()) &&
           (reduction == SamplerReduction::WeightedAverage ||
            reduction == SamplerReduction::Min ||
            reduction == SamplerReduction::Max);
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
// Immutable, typed guest descriptors after CaptureAdmissible. These are
// metadata ONLY: no live TextureCache slot, VkImageView, VkSampler or GPU
// lifetime is acquired by decoding a snapshot.
struct NativeQmdTypedTextureRecord {
    NativeQmdImageKind kind{};
    u32 tic_index{};
    u32 tsc_index{};
    bool is_written{};
    Tegra::Texture::TICEntry tic{};
    std::optional<Tegra::Texture::TSCEntry> sampler{};
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

    // A channel ticket only guards channel ownership. TIC/TSC are guest
    // memory and can change independently while images are being validated.
    // Compare typed fields and complete raw records, never padding bytes.
    // This is a pre-publication check, not a GPU fence or ownership token.
    template <typename Read>
    [[nodiscard]] bool MatchesSnapshot(
        const NativeQmdResourcePlan& plan,
        const std::vector<NativeQmdImageTableRecord>& snapshot,
        Read&& read) const {
        const auto fresh = Capture(plan, std::forward<Read>(read));
        if (!fresh || fresh->size() != snapshot.size()) return false;
        for (std::size_t i = 0; i < snapshot.size(); ++i) {
            const auto& a = snapshot[i];
            const auto& b = (*fresh)[i];
            if (a.kind != b.kind || a.raw_handle != b.raw_handle ||
                a.tic_index != b.tic_index || a.tsc_index != b.tsc_index ||
                a.has_sampler != b.has_sampler || a.is_written != b.is_written ||
                a.tic_words != b.tic_words || a.tsc_words != b.tsc_words)
                return false;
        }
        return true;
    }

    // Transcode already admitted snapshot bytes to the EXACT shared cache
    // TICEntry/TSCEntry ABI. Keep every entry on the stack/vector, then
    // check table equality again under the active channel cache lock.
    // No cached image or Vulkan descriptor is created here.
    [[nodiscard]] std::optional<std::vector<NativeQmdTypedTextureRecord>>
    DecodeAdmitted(const NativeQmdResourcePlan& plan,
                   const std::vector<NativeQmdImageTableRecord>& records) const {
        if (plan.image_handles.size() != records.size() ||
            records.size() > 256 ||
            plan.requires_tic_tsc != !records.empty())
            return std::nullopt;
        std::vector<NativeQmdTypedTextureRecord> typed;
        typed.reserve(records.size());
        for (std::size_t index = 0; index < records.size(); ++index) {
            const auto& src = records[index];
            const auto& wanted = plan.image_handles[index];
            const bool sampled = wanted.kind == NativeQmdImageKind::SampledTexture;
            if (src.kind != wanted.kind || src.is_written != wanted.is_written ||
                src.has_sampler != sampled)
                return std::nullopt;
            // A captured 32-byte TIC/TSC snapshot belongs to one exact
            // reflected QMD handle, not merely a descriptor of the same
            // texture kind. Refuse a stale/mixed plan before decoding it
            // into the shared TextureCache ABI.
            if (wanted.primary_shift >= 32 ||
                (wanted.has_secondary && wanted.secondary_shift >= 32))
                return std::nullopt;
            const u64 primary = u64{wanted.primary_word} << wanted.primary_shift;
            const u64 secondary = wanted.has_secondary
                ? u64{wanted.secondary_word} << wanted.secondary_shift : 0;
            const u64 raw64 = primary | secondary;
            if (raw64 > 0xffffffffU) return std::nullopt;
            const u32 raw = static_cast<u32>(raw64);
            if (src.raw_handle != raw ||
                src.tic_index != (linked_tsc ? raw : (raw & 0xfffffU)) ||
                src.tsc_index != (linked_tsc ? raw : (raw >> 20)))
                return std::nullopt;
            NativeQmdTypedTextureRecord entry{};
            entry.kind = src.kind;
            entry.tic_index = src.tic_index;
            entry.tsc_index = src.tsc_index;
            entry.is_written = src.is_written;
            static_assert(sizeof(entry.tic) == sizeof(src.tic_words));
            std::memcpy(&entry.tic, src.tic_words.data(), sizeof(entry.tic));
            if (!entry.tic.Address() || entry.tic.Address() >= (u64{1} << 49))
                return std::nullopt;
            if (sampled) {
                Tegra::Texture::TSCEntry sampler{};
                static_assert(sizeof(sampler) == sizeof(src.tsc_words));
                std::memcpy(&sampler, src.tsc_words.data(), sizeof(sampler));
                if (!NativeQmdSamplerSupported(sampler))
                    return std::nullopt;
                entry.sampler = sampler;
            }
            typed.emplace_back(std::move(entry));
        }
        return typed;
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
            // Keep u32 ImageInfo size math below overflow, including
            // amplified MSAA allocations, until native texture lifetime is
            // proven. Refuse unrecognized MSAA modes rather than sending
            // them to MaxwellToVK::MsaaMode's fallback assertion.
            const auto sample_mode = tic.msaa_mode.Value();
            using Tegra::Texture::MsaaMode;
            switch (sample_mode) {
            case MsaaMode::Msaa1x1:
            case MsaaMode::Msaa2x1:
            case MsaaMode::Msaa2x2:
            case MsaaMode::Msaa4x2:
            case MsaaMode::Msaa4x2_D3D:
            case MsaaMode::Msaa2x1_D3D:
            case MsaaMode::Msaa4x4:
            case MsaaMode::Msaa2x2_VC4:
            case MsaaMode::Msaa2x2_VC12:
            case MsaaMode::Msaa4x2_VC8:
            case MsaaMode::Msaa4x2_VC24:
                break;
            default:
                return std::nullopt;
            }
            const u64 budget = sample_mode == MsaaMode::Msaa1x1
                ? 64ULL * 1024 * 1024 : 4ULL * 1024 * 1024;
            if (!NativeQmdWithinTexelBudget(
                    tic.Width(), tic.Height(), tic.Depth(),
                    u64{tic.BaseLayer()} + 1, budget))
                return std::nullopt;
            if (record.has_sampler) {
                // A nonzero TSC is not necessarily a supported Vulkan
                // sampler. Decode the same TSCEntry ABI as TextureCache
                // and verify the filter/reduction enums before allocation.
                Tegra::Texture::TSCEntry sampler{};
                static_assert(sizeof(sampler) == sizeof(record.tsc_words));
                std::memcpy(&sampler, record.tsc_words.data(), sizeof(sampler));
                if (!NativeQmdSamplerSupported(sampler))
                    return std::nullopt;
            }
            if (!admit_image(tic)) return std::nullopt;
        }
        // Texture layout probing can take time. Detect guest table writes
        // during that interval before the renderer accepts the plan.
        if (!MatchesSnapshot(plan, *records, read)) return std::nullopt;
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
