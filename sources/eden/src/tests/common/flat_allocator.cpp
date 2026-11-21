// SPDX-License-Identifier: GPL-3.0-or-later
// Regression adapted from Lichii #1896/#1911 (ae4ea4d430).
#include <deque>
#include <vector>
#include <utility>
#include <catch2/catch_test_macros.hpp>
#include "common/address_space.h"

namespace {
using Allocator = Common::FlatAllocator<u32, 0, 32>;
constexpr u32 Begin = 0x20000;
constexpr u32 End = 0xe0000;
}

TEST_CASE("Streaming GPU VA frees oldest first and reuses its ranges", "[flat_allocator]") {
    Allocator allocator{Begin, End};
    std::vector<bool> in_use(End);
    std::deque<std::pair<u32, u32>> live;
    const u32 streamed = (End - Begin) * 4;
    for (u32 pages = 0, size = 1; pages < streamed; pages += size, size = size % 3 + 1) {
        const u32 address = allocator.Allocate(size);
        REQUIRE(address >= Begin);
        REQUIRE(address <= End - size);
        for (u32 i = address; i < address + size; ++i) {
            REQUIRE_FALSE(in_use[i]);
            in_use[i] = true;
        }
        live.emplace_back(address, size);
        if (live.size() > 64) {
            const auto [oldest, count] = live.front();
            allocator.Free(oldest, count);
            for (u32 i = oldest; i < oldest + count; ++i) in_use[i] = false;
            live.pop_front();
        }
    }
    for (const auto& [address, size] : live) allocator.Free(address, size);
    REQUIRE(allocator.Allocate(End - Begin) == Begin);
}

TEST_CASE("Dynamic GPU VA skips fixed gaps and eventually wraps", "[flat_allocator]") {
    Allocator allocator{0x10, 0x1000};
    REQUIRE(allocator.Allocate(8) == 0x10);
    allocator.AllocateFixed(0x28, 8);
    REQUIRE(allocator.Allocate(0x20) == 0x30);
    allocator.AllocateFixed(0x100, 8);
    REQUIRE(allocator.Allocate(0x100) == 0x108);
    REQUIRE(allocator.Allocate(0x10) == 0x208);
    REQUIRE(allocator.Allocate(0x1000) == 0);
}
