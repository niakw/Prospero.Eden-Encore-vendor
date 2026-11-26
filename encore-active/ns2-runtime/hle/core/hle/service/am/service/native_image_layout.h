// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <optional>
#include <limits>
#include <array>
#include <bitset>
#include <algorithm>
#include <map>
#include <vector>
#include <span>
#include "common/common_types.h"

namespace Service::AM {

struct NativeImageRequirements {
    u64 size;
    u64 alignment;
};

// Inter-module block image layout for the observed native color formats.
// Like Lichii/Eglantine's Nx2CalculateImageLayout, RGBA8/RGB10A2 use four
// bytes per pixel, RGBA16Float uses eight. Every request still validates its
// GOB pitch, block height and actual backing size before publishing an ID.
// The SDK's zero image count sizes one image (max(count, 1)).
// Lichii's Xenoblade 2 window-buffer trace proves block-format word 7 may be 1 or 2 with
// identical storage geometry; all other unreviewed variants remain fail-closed.
inline std::optional<NativeImageRequirements> CalculateNativeImageRequirements(
    std::span<const u32> preference, u32 count) {
    if (preference.size() != 16 || preference[0] != 0 ||
        (preference[1] != 1 && preference[1] != 2) || preference[3] != 0 ||
        (preference[6] != 1 && preference[6] != 4 && preference[6] != 5) ||
        (preference[7] != 1 && preference[7] != 2) ||
        preference[4] < 3 || preference[4] > 8) {
        return std::nullopt;
    }
    for (size_t i = 8; i < preference.size(); ++i) {
        if (preference[i] != 0) { return std::nullopt; }
    }
    const u64 width = (preference[2] & 0xffffU) + 1;
    const u64 height = (preference[2] >> 16) + 1;
    const u64 pitch_gobs = preference[5];
    const u64 bytes_per_pixel = preference[6] == 5 ? 8 : 4;
    if (pitch_gobs < (width * bytes_per_pixel + 63) / 64 || pitch_gobs > 4096) {
        return std::nullopt;
    }
    const u64 requested_images = std::max<u64>(count, 1);
    const u64 block_height = 1ULL << preference[4];
    const u64 aligned_height = (height + block_height - 1) & ~(block_height - 1);
    const u64 image_size = pitch_gobs * 64 * aligned_height;
    if (image_size > (1ULL << 48) / requested_images) {
        return std::nullopt;
    }
    return NativeImageRequirements{image_size * requested_images, 4096};
}

// Feature-image pool requirements used by the common GSV server template
// (391/691/1291/1691 in Lichii). This is sizing only: it does not create,
// map or publish an image. Lichii observes preference format 1=RGBA8,
// 4=RGB10A2 and 5=RGBA16F; MKW camera traces also use 0x38 as a four-byte
// block-linear format. Unknown formats fail closed instead of guessing Bpp.
inline std::optional<NativeImageRequirements> CalculateFeatureImageRequirements(
    std::span<const u32> preference, u32 requested_count) {
    if (preference.size() != 16) return std::nullopt;

    u64 bytes_per_pixel{};
    switch (preference[6]) {
    case 1:    // RGBA8
    case 4:    // RGB10A2
    case 0x38: // observed MKW GameChat camera format
        bytes_per_pixel = 4;
        break;
    case 5:    // RGBA16F
        bytes_per_pixel = 8;
        break;
    default:
        return std::nullopt;
    }

    const u64 width = (preference[2] & 0xffffU) + 1;
    const u64 height = (preference[2] >> 16) + 1;
    const u64 minimum_gobs = (width * bytes_per_pixel + 63) / 64;
    u32 block_log2 = preference[4];
    u64 pitch{};
    if (block_log2 <= 8 && preference[5] >= minimum_gobs &&
        preference[5] <= 4096) {
        pitch = u64{preference[5]} * 64;
    } else {
        block_log2 = 7;
        pitch = (width * bytes_per_pixel + 63) & ~u64{63};
    }

    const u64 block_rows = u64{1} << block_log2;
    if (height > std::numeric_limits<u64>::max() - (block_rows - 1))
        return std::nullopt;
    const u64 aligned_height = (height + block_rows - 1) & ~(block_rows - 1);
    if (pitch == 0 || aligned_height > std::numeric_limits<u64>::max() / pitch)
        return std::nullopt;
    const u64 image_size = pitch * aligned_height;
    const u64 count = std::max<u64>(requested_count, 1);
    if (image_size == 0 || image_size > std::numeric_limits<u64>::max() / count)
        return std::nullopt;
    const u64 alignment = std::max<u64>(4096, 64 * block_rows);
    return NativeImageRequirements{image_size * count, alignment};
}

// 429/430 maintain real NVMap-backed image reference ownership. The ledger
// does not allocate VkImage, translate shaders or present any guest frame.
struct NativeImageRecord {
    u32 id{};
    u32 backing{};
    u64 size{};
    std::array<u32, 16> preference{};
    // 315(first,count) owns a whole alias group; only the first record stores
    // the group length. Every member still carries window_pool=true so 430
    // cannot destroy an aliased image behind VI2.
    u32 window_pool_count{};
    bool window_pool{};
    u64 offset{}; // byte offset within this real NVMap backing
};
class NativeImageLedger {
public:
    static constexpr std::size_t MaxEntries = 1024;
    // 429(count) returns the first of count consecutive image handles.
    // Each handle owns a separate NVMap reference and distinct storage.
    // All records are published atomically or none are published.
    [[nodiscard]] std::optional<u32> Create(u32 backing, u64 backing_size,
                                            NativeImageRequirements requirements,
                                            std::span<const u32> preferences,
                                            u32 requested_id = 0, u32 count = 1) {
        if (backing == 0 || preferences.size() != 16 || requirements.size == 0 ||
            requirements.alignment != 4096 || requirements.size > backing_size ||
            !count || count > MaxEntries - m_images.size() ||
            requirements.size % count || m_next == 0)
            return {};
        const u32 id = requested_id ? requested_id : m_next;
        if (!id || id < m_next || id > std::numeric_limits<u32>::max() - (count - 1))
            return {};
        const u64 stride = requirements.size / count;
        for (u32 i = 0; i < count; ++i)
            if (m_images.contains(id + i)) return {};
        u32 published{};
        try {
            for (; published < count; ++published) {
                NativeImageRecord record{id + published, backing, stride, {}};
                record.offset = u64{published} * stride;
                std::copy(preferences.begin(), preferences.end(), record.preference.begin());
                m_images.emplace(record.id, record);
            }
        } catch (...) {
            for (u32 i = 0; i < published; ++i) m_images.erase(id + i);
            throw;
        }
        const u32 last = id + count - 1;
        m_next = last == std::numeric_limits<u32>::max() ? 0 : last + 1;
        return id;
    }
    [[nodiscard]] std::optional<NativeImageRecord> Get(u32 id) const {
        const auto it = m_images.find(id);
        if (it == m_images.end()) return {};
        return it->second;
    }
    [[nodiscard]] bool ConfigureWindowPool(u32 first, u32 count, bool enabled) {
        const auto first_it = m_images.find(first);
        if (first_it == m_images.end()) return false;
        if (enabled) {
            if (!count || count > MaxEntries ||
                first > std::numeric_limits<u32>::max() - (count - 1) ||
                first_it->second.window_pool || first_it->second.window_pool_count)
                return false;
            // Validate the whole group first so a missing/aliased middle image
            // cannot leave a partially-published window pool.
            for (u32 i = 0; i < count; ++i) {
                const auto it = m_images.find(first + i);
                if (it == m_images.end() || it->second.window_pool ||
                    it->second.window_pool_count)
                    return false;
            }
            for (u32 i = 0; i < count; ++i) {
                auto& record = m_images.find(first + i)->second;
                record.window_pool = true;
                record.window_pool_count = 0;
            }
            first_it->second.window_pool_count = count;
            return true;
        }

        // 314 carries only the first image ID. Retire exactly the group
        // recorded by 315; never infer a length from adjacent numeric IDs.
        if (count != 0 || !first_it->second.window_pool ||
            first_it->second.window_pool_count == 0)
            return false;
        const u32 stored_count = first_it->second.window_pool_count;
        if (stored_count > MaxEntries ||
            first > std::numeric_limits<u32>::max() - (stored_count - 1))
            return false;
        for (u32 i = 0; i < stored_count; ++i) {
            const auto it = m_images.find(first + i);
            if (it == m_images.end() || !it->second.window_pool ||
                (i != 0 && it->second.window_pool_count != 0))
                return false;
        }
        for (u32 i = 0; i < stored_count; ++i) {
            auto& record = m_images.find(first + i)->second;
            record.window_pool = false;
            record.window_pool_count = 0;
        }
        return true;
    }
    [[nodiscard]] bool Destroy(u32 id) {
        const auto it = m_images.find(id);
        if (it == m_images.end() || it->second.window_pool ||
            it->second.window_pool_count)
            return false;
        m_images.erase(it);
        return true;
    }
    [[nodiscard]] std::size_t Count() const { return m_images.size(); }
    [[nodiscard]] const std::map<u32, NativeImageRecord>& Records() const {
        return m_images;
    }
private:
    u32 m_next{1};
    std::map<u32, NativeImageRecord> m_images;
};

// Separate from 429 image objects: 911 creates image pools backed by
// owned NVMap allocations; 426/427/428 map/query those individual images.
struct NativeVideoImageLayout {
    u64 total_bytes{};
    u32 image_bytes{};
    u32 image_stride{};
    u32 row_bytes{};
    bool nv12{};
    u32 chroma_offset{};
    u32 cpu_kind{}; // nn::gsv::CpuUserImageDescriptor: 8 RGBA, 9 BGRA, 10 NV12
    // GameChat 1211 pools use the native block-linear 426/428 ABI, while
    // decoder 911 pools retain Eden's address/last-byte reply convention.
    bool native_gpu_layout{};
    u32 gpu_layout_code{};
    u32 block_height_log2{};
    u32 pitch_gobs{};
};
inline std::optional<NativeVideoImageLayout> CalculateNativeVideoImageLayout(
    std::span<const u32> preference, u32 count) {
    if (preference.size() != 16 || !count || count > 1024 ||
        preference[0] != 0 || preference[1] < 1 || preference[1] > 6 ||
        std::any_of(preference.begin() + 3, preference.end(),
                    [](u32 word) { return word != 0; }))
        return {};
    const u64 width = (preference[2] & 0xffffU) + 1;
    const u64 height = (preference[2] >> 16) + 1;
    const bool nv12 = preference[1] == 1 || preference[1] == 4;
    const u32 cpu_kind = nv12 ? 10U :
                         (preference[1] == 2 || preference[1] == 5) ? 9U : 8U;
    if (nv12) {
        if (width < 2 || height < 2 || ((width | height) & 1)) return {};
        const u64 pitch = (width + 255) & ~u64{255};
        const u64 chroma_offset = pitch * ((height + 3) & ~u64{3});
        const u64 bytes = std::max(u64{4096},
            chroma_offset + pitch * (((height / 2) + 3) & ~u64{3}));
        if (bytes > std::numeric_limits<u32>::max() ||
            bytes > std::numeric_limits<u64>::max() / count)
            return {};
        return NativeVideoImageLayout{bytes * count, static_cast<u32>(bytes),
            static_cast<u32>(bytes), static_cast<u32>(pitch), true,
            static_cast<u32>(chroma_offset), cpu_kind};
    }
    const u64 pitch = ((width + 15) & ~u64{15}) * 4;
    const u64 bytes = std::max(u64{4096}, pitch * ((height + 127) & ~u64{127}));
    const u64 stride = (bytes + 8191) & ~u64{8191};
    if (bytes > std::numeric_limits<u32>::max() ||
        stride > std::numeric_limits<u32>::max() ||
        (count - 1) > (std::numeric_limits<u64>::max() - bytes) / stride)
        return {};
    return NativeVideoImageLayout{stride * (count - 1) + bytes,
        static_cast<u32>(bytes), static_cast<u32>(stride),
        static_cast<u32>(pitch), false, 0, cpu_kind};
}
struct NativeVideoPoolRecord {
    u32 first_id{};
    u32 count{};
    u32 backing{};
    u32 packed_dimensions{};
    NativeVideoImageLayout layout{};
    // Decoder 912 retires image handles individually. Keep allocation range
    // ownership until the last live member retires; chat 1212 still removes
    // the complete pool by its first handle.
    std::bitset<1024> active_images{};
    u32 remaining{};
};
struct NativeVideoImageView {
    u32 backing{};
    u32 packed_dimensions{};
    u64 offset{};
    u64 size{};
    bool native_gpu_layout{};
    u32 gpu_layout_code{};
    u32 block_height_log2{};
    u32 pitch_gobs{};
};
class NativeVideoPoolLedger {
public:
    static constexpr std::size_t MaxImages = 1024;
    [[nodiscard]] bool Add(u32 first, u32 count, u32 backing, u64 backing_size,
                           u32 dimensions, NativeVideoImageLayout layout) {
        if (!first || !count || count > MaxImages || !backing ||
            (layout.cpu_kind != 8 && layout.cpu_kind != 9 &&
             layout.cpu_kind != 10) || layout.nv12 != (layout.cpu_kind == 10) ||
            !layout.image_bytes || !layout.image_stride ||
            layout.image_stride < layout.image_bytes ||
            ((layout.image_bytes | layout.image_stride) & 0xfffU) != 0 ||
            layout.total_bytes == 0 || layout.total_bytes > backing_size ||
            (count > 1 && u64{count - 1} * layout.image_stride >
                              std::numeric_limits<u32>::max()) ||
            first > std::numeric_limits<u32>::max() - (count - 1) ||
            m_count > MaxImages - count || m_pools.contains(first))
            return false;
        // Allocated image IDs must never alias another active pool.
        for (const auto& [_, existing] : m_pools) {
            const u64 a = first, b = a + count;
            const u64 x = existing.first_id, y = x + existing.count;
            if (a < y && x < b) return false;
        }
        NativeVideoPoolRecord record{first, count, backing, dimensions, layout};
        for (u32 i = 0; i < count; ++i) record.active_images.set(i);
        record.remaining = count;
        m_pools.emplace(first, record);
        m_count += count;
        return true;
    }
    [[nodiscard]] std::optional<NativeVideoImageView> Lookup(u32 image) const {
        for (const auto& [_, pool] : m_pools) {
            if (image < pool.first_id || image - pool.first_id >= pool.count)
                continue;
            const u32 index = image - pool.first_id;
            if (!pool.active_images.test(index)) return {};
            const u64 offset = u64{index} * pool.layout.image_stride;
            if (offset > std::numeric_limits<u32>::max()) return {};
            return NativeVideoImageView{
                pool.backing, pool.packed_dimensions, offset, pool.layout.image_bytes,
                pool.layout.native_gpu_layout, pool.layout.gpu_layout_code,
                pool.layout.block_height_log2, pool.layout.pitch_gobs};
        }
        return {};
    }
    // gsv TIPC 961: a 128-byte CPU-user decoder-image descriptor. The
    // reported offsets and row pitch MUST describe the same guest backing
    // chosen by 911, not a newly guessed surface layout. The words at 0x10
    // and 0x20 are u64 pitches (upper halves remain zero here).
    [[nodiscard]] std::optional<std::array<u32, 32>> CpuImageDescriptor(u32 image) const {
        for (const auto& [_, pool] : m_pools) {
            if (image < pool.first_id || image - pool.first_id >= pool.count)
                continue;
            const u32 index = image - pool.first_id;
            if (!pool.active_images.test(index)) return {};
            const u64 offset = u64{index} * pool.layout.image_stride;
            if (!pool.layout.row_bytes || offset > std::numeric_limits<u32>::max())
                return {};
            std::array<u32, 32> words{};
            words[0] = pool.layout.cpu_kind; // RGBA/BGRA/NV12 pitch-linear
            words[2] = static_cast<u32>(offset);      // Plane 0 offset (+0x8)
            words[4] = pool.layout.row_bytes;          // Plane 0 pitch  (+0x10)
            if (pool.layout.nv12) {
                if (pool.layout.chroma_offset >
                    std::numeric_limits<u32>::max() - offset)
                    return {};
                words[6] = static_cast<u32>(offset + pool.layout.chroma_offset);
                words[8] = pool.layout.row_bytes;
            }
            return words;
        }
        return {};
    }
    [[nodiscard]] std::optional<NativeVideoPoolRecord> Get(u32 first) const {
        const auto it = m_pools.find(first);
        return it == m_pools.end() ? std::nullopt :
               std::optional<NativeVideoPoolRecord>{it->second};
    }
    [[nodiscard]] std::optional<NativeVideoPoolRecord> GetByImage(u32 image) const {
        for (const auto& [_, pool] : m_pools) {
            if (image < pool.first_id || image - pool.first_id >= pool.count)
                continue;
            const u32 index = image - pool.first_id;
            if (!pool.active_images.test(index)) return {};
            return pool;
        }
        return {};
    }
    [[nodiscard]] bool RetireImage(u32 image) {
        for (auto it = m_pools.begin(); it != m_pools.end(); ++it) {
            auto& pool = it->second;
            if (image < pool.first_id || image - pool.first_id >= pool.count)
                continue;
            const u32 index = image - pool.first_id;
            if (!pool.active_images.test(index) || pool.remaining == 0)
                return false;
            pool.active_images.reset(index);
            --pool.remaining;
            --m_count;
            if (pool.remaining == 0) m_pools.erase(it);
            return true;
        }
        return false;
    }
    [[nodiscard]] bool Remove(u32 first) {
        const auto it = m_pools.find(first);
        if (it == m_pools.end()) return false;
        m_count -= it->second.remaining;
        m_pools.erase(it);
        return true;
    }
    [[nodiscard]] std::size_t ImageCount() const { return m_count; }
    [[nodiscard]] const std::map<u32, NativeVideoPoolRecord>& Records() const {
        return m_pools;
    }
private:
    std::size_t m_count{};
    std::map<u32, NativeVideoPoolRecord> m_pools;
};

} // namespace Service::AM
