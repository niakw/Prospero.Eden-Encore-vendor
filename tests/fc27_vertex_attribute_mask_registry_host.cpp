// SPDX-License-Identifier: GPL-3.0-or-later
#include <array>
#include <atomic>
#include <bit>
#include <cassert>
#include <cstdint>
#include <map>
#include <mutex>
#include <optional>
#include <thread>
#include <vector>

using u32 = std::uint32_t;
using u64 = std::uint64_t;
using Identity = std::array<u64, 2>;

struct Registry {
    std::mutex mutex;
    std::map<Identity, u64> masks;
    std::atomic<u32> generation{0};
    static constexpr std::size_t MaxMasks = 128;

    void Publish(Identity hash, u64 mask) {
        bool changed = false;
        {
            std::scoped_lock lock{mutex};
            auto it = masks.find(hash);
            if (it == masks.end()) {
                if (masks.size() >= MaxMasks) {
                    return;
                }
                masks.emplace(hash, mask);
                changed = true;
            } else if (it->second != mask && it->second != ~u64{0}) {
                it->second = ~u64{0};
                changed = true;
            }
        }
        if (changed) {
            generation.fetch_add(1, std::memory_order_release);
        }
    }

    std::optional<u64> Lookup(Identity hash) {
        struct Entry {
            Identity hash{};
            u32 generation{~u32{0}};
            u64 mask{};
            bool known{};
        };
        struct Bucket {
            std::array<Entry, 4> ways{};
            unsigned next_slot{};
        };
        static thread_local std::array<Bucket, 16> buckets{};
        static thread_local Entry last{};

        const u32 current = generation.load(std::memory_order_acquire);
        if (last.hash == hash && last.generation == current) {
            return last.known ? std::optional<u64>{last.mask} : std::nullopt;
        }
        auto& bucket =
            buckets[(hash[0] ^ std::rotl(hash[1], 11)) & (buckets.size() - 1)];
        for (const auto& entry : bucket.ways) {
            if (entry.hash == hash && entry.generation == current) {
                last = entry;
                return entry.known ? std::optional<u64>{entry.mask} : std::nullopt;
            }
        }

        Entry entry{};
        entry.hash = hash;
        entry.generation = current;
        {
            const std::scoped_lock lock{mutex};
            if (const auto it = masks.find(hash); it != masks.end()) {
                entry.mask = it->second;
                entry.known = true;
            }
        }
        bucket.ways[bucket.next_slot] = entry;
        bucket.next_slot = (bucket.next_slot + 1) & (bucket.ways.size() - 1);
        last = entry;
        return entry.known ? std::optional<u64>{entry.mask} : std::nullopt;
    }
};

int main() {
    Registry registry;
    constexpr Identity shader{0, 0x12345678};
    constexpr u64 partial = (u64{3} << 0) | (u64{3} << 6);

    assert(!registry.Lookup(shader));
    registry.Publish(shader, partial);
    assert(registry.Lookup(shader) == partial);

    // Exercise alternating identities that collide in the 16-bucket memo.
    for (u64 i = 1; i <= 96; ++i) {
        const Identity id{i << 16, i};
        registry.Publish(id, u64{3} << ((i % 32) * 2));
        assert(registry.Lookup(id).has_value());
    }
    assert(registry.Lookup(shader) == partial);

    // Multiple GPU-owner-like readers must observe the published mask.
    std::atomic<unsigned> ready{0};
    std::atomic_bool go{false};
    std::vector<std::thread> readers;
    for (unsigned i = 0; i < 16; ++i) {
        readers.emplace_back([&] {
            ready.fetch_add(1, std::memory_order_release);
            while (!go.load(std::memory_order_acquire)) {
                std::this_thread::yield();
            }
            for (unsigned n = 0; n < 5000; ++n) {
                const auto value = registry.Lookup(shader);
                assert(value && *value == partial);
            }
        });
    }
    while (ready.load(std::memory_order_acquire) != readers.size()) {
        std::this_thread::yield();
    }
    go.store(true, std::memory_order_release);
    for (auto& reader : readers) {
        reader.join();
    }

    // Conflicting metadata/hash collision must widen, never narrow. The
    // generation change invalidates thread-local positive memo entries.
    registry.Publish(shader, u64{3} << 20);
    assert(registry.Lookup(shader) == ~u64{0});
    const u32 widened_generation = registry.generation.load(std::memory_order_acquire);

    // Once fail-safe, later publications cannot narrow the identity again.
    registry.Publish(shader, partial);
    assert(registry.generation.load(std::memory_order_acquire) == widened_generation);
    assert(registry.Lookup(shader) == ~u64{0});

    // Fill bounded metadata; overflow identities stay on the historical
    // unknown/unmasked behavior and cannot inherit a colliding memo entry.
    for (u64 i = 1000; registry.masks.size() < Registry::MaxMasks; ++i) {
        registry.Publish({0, i}, u64{3} << ((i % 32) * 2));
    }
    const auto full_size = registry.masks.size();
    constexpr Identity unknown{0, 0xfeedface};
    registry.Publish(unknown, partial);
    assert(registry.masks.size() == full_size);
    assert(!registry.Lookup(unknown));
}
