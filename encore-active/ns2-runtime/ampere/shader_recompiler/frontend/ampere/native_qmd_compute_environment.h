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
            storage_buffers.size() != other.storage_buffers.size() ||
            image_handles.size() != other.image_handles.size())
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
        // The QMD image-handle words also live in mutable guest CBUFs.
        // A rewrite may reuse every mapped buffer address while selecting
        // unrelated TIC/TSC indices, texture types or write permissions.
        // No hash, padding comparison, allocation or cache publication.
        for (std::size_t i = 0; i < image_handles.size(); ++i) {
            const auto& a = image_handles[i];
            const auto& b = other.image_handles[i];
            if (a.kind != b.kind || a.primary_word != b.primary_word ||
                a.secondary_word != b.secondary_word ||
                a.primary_shift != b.primary_shift ||
                a.secondary_shift != b.secondary_shift ||
                a.has_secondary != b.has_secondary ||
                a.is_written != b.is_written)
                return false;
        }
        return true;
    }

    // P2.2b: shaders with TIC/TSC images still require genuine CBUF and
    // SSBO backing. Split their 49-bit GPU-span proof from image publication,
    // which remains forbidden until the Vulkan view/sampler lifetime owner
    // is connected. Never admit indirect CBUF, unknown SSBO or global memory.
    [[nodiscard]] bool HasResolvedGpuBufferBindings() const noexcept {
        if (requires_memory_layout || uses_global_memory)
            return false;
        // Zero is the unmapped/null guest buffer sentinel. Shader CBUFs
        // are required real bindings, not optional descriptors: never
        // delegate a zero address to a permissive mapping callback.
        for (const auto& cbuf : uniform_buffers)
            if (!cbuf.gpu_address || !cbuf.bytes) return false;
        for (const auto& storage : storage_buffers)
            if (!storage.known_size || !storage.bytes || !storage.gpu_address)
                return false;
        return true;
    }

    [[nodiscard]] bool HasResolvedBufferBindings() const noexcept {
        return !requires_tic_tsc && HasResolvedGpuBufferBindings();
    }

    template <typename IsMapped>
    [[nodiscard]] bool ValidateGpuBufferMappings(IsMapped&& mapped) const {
        if (!HasResolvedGpuBufferBindings()) return false;
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

    // The already-published Vulkan BufferCache path is intentionally
    // buffer-only. Do not let this refactor turn an image shader into a
    // successful dispatch before the shared TextureCache owns its views.
    template <typename IsMapped>
    [[nodiscard]] bool ValidateGpuMappings(IsMapped&& mapped) const {
        return !requires_tic_tsc &&
               ValidateGpuBufferMappings(std::forward<IsMapped>(mapped));
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
        BindStorage&& bind_storage, Rollback&& rollback,
        bool image_descriptors_fence_owned = false) const {
        // The original buffer-only policy remains strict. An image shader
        // may stage its independent CBUF/SSBO spans only after the renderer
        // presents real pinned TextureCache image/sampler descriptors.
        // An authenticated GMMU callback may throw while pinning a guest
        // span (e.g. sparse unmap or failed host-memory allocation). Such a
        // failure happens BEFORE any Vulkan descriptor is staged, so return
        // refusal without calling rollback on resources we do not own yet.
        // Keep the existing one-shot rollback for subsequent bind failures.
        bool mappings_valid{};
        try {
            mappings_valid = image_descriptors_fence_owned
                                 ? ValidateGpuBufferMappings(mapped)
                                 : ValidateGpuMappings(mapped);
        } catch (...) {
            return false;
        }
        if (!mappings_valid) return false;
        bool staged{};
        // Rollback is a one-shot operation. A failing cleanup callback must
        // never be re-entered by this function's exception handler, since
        // descriptor owners may already have been released by its first call.
        const auto rollback_once = [&] {
            if (!staged) return;
            staged = false;
            rollback();
        };
        try {
            u32 descriptor_index{};
            for (const auto& cbuf : uniform_buffers) {
                staged = true;
                if (!bind_uniform(descriptor_index++, cbuf.qmd_index,
                                  cbuf.gpu_address, cbuf.bytes)) {
                    rollback_once();
                    return false;
                }
            }
            descriptor_index = 0;
            for (const auto& storage : storage_buffers) {
                staged = true;
                if (!bind_storage(descriptor_index++, storage.gpu_address,
                                  storage.bytes, storage.is_written)) {
                    rollback_once();
                    return false;
                }
            }
            return true;
        } catch (...) {
            // A rollback failure is still a failed staging transaction.
            // It must not trigger a second release of the same descriptors.
            try {
                rollback_once();
            } catch (...) {
            }
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
    u64 tick{};
    try {
        tick = scheduler.Flush();
    } catch (...) {
        // A failed Flush can occur AFTER queuing callbacks which capture
        // ComputePipeline*. Without a timeline tick, the normal fence
        // registration cannot retain that owner. Drain outstanding commands
        // before the emergency shared_ptr leaves scope. Keep the original
        // failure and let the renderer's fatal GPU policy handle a broken
        // scheduler; never pretend this dispatch succeeded.
        try { scheduler.Finish(); } catch (...) {}
        throw;
    }
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

inline constexpr u64 kNativeQmdMaxContiguousBytes = 256ULL * 1024 * 1024;

// Shared NS2 GMMU preflight for real guest buffers and texture image spans.
// GpuToCpuAddress(addr, size) in the older memory manager merely searches for
// ANY mapped page, not full coverage. Check all 4 KiB boundaries and the
// final byte, including physical contiguity. Do not truncate 49-bit VAs.
template <typename Map>
[[nodiscard]] bool NativeQmdContiguousGpuSpan(u64 gpu_addr, u32 bytes,
                                               Map&& map) {
    constexpr u64 gpu_limit = u64{1} << 49;
    if (!bytes || bytes > kNativeQmdMaxContiguousBytes || !gpu_addr ||
        gpu_addr >= gpu_limit || bytes > gpu_limit - gpu_addr)
        return false;
    // A sparse GMMU unmap or host-side memory failure can invalidate a
    // mapping between the first and last checked pages. Refuse the image
    // span without publishing any TextureCache view or leaking exceptions
    // into the render worker. All address/overflow checks above still run.
    try {
        const auto start = map(gpu_addr);
        if (!start || *start > ~u64{0} - bytes) return false;
        for (u64 delta = 4096 - (gpu_addr & 4095U); delta < bytes; delta += 4096) {
            const auto page = map(gpu_addr + delta);
            if (!page || *page != *start + delta) return false;
        }
        const auto last = map(gpu_addr + bytes - 1);
        return last && *last == *start + bytes - 1;
    } catch (...) {
        return false;
    }
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

// The memory manager's CalculateGuestSizeInBytes returns u32 and multiplies
// layer stride, pitch and buffer width without overflow checks. Reject raw
// TIC image extents that exceed the actual 256 MiB Vulkan/GMMU admission
// budget BEFORE doing those narrower multiplications. For a block-linear
// single-layer image the caller supplies its minimum level-zero byte count;
// it must separately check the computed guest size against that lower bound.
[[nodiscard]] constexpr bool NativeQmdWithinByteBudget(
    u64 width_blocks, u64 height_blocks, u64 depth, u64 layers,
    u64 bytes_per_block) noexcept {
    return bytes_per_block &&
           NativeQmdWithinTexelBudget(
               width_blocks, height_blocks, depth, layers,
               kNativeQmdMaxContiguousBytes / bytes_per_block);
}

// Conservative lower bound for EVERY tiled mip level, not only level zero.
// ImageInfo/CalculateLevelOffset still accumulate u32 GOB-aligned sizes.
// A smaller wrapped value cannot authenticate a larger native allocation.
// This checks only layout arithmetic: no image slot or Vulkan handle is created.
[[nodiscard]] constexpr bool NativeQmdMipChainMinimumBytes(
    u64 width, u64 height, u64 depth, u64 layers, u32 levels,
    u64 block_width, u64 block_height, u64 bytes_per_block,
    u64& minimum) noexcept {
    minimum = 0;
    if (!width || !height || !depth || !layers ||
        !levels || levels > 32 || !block_width || !block_height || !bytes_per_block)
        return false;
    for (u32 level = 0; level < levels; ++level) {
        const u64 blocks_x = width / block_width + (width % block_width != 0);
        const u64 blocks_y = height / block_height + (height % block_height != 0);
        if (!NativeQmdWithinByteBudget(
                blocks_x, blocks_y, depth, layers, bytes_per_block))
            return false;
        const u64 level_bytes = blocks_x * blocks_y * depth * layers * bytes_per_block;
        if (level_bytes > kNativeQmdMaxContiguousBytes - minimum)
            return false;
        minimum += level_bytes;
        width = width > 1 ? width >> 1 : 1;
        height = height > 1 ? height >> 1 : 1;
        depth = depth > 1 ? depth >> 1 : 1;
    }
    return true;
}

// The raw guest TIC policy is shared by both initial capture and later
// typed replay. A forged replay must never skip the image-size/MSAA guards:
// neither step creates a Vulkan image, but both can hand metadata to one
// common TextureCache owner. Keep this single admission policy in sync.
[[nodiscard]] inline bool NativeQmdTicLayoutSupported(
    const Tegra::Texture::TICEntry& tic) {
            if (static_cast<u32>(tic.header_version.Value()) >
                    static_cast<u32>(Tegra::Texture::TICHeaderVersion::BlockLinearColorKey) ||
                static_cast<u32>(tic.texture_type.Value()) >
                    static_cast<u32>(Tegra::Texture::TextureType::TextureCubeArray) ||
                !tic.Address() || tic.Address() >= (u64{1} << 49) ||
                tic.is_sparse || !tic.Width() || !tic.Height() || !tic.Depth() ||
                tic.res_min_mip_level > tic.res_max_mip_level ||
                tic.res_max_mip_level > tic.max_mip_level ||
                (tic.IsBuffer() != (tic.texture_type == Tegra::Texture::TextureType::Texture1DBuffer)))
                return false;
            const auto type = static_cast<u32>(tic.texture_type.Value());
            if (((type == static_cast<u32>(Tegra::Texture::TextureType::Texture2D) ||
                   type == static_cast<u32>(Tegra::Texture::TextureType::Texture2DNoMipmap) ||
                   type == static_cast<u32>(Tegra::Texture::TextureType::TextureCubemap)) &&
                  tic.Depth() != 1) ||
                (type == static_cast<u32>(Tegra::Texture::TextureType::Texture3D) &&
                 tic.BaseLayer() != 0))
                return false;
            // ImageInfo asserts when a pitch-linear TIC is not a 2D image;
            // its Vulkan cache owner cannot represent that guest layout.
            // A zero row pitch would also manufacture a zero-sized backing.
            // Reject both before ImageInfo is constructed by the renderer.
            if (tic.IsPitchLinear() &&
                ((type != static_cast<u32>(Tegra::Texture::TextureType::Texture2D) &&
                  type != static_cast<u32>(Tegra::Texture::TextureType::Texture2DNoMipmap)) ||
                 !tic.pitch_high.Value() ||
                 u64{tic.pitch_high.Value()} * 32U * tic.Height() > ~u32{0}))
                return false;
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
                return false;
            }
            const u64 budget = sample_mode == MsaaMode::Msaa1x1
                ? 64ULL * 1024 * 1024 : 4ULL * 1024 * 1024;
            // Match ImageInfo::ImageInfo(TICEntry) dimensions exactly:
            // arrays put TIC Depth in the layer count, 3D puts it in the
            // image depth, and 1D/buffer images have height 1. Counting both
            // TIC Depth and array layers would wrongly reject valid views.
            using Tegra::Texture::TextureType;
            const u64 base_layer = tic.BaseLayer();
            const u64 depth = tic.Depth();
            const u64 layers = [&]() -> u64 {
                switch (tic.texture_type.Value()) {
                case TextureType::Texture1D:
                case TextureType::Texture1DArray:
                case TextureType::Texture2DArray:
                    return base_layer + depth;
                case TextureType::Texture2D:
                case TextureType::Texture2DNoMipmap:
                    return base_layer + 1;
                case TextureType::TextureCubemap:
                    return base_layer + 6;
                case TextureType::TextureCubeArray:
                    return base_layer + depth * 6;
                case TextureType::Texture3D:
                case TextureType::Texture1DBuffer:
                    return 1;
                default:
                    return 0;
                }
            }();
            const bool one_dimensional =
                type == static_cast<u32>(TextureType::Texture1D) ||
                type == static_cast<u32>(TextureType::Texture1DArray) ||
                type == static_cast<u32>(TextureType::Texture1DBuffer);
            const u64 image_height = one_dimensional ? 1 : tic.Height();
            const u64 image_depth =
                type == static_cast<u32>(TextureType::Texture3D) ? depth : 1;
            if (!NativeQmdWithinTexelBudget(
                    tic.Width(), image_height, image_depth, layers, budget))
                return false;

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
    // Vulkan's VkSamplerCreateInfo requires minLod <= maxLod. The shared
    // sampler owner uses these raw clamps whenever mipmapping is enabled;
    // reject an inverted guest range before it reaches vkCreateSampler.
    // With mipmapping disabled the owner intentionally uses fixed LODs.
    if (sampler.mipmap_filter.Value() != TextureMipmapFilter::None &&
        sampler.min_lod_clamp.Value() > sampler.max_lod_clamp.Value())
        return false;
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
// Reflected SPIR-V image dimensions must agree with the exact guest TIC
// decoded for this QMD. The native shader metadata reader uses this mapping;
// a changed image type must not silently bind an unrelated cache view.
[[nodiscard]] inline bool NativeQmdTicMatchesShaderType(
    Shader::TextureType reflected, const Tegra::Texture::TICEntry& tic) {
    using Shader::TextureType;
    const auto type = tic.texture_type.Value();
    switch (reflected) {
    case TextureType::Color1D:
        return type == Tegra::Texture::TextureType::Texture1D;
    case TextureType::ColorArray1D:
        return type == Tegra::Texture::TextureType::Texture1DArray;
    case TextureType::Color2D:
    case TextureType::Color2DRect:
        return (type == Tegra::Texture::TextureType::Texture2D ||
                type == Tegra::Texture::TextureType::Texture2DNoMipmap) &&
               bool(tic.normalized_coords) == (reflected == TextureType::Color2D);
    case TextureType::ColorArray2D:
        return type == Tegra::Texture::TextureType::Texture2DArray;
    case TextureType::Color3D:
        return type == Tegra::Texture::TextureType::Texture3D;
    case TextureType::ColorCube:
        return type == Tegra::Texture::TextureType::TextureCubemap;
    case TextureType::ColorArrayCube:
        return type == Tegra::Texture::TextureType::TextureCubeArray;
    case TextureType::Buffer:
        return type == Tegra::Texture::TextureType::Texture1DBuffer;
    default:
        return false;
    }
}

// The eight explicitly typed storage formats in the shared SPIR-V ABI
// must agree with the actual guest TIC format AND signed channel types.
// An incorrect but dimension-compatible image would otherwise bind with
// the wrong storage format. Typeless shaders retain their separate runtime
// format policy; this helper does not publish Vulkan descriptors.
[[nodiscard]] inline bool NativeQmdStorageImageFormatMatches(
    Shader::ImageFormat expected, const Tegra::Texture::TICEntry& tic) {
    using Tegra::Texture::ComponentType;
    using Tegra::Texture::TextureFormat;
    const auto channels_are = [&](TextureFormat format, ComponentType kind,
                                  u32 components) {
        return tic.format.Value() == format &&
               tic.r_type.Value() == kind &&
               (components < 2 || tic.g_type.Value() == kind) &&
               (components < 3 || tic.b_type.Value() == kind) &&
               (components < 4 || tic.a_type.Value() == kind);
    };
    switch (expected) {
    case Shader::ImageFormat::Typeless:
        return true;
    case Shader::ImageFormat::R8_UINT:
        return channels_are(TextureFormat::R8, ComponentType::UINT, 1);
    case Shader::ImageFormat::R8_SINT:
        return channels_are(TextureFormat::R8, ComponentType::SINT, 1);
    case Shader::ImageFormat::R16_UINT:
        return channels_are(TextureFormat::R16, ComponentType::UINT, 1);
    case Shader::ImageFormat::R16_SINT:
        return channels_are(TextureFormat::R16, ComponentType::SINT, 1);
    case Shader::ImageFormat::R32_UINT:
        return channels_are(TextureFormat::R32, ComponentType::UINT, 1);
    case Shader::ImageFormat::R32G32_UINT:
        return channels_are(TextureFormat::R32G32, ComponentType::UINT, 2);
    case Shader::ImageFormat::R32G32B32A32_UINT:
        return channels_are(TextureFormat::R32G32B32A32, ComponentType::UINT, 4);
    default:
        return false;
    }
}

// This matches the actual descriptor flattening order, but acts ONLY on
// typed snapshots. It does not acquire cache views, samplers or GPU leases.
[[nodiscard]] inline bool NativeQmdTypedImageLayoutMatches(
    const Shader::Info& info,
    const std::vector<NativeQmdTypedTextureRecord>& typed) {
    if (typed.size() > 256) return false;
    std::size_t pos{};
    const auto matches = [&](const auto& descriptions, NativeQmdImageKind kind) {
        for (const auto& desc : descriptions) {
            if (!desc.count || desc.count > typed.size() - pos) return false;
            using Descriptor = std::remove_cvref_t<decltype(desc)>;
            constexpr bool has_type =
                std::is_same_v<Descriptor, Shader::TextureDescriptor> ||
                std::is_same_v<Descriptor, Shader::ImageDescriptor>;
            constexpr bool has_write =
                std::is_same_v<Descriptor, Shader::ImageBufferDescriptor> ||
                std::is_same_v<Descriptor, Shader::ImageDescriptor>;
            for (u32 index = 0; index < desc.count; ++index) {
                const auto& record = typed[pos++];
                const bool writable = [&] {
                    if constexpr (has_write) return desc.is_written;
                    return false;
                }();
                if (record.kind != kind || record.is_written != writable ||
                    record.sampler.has_value() !=
                        (kind == NativeQmdImageKind::SampledTexture) ||
                    !NativeQmdTicLayoutSupported(record.tic))
                    return false;
                // SPIR-V's element type is distinct from its image
                // dimension and explicit format. An integer guest TIC may
                // not be bound to a reflected float image (or vice versa).
                // Texture buffers carry no is_integer metadata in this ABI.
                if constexpr (std::is_same_v<Descriptor, Shader::TextureDescriptor> ||
                              std::is_same_v<Descriptor, Shader::ImageDescriptor> ||
                              std::is_same_v<Descriptor, Shader::ImageBufferDescriptor>) {
                    const auto channel_type = record.tic.r_type.Value();
                    const bool tic_integer =
                        channel_type == Tegra::Texture::ComponentType::UINT ||
                        channel_type == Tegra::Texture::ComponentType::SINT;
                    if (desc.is_integer != tic_integer)
                        return false;
                }
                if constexpr (std::is_same_v<Descriptor, Shader::ImageDescriptor> ||
                              std::is_same_v<Descriptor, Shader::ImageBufferDescriptor>) {
                    if (!NativeQmdStorageImageFormatMatches(desc.format, record.tic))
                        return false;
                }
                if constexpr (has_type) {
                    if (!NativeQmdTicMatchesShaderType(desc.type, record.tic))
                        return false;
                    // SPIR-V texture descriptors carry an explicit MSAA
                    // bit, unlike storage image descriptors. Never infer
                    // multisampled storage from a non-MSAA reflection ABI.
                    const bool tic_msaa =
                        record.tic.msaa_mode.Value() != Tegra::Texture::MsaaMode::Msaa1x1;
                    if constexpr (std::is_same_v<Descriptor, Shader::TextureDescriptor>) {
                        if (desc.is_multisample != tic_msaa) return false;
                    } else if (tic_msaa) {
                        return false;
                    }
                } else if (record.tic.texture_type !=
                               Tegra::Texture::TextureType::Texture1DBuffer ||
                           record.tic.msaa_mode.Value() != Tegra::Texture::MsaaMode::Msaa1x1) {
                    // Buffer images likewise cannot be multisampled.
                    return false;
                }
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
           pos == typed.size();
}

struct NativeQmdTextureTables {
    u64 tic_base{};
    u64 tsc_base{};
    u32 tic_limit{};
    u32 tsc_limit{};
    bool linked_tsc{};

    // A ticket proves the GPU engine epoch, not that TIC/TSC table registers
    // stayed unchanged while shader compilation/guest CBUF reads executed.
    // This check reads only the channel's live registers under the existing
    // TextureCache mutex; all five values belong to one logical table owner.
    [[nodiscard]] bool SameRegisters(u64 live_tic, u32 live_tic_limit,
                                     u64 live_tsc, u32 live_tsc_limit,
                                     bool live_linked_tsc) const noexcept {
        return tic_base == live_tic && tic_limit == live_tic_limit &&
               tsc_base == live_tsc && tsc_limit == live_tsc_limit &&
               linked_tsc == live_linked_tsc;
    }

    // The capture and snapshot check use ONE descriptor-table walker.
    // The first pass owns a vector; the second runs under TextureCache's
    // channel lock without allocating or creating another descriptor array.
    // Refuse a mixed table as soon as any TIC/TSC byte or handle differs.
    template <typename Read, typename Accept>
    [[nodiscard]] bool ForEachCapturedRecord(const NativeQmdResourcePlan& plan,
                                              Read&& read, Accept&& accept) const {
        if (plan.image_handles.empty()) return !plan.requires_tic_tsc;
        if (!plan.requires_tic_tsc || plan.image_handles.size() > 256)
            return false;
        constexpr u64 gpu_limit = u64{1} << 49;
        constexpr u64 entry_bytes = 32;
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
        for (const auto& handle : plan.image_handles) {
            if (handle.primary_shift >= 32 ||
                (handle.has_secondary && handle.secondary_shift >= 32))
                return false;
            const u64 primary = u64{handle.primary_word} << handle.primary_shift;
            const u64 secondary = handle.has_secondary
                ? u64{handle.secondary_word} << handle.secondary_shift : 0;
            const u64 raw64 = primary | secondary;
            if (raw64 > 0xffffffffU) return false;
            const u32 raw = static_cast<u32>(raw64);
            const u32 tic = linked_tsc ? raw : (raw & 0xfffffU);
            const u32 tsc = linked_tsc ? raw : (raw >> 20);
            const auto tic_addr = entry_address(tic_base, tic_limit, tic);
            if (!tic_addr) return false;
            NativeQmdImageTableRecord record{};
            record.kind = handle.kind;
            record.raw_handle = raw;
            record.tic_index = tic;
            record.tsc_index = tsc;
            record.is_written = handle.is_written;
            if (!read(*tic_addr, record.tic_words.data(),
                      sizeof(record.tic_words)))
                return false;
            record.has_sampler = handle.kind == NativeQmdImageKind::SampledTexture;
            if (record.has_sampler) {
                const auto tsc_addr = entry_address(tsc_base, tsc_limit, tsc);
                if (!tsc_addr ||
                    !read(*tsc_addr, record.tsc_words.data(),
                          sizeof(record.tsc_words)))
                    return false;
            }
            if (!accept(record)) return false;
        }
        return true;
    }

    template <typename Read>
    [[nodiscard]] std::optional<std::vector<NativeQmdImageTableRecord>>
    Capture(const NativeQmdResourcePlan& plan, Read&& read) const {
        if (plan.image_handles.size() > 256) return std::nullopt;
        std::vector<NativeQmdImageTableRecord> captured;
        captured.reserve(plan.image_handles.size());
        if (!ForEachCapturedRecord(
                plan, std::forward<Read>(read),
                [&](const NativeQmdImageTableRecord& record) {
                    captured.push_back(record);
                    return true;
                }))
            return std::nullopt;
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
        if (plan.image_handles.size() != snapshot.size()) return false;
        std::size_t index{};
        return ForEachCapturedRecord(
            plan, std::forward<Read>(read),
            [&](const NativeQmdImageTableRecord& current) {
                const auto& saved = snapshot[index++];
                return saved.kind == current.kind &&
                       saved.raw_handle == current.raw_handle &&
                       saved.tic_index == current.tic_index &&
                       saved.tsc_index == current.tsc_index &&
                       saved.has_sampler == current.has_sampler &&
                       saved.is_written == current.is_written &&
                       saved.tic_words == current.tic_words &&
                       saved.tsc_words == current.tsc_words;
            });
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
            // The same layout, MSAA and texel-budget policy is checked on
            // both captured and replayed typed guest TIC records.
            if (!NativeQmdTicLayoutSupported(entry.tic)) return std::nullopt;
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
            if (!NativeQmdTicLayoutSupported(tic)) return std::nullopt;
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
