// SPDX-License-Identifier: GPL-3.0-or-later
#include "shader_recompiler/frontend/ampere/translation_cache.h"
#include <algorithm>
#include <cstring>

namespace Shader::Ampere {
NativeTranslationCache::NativeTranslationCache(std::size_t bytes, std::size_t entries)
    : limit_bytes{bytes}, limit_entries{entries} {}

bool NativeTranslationCache::Matches(
    const Entry& e, std::span<const u8> header,
    std::span<const u8> native, u32 stage) {
    // Verify FULL source bytes and header. Never trust a hash alone: a
    // collision must not alias two GPU programs.
    return e.stage == stage && e.native.size() == native.size() &&
           std::equal(e.header.begin(), e.header.end(), header.begin()) &&
           std::equal(e.native.begin(), e.native.end(), native.begin());
}

void NativeTranslationCache::EvictOne() {
    const auto& oldest = lru.back();
    stats.bytes -= oldest.cost;
    lru.pop_back();
    ++stats.evictions;
    stats.entries = lru.size();
}

MaxwellShaderImage NativeTranslationCache::GetOrTranslate(
    std::span<const u8> header, std::span<const u8> native, u32 stage) {
    if (header.size() != 0x50 || native.empty() ||
        native.size() > 4096 * 16 || native.size() % 16) {
        std::scoped_lock lock{mutex};
        ++stats.rejected;
        return {};
    }
    u64 started_generation{};
    {
        std::scoped_lock lock{mutex};
        started_generation = generation;
        for (auto it = lru.begin(); it != lru.end(); ++it) {
            if (!Matches(*it, header, native, stage)) continue;
            auto copy = it->image;
            lru.splice(lru.begin(), lru, it);
            ++stats.hits;
            return copy;
        }
        ++stats.misses;
    }

    // Do not hold the cache mutex over the potentially costly native
    // lowering; another worker may compute the same image concurrently.
    auto image = BuildMaxwellShaderImage(header, native, stage);
    {
        std::scoped_lock lock{mutex};
        ++stats.translations;
        // Clear may race with expensive native lowering. Do not publish
        // an image captured before the GPU/session cache was invalidated.
        if (generation != started_generation) {
            ++stats.invalidated_translations;
            return {};
        }
        if (!image.Ready()) {
            ++stats.rejected;
            return image; // Never cache invalid/partially decoded shaders.
        }
        for (auto it = lru.begin(); it != lru.end(); ++it) {
            if (!Matches(*it, header, native, stage)) continue;
            auto copy = it->image;
            lru.splice(lru.begin(), lru, it);
            ++stats.hits;
            return copy;
        }
        const std::size_t cost = sizeof(Entry) + header.size() +
                                 native.size() + image.code.size() * sizeof(u64);
        // Oversize successful images remain valid, but do not enter cache.
        if (!limit_entries || cost > limit_bytes) return image;
        while (!lru.empty() &&
               (lru.size() >= limit_entries || stats.bytes > limit_bytes - cost))
            EvictOne();
        Entry entry;
        std::copy(header.begin(), header.end(), entry.header.begin());
        entry.native.assign(native.begin(), native.end());
        entry.stage = stage;
        entry.image = image;
        entry.cost = cost;
        lru.push_front(std::move(entry));
        stats.bytes += cost;
        stats.entries = lru.size();
    }
    return image;
}

NativeTranslationCacheStats NativeTranslationCache::Stats() const {
    const std::scoped_lock lock{mutex};
    return stats;
}
void NativeTranslationCache::Clear() {
    const std::scoped_lock lock{mutex};
    ++generation;
    ++stats.invalidations;
    lru.clear();
    stats.bytes = 0;
    stats.entries = 0;
}
} // namespace Shader::Ampere
