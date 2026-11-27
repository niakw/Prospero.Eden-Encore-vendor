// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <array>
#include <limits>
#include <optional>
#include <span>
#include "common/common_types.h"
#include "common/swap.h"
#include "core/hle/service/nvdrv/devices/native_sparse_remap.h"

namespace Service::AM {
struct GpuSparseMapRequest {
    u64_le address;
    u64_le size;
    u8 read_only;
    u8 cache;
    u8 reserved0;
    u8 kind;
    u8 fallback_kind;
    u8 comptag_mode;
    u16_le reserved1;
    u32_le memory;
    u32_le reserved2;
    u64_le source_offset;
};
static_assert(sizeof(GpuSparseMapRequest) == 40);

inline std::optional<Nvidia::Devices::NativeSparseRemapEntry> DecodeGpuSparseMap(
    const GpuSparseMapRequest& request, u8 page_shift = 12) {
    // TIPC 216 has no page-size field. Its owner is the preceding TIPC 214
    // reservation: use that authenticated granularity, never invented flags
    // from the guest's forty-byte remap structure.
    if ((page_shift != 12 && page_shift != 16) ||
        !request.size || ((u64{request.address} | u64{request.size} |
        u64{request.source_offset}) & ((u64{1} << page_shift) - 1)) ||
        request.cache > 1 || request.read_only ||
        request.comptag_mode || request.reserved0 || request.reserved1 || request.reserved2) {
        return std::nullopt;
    }
    Nvidia::Devices::NativeSparseRemapEntry entry{};
    // SDK 46d68c..46d6a4 converts ioctl bit7 to byte16 and cacheable bit2 to byte17.
    entry.flags = (page_shift == 16 ? 0x2000U : 0x1000U) | (request.cache ? 4U : 0U);
    entry.kind = request.kind;
    entry.fallback_kind = request.fallback_kind;
    entry.handle = request.memory;
    entry.source_pages = u64{request.source_offset} >> page_shift;
    entry.destination_pages = u64{request.address} >> page_shift;
    entry.pages = u64{request.size} >> page_shift;
    if (!Nvidia::Devices::DecodeNativeSparseRemap(entry)) return std::nullopt;
    return entry;
}

struct GpuMemoryRegistration {
    u64 begin;
    u64 end;
    u8 flags;
    u8 device;
};

constexpr std::optional<GpuMemoryRegistration> DecodeGpuMemoryRegistration(std::span<const u32> packet) {
    if (packet.size() < 8 || packet[0] != 0x75 || packet[1] != 6) return std::nullopt;
    // The SDK writes STRB at offsets 0x18 and 0x1c, leaving three padding bytes.
    return GpuMemoryRegistration{u64{packet[2]} | (u64{packet[3]} << 32),
        u64{packet[4]} | (u64{packet[5]} << 32), static_cast<u8>(packet[6]), static_cast<u8>(packet[7])};
}
struct GpuMemoryRelease {
    u32 id;
    u32 notification;
};
struct GpuMemoryUserDebugLabel {
    u32 id{};
    std::array<u64, 2> value{};
};
struct GpuMemorySdkDebugLabel {
    u32 id{};
    u64 value{};
};
// nn::gpu::sdk uses worker TIPC 172/173 to annotate an existing
// GpuMemoryHandle. These are metadata operations, never GPU submissions.
inline constexpr std::optional<GpuMemoryUserDebugLabel> DecodeGpuMemoryUserDebugLabel(
    std::span<const u32> packet) {
    if (packet.size() != 7 || packet[0] != 0xbc || packet[1] != 5 || !packet[2])
        return {};
    return GpuMemoryUserDebugLabel{packet[2],
        {u64{packet[3]} | (u64{packet[4]} << 32),
         u64{packet[5]} | (u64{packet[6]} << 32)}};
}
inline constexpr std::optional<GpuMemorySdkDebugLabel> DecodeGpuMemorySdkDebugLabel(
    std::span<const u32> packet) {
    if (packet.size() != 5 || packet[0] != 0xbd || packet[1] != 3 || !packet[2])
        return {};
    return GpuMemorySdkDebugLabel{packet[2],
        u64{packet[3]} | (u64{packet[4]} << 32)};
}
struct GpuSegmentUserDebugLabel {
    u32 address_space{};
    u64 address{};
    u64 size{};
    std::array<u64, 2> value{};
};
struct GpuSegmentSdkDebugLabel {
    u32 address_space{};
    u64 address{};
    u64 size{};
    u64 value{};
};
inline constexpr bool IsValidGpuSegmentDebugRange(u32 address_space, u64 address, u64 size) {
    return address_space != 0 && size != 0 &&
           address <= std::numeric_limits<u64>::max() - size;
}
// nn::gpu::sdk SetGpuSegment*DebugLabel writes request words 0xE1/0xE2.
// ServiceFramework TIPC dispatch subtracts the 0x10 command envelope, so
// the registered worker command IDs are 209/210. These are debug metadata
// requests, not GPU submissions or mappings.
inline constexpr std::optional<GpuSegmentUserDebugLabel>
DecodeGpuSegmentUserDebugLabel(std::span<const u32> packet) {
    if (packet.size() != 11 || packet[0] != 0xe1 || packet[1] != 9)
        return {};
    GpuSegmentUserDebugLabel result{
        packet[2],
        u64{packet[3]} | (u64{packet[4]} << 32),
        u64{packet[5]} | (u64{packet[6]} << 32),
        {u64{packet[7]} | (u64{packet[8]} << 32),
         u64{packet[9]} | (u64{packet[10]} << 32)}};
    return IsValidGpuSegmentDebugRange(result.address_space, result.address, result.size)
        ? std::optional<GpuSegmentUserDebugLabel>{result} : std::nullopt;
}
inline constexpr std::optional<GpuSegmentSdkDebugLabel>
DecodeGpuSegmentSdkDebugLabel(std::span<const u32> packet) {
    if (packet.size() != 9 || packet[0] != 0xe2 || packet[1] != 7)
        return {};
    GpuSegmentSdkDebugLabel result{
        packet[2],
        u64{packet[3]} | (u64{packet[4]} << 32),
        u64{packet[5]} | (u64{packet[6]} << 32),
        u64{packet[7]} | (u64{packet[8]} << 32)};
    return IsValidGpuSegmentDebugRange(result.address_space, result.address, result.size)
        ? std::optional<GpuSegmentSdkDebugLabel>{result} : std::nullopt;
}

inline constexpr std::optional<u32> DecodeEnsureGpuMemoryComptags(
    std::span<const u32> packet) {
    // nn::gpu::sdk::EnsureGpuMemoryComptags: worker TIPC 171 carries only
    // the live GpuMemoryHandle. Encore always selects an uncompressed
    // fallback kind for compressed native requests, so no NVIDIA comptag
    // storage is allocated by the backend.
    if (packet.size() != 3 || packet[0] != 0xbb || packet[1] != 1 ||
        packet[2] == 0)
        return {};
    return packet[2];
}
constexpr std::optional<GpuMemoryRelease> DecodeGpuMemoryRelease(
    std::span<const u32> packet) {
    if (packet.size() != 4 || packet[0] != 0x76 || packet[1] != 2 ||
        !packet[2] || packet[3] >= 64) return std::nullopt;
    return GpuMemoryRelease{packet[2], packet[3]};
}

// TIPC registration uses inclusive end addresses. Pure preflight avoids
// allocating an NvMap handle for wrapped or unaligned ranges.
constexpr bool IsValidGpuMemoryRegistrationRange(
    const GpuMemoryRegistration& req) {
    if (req.end < req.begin || req.end == ~u64{0} ||
        !req.device || (req.begin & 0xfff)) return false;
    const u64 size = req.end - req.begin + 1;
    return size != 0 && (size & 0xfff) == 0;
}
inline constexpr u32 GpuSparseRegionFlag = 0x200U;

struct GpuSparseRegionRequest {
    u32 address_space;
    u64 begin;
    u64 end;
    u32 page_flags;
    u8 page_shift;
    u8 allocation_mode;
    u8 device;
    u8 sync;
    u32 notification;
};

[[nodiscard]] constexpr bool IsSparseGpuRegion(
    const GpuSparseRegionRequest& request) {
    // TIPC 214 word 7 combines the low-byte page shift with region flags.
    // MKW uses 0x60c/0xa0c: truncating this word to u8 preserved 4-KiB
    // granularity but silently discarded the 0x200 sparse-reservation bit.
    // Keep the historical word-8 mode as an additional sparse indication so
    // already-qualified callers retain their previous ABI.
    return (request.page_flags & GpuSparseRegionFlag) != 0 ||
           request.allocation_mode == 1;
}

constexpr std::optional<GpuSparseRegionRequest> DecodeGpuSparseRegion(
    std::span<const u32> packet) {
    if (packet.size() < 12 || packet[0] != 0xe6 || packet[1] != 10) return std::nullopt;
    return GpuSparseRegionRequest{packet[2], u64{packet[3]} | (u64{packet[4]} << 32),
        u64{packet[5]} | (u64{packet[6]} << 32), packet[7],
        static_cast<u8>(packet[7] & 0xffU), static_cast<u8>(packet[8]),
        static_cast<u8>(packet[9]), static_cast<u8>(packet[10]), packet[11]};
}

// Bound experimental GPU-only render-target backing. An unbacked sparse
// reservation (TIPC word-7 sparse flag or the older word-8 mode 1) must never
// count toward physically resident images, and an OOM must not return a
// successful 214 completion.
inline constexpr u64 NativeDenseGpuRegionLimit = 512ULL << 20;
inline constexpr u64 NativeDenseGpuTotalLimit = 2ULL << 30;
inline constexpr u64 NativeDenseGpuFreeReserve = 128ULL << 20;

constexpr bool CanBackNativeDenseGpuOnly(u8 allocation_mode, u64 size,
                                         u64 used, u64 free_bytes) {
    return allocation_mode == 0 && size >= 0x1000 &&
           size <= NativeDenseGpuRegionLimit && (size & 0xfff) == 0 &&
           used <= NativeDenseGpuTotalLimit &&
           size <= NativeDenseGpuTotalLimit - used &&
           free_bytes >= NativeDenseGpuFreeReserve &&
           size <= free_bytes - NativeDenseGpuFreeReserve;
}

constexpr bool IsValidGpuSparseRegionRange(const GpuSparseRegionRequest& request) {
    if ((request.page_shift != 12 && request.page_shift != 16) || request.end < request.begin ||
        request.end == ~u64{0}) return false;
    const u64 mask = (u64{1} << request.page_shift) - 1;
    return (request.begin & mask) == 0 && ((request.end + 1) & mask) == 0;
}
} // namespace Service::AM
