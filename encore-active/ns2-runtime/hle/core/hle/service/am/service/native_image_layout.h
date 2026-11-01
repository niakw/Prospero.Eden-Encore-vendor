// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <optional>
#include <limits>
#include <array>
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

// Inter-module block image layout, restricted to the observed four-byte color formats. Row pitch is
// in 64-byte GOBs; block height is in pixels. Matches the gsv sizers (commands 435 and 1510, run
// against the real module): the size is one image's padded block storage times the request's
// count word, the format word does not scale it, and the alignment is always one page.
inline std::optional<NativeImageRequirements> CalculateNativeImageRequirements(
    std::span<const u32> preference, u32 count) {
    if (preference.size() != 16 || count == 0 || preference[0] != 0 ||
        (preference[1] != 1 && preference[1] != 2) || preference[3] != 0 ||
        (preference[6] != 1 && preference[6] != 4) ||
        preference[7] != 1 || preference[4] < 3 || preference[4] > 8) {
        return std::nullopt;
    }
    for (size_t i = 8; i < preference.size(); ++i) {
        if (preference[i] != 0) { return std::nullopt; }
    }
    const u64 width = (preference[2] & 0xffffU) + 1;
    const u64 height = (preference[2] >> 16) + 1;
    const u64 pitch_gobs = preference[5];
    if (pitch_gobs < (width * 4 + 63) / 64 || pitch_gobs > 4096) {
        return std::nullopt;
    }
    const u64 block_height = 1ULL << preference[4];
    const u64 aligned_height = (height + block_height - 1) & ~(block_height - 1);
    const u64 image_size = pitch_gobs * 64 * aligned_height;
    if (image_size > (1ULL << 48) / count) {
        return std::nullopt;
    }
    return NativeImageRequirements{image_size * count, 4096};
}

// 429/430 maintain real NVMap-backed image reference ownership. The ledger
// does not allocate VkImage, translate shaders or present any guest frame.
struct NativeImageRecord {
    u32 id{};
    u32 backing{};
    u64 size{};
    std::array<u32, 16> preference{};
};
class NativeImageLedger {
public:
    static constexpr std::size_t MaxEntries = 1024;
    [[nodiscard]] std::optional<u32> Create(u32 backing, u64 backing_size,
                                            NativeImageRequirements requirements,
                                            std::span<const u32> preferences) {
        if (backing == 0 || preferences.size() != 16 || requirements.size == 0 ||
            requirements.alignment != 4096 || requirements.size > backing_size ||
            m_images.size() >= MaxEntries || m_next == 0)
            return {};
        const u32 id = m_next++;
        NativeImageRecord record{id, backing, requirements.size, {}};
        std::copy(preferences.begin(), preferences.end(), record.preference.begin());
        m_images.emplace(id, record);
        return id;
    }
    [[nodiscard]] std::optional<NativeImageRecord> Get(u32 id) const {
        const auto it = m_images.find(id);
        if (it == m_images.end()) return {};
        return it->second;
    }
    [[nodiscard]] bool Destroy(u32 id) { return m_images.erase(id) == 1; }
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
};
inline std::optional<NativeVideoImageLayout> CalculateNativeVideoImageLayout(
    std::span<const u32> preference, u32 count) {
    if (preference.size() != 16 || !count || count > 1024 ||
        preference[0] != 0 || (preference[1] != 1 && preference[1] != 5) ||
        std::any_of(preference.begin() + 3, preference.end(),
                    [](u32 word) { return word != 0; }))
        return {};
    const u64 width = (preference[2] & 0xffffU) + 1;
    const u64 height = (preference[2] >> 16) + 1;
    if (preference[1] == 1) {
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
            static_cast<u32>(chroma_offset)};
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
        static_cast<u32>(pitch), false, 0};
}
struct NativeVideoPoolRecord {
    u32 first_id{};
    u32 count{};
    u32 backing{};
    u32 packed_dimensions{};
    NativeVideoImageLayout layout{};
};
struct NativeVideoImageView {
    u32 backing{};
    u32 packed_dimensions{};
    u64 offset{};
    u64 size{};
};
class NativeVideoPoolLedger {
public:
    static constexpr std::size_t MaxImages = 1024;
    [[nodiscard]] bool Add(u32 first, u32 count, u32 backing, u64 backing_size,
                           u32 dimensions, NativeVideoImageLayout layout) {
        if (!first || !count || count > MaxImages || !backing ||
            layout.total_bytes == 0 || layout.total_bytes > backing_size ||
            first > std::numeric_limits<u32>::max() - (count - 1) ||
            m_count > MaxImages - count || m_pools.contains(first))
            return false;
        // Allocated image IDs must never alias another active pool.
        for (const auto& [_, existing] : m_pools) {
            const u64 a = first, b = a + count;
            const u64 x = existing.first_id, y = x + existing.count;
            if (a < y && x < b) return false;
        }
        m_pools.emplace(first, NativeVideoPoolRecord{first, count, backing,
                                                      dimensions, layout});
        m_count += count;
        return true;
    }
    [[nodiscard]] std::optional<NativeVideoImageView> Lookup(u32 image) const {
        for (const auto& [_, pool] : m_pools) {
            if (image < pool.first_id || image - pool.first_id >= pool.count)
                continue;
            const u64 offset = u64{image - pool.first_id} * pool.layout.image_stride;
            if (offset > std::numeric_limits<u32>::max()) return {};
            return NativeVideoImageView{pool.backing, pool.packed_dimensions,
                                        offset, pool.layout.image_bytes};
        }
        return {};
    }
    [[nodiscard]] std::optional<NativeVideoPoolRecord> Get(u32 first) const {
        const auto it = m_pools.find(first);
        return it == m_pools.end() ? std::nullopt :
               std::optional<NativeVideoPoolRecord>{it->second};
    }
    [[nodiscard]] bool Remove(u32 first) {
        const auto it = m_pools.find(first);
        if (it == m_pools.end()) return false;
        m_count -= it->second.count;
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
