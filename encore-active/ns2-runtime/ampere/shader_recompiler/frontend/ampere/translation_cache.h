// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
// Explicitly owned research cache. Never global: GPU processes and channel
// lifetimes differ, and 64-bit diagnostic keys alone are NOT proof of content.
#include <array>
#include <cstddef>
#include <list>
#include <mutex>
#include <span>
#include <vector>
#include "shader_recompiler/frontend/ampere/maxwell_image.h"

namespace Shader::Ampere {

struct NativeTranslationCacheStats {
    u64 hits{};
    u64 misses{};
    u64 translations{};
    u64 rejected{};
    u64 evictions{};
    u64 invalidations{};
    u64 invalidated_translations{};
    std::size_t entries{};
    std::size_t bytes{};
};

class NativeTranslationCache {
public:
    // Small, explicitly owned, bounded; owner must drop it with its NS2
    // graphics session or on shader-cache invalidation.
    explicit NativeTranslationCache(std::size_t limit_bytes = 16 * 1024 * 1024,
                                    std::size_t limit_entries = 64);
    [[nodiscard]] MaxwellShaderImage GetOrTranslate(
        std::span<const u8> header, std::span<const u8> native, u32 stage);
    [[nodiscard]] NativeTranslationCacheStats Stats() const;
    void Clear();
private:
    struct Entry {
        std::array<u8, 0x50> header{};
        std::vector<u8> native{};
        u32 stage{};
        MaxwellShaderImage image{};
        std::size_t cost{};
    };
    [[nodiscard]] static bool Matches(const Entry& entry,
                                      std::span<const u8> header,
                                      std::span<const u8> native, u32 stage);
    void EvictOne();
    mutable std::mutex mutex;
    std::list<Entry> lru;
    const std::size_t limit_bytes;
    const std::size_t limit_entries;
    NativeTranslationCacheStats stats{};
    // Prevent in-flight lowering from resurrecting a cleared cache.
    u64 generation{};
};
} // namespace Shader::Ampere
