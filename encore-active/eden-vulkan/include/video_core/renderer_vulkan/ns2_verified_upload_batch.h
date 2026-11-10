// SPDX-License-Identifier: GPL-3.0-or-later
// Native PS5 research: bounded in-order transfer batch admission.
// Header-only owner, NOT active until Scheduler/BufferCache wiring is qualified.
// Do not aggregate copies without authenticated allocation and epoch metadata.
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>

namespace Vulkan::NS2Research {

using U64 = std::uint64_t;

struct VerifiedCopy {
    // Stable nonzero IDs for the actual VkBuffer and underlying memory
    // allocation. GPU addresses alone are NOT unique memory-owner evidence.
    U64 source_buffer{};
    U64 destination_buffer{};
    U64 source_allocation{};
    U64 destination_allocation{};
    U64 source_backing_bytes{};
    U64 destination_backing_bytes{};
    U64 source_offset{};
    U64 destination_offset{};
    U64 bytes{};
    U64 channel_generation{};
    U64 queue_generation{};
    // Must be false if source/destination mapping is ambiguous or if the
    // caller needs a standalone semaphore, barrier or ordered copy command.
    bool allow_reorder{};
};

struct UploadGroup {
    U64 source_buffer{};
    U64 destination_buffer{};
    std::size_t first{};
    std::size_t count{};
};

class VerifiedUploadBatch final {
public:
    static constexpr std::size_t MaxCopies = 64;
    [[nodiscard]] constexpr bool Empty() const noexcept { return count_ == 0; }
    [[nodiscard]] constexpr std::size_t Size() const noexcept { return count_; }
    [[nodiscard]] constexpr std::size_t GroupCount() const noexcept { return groups_; }
    [[nodiscard]] constexpr std::span<const VerifiedCopy> Copies() const noexcept {
        return {copies_.data(), count_};
    }
    [[nodiscard]] constexpr std::span<const UploadGroup> Groups() const noexcept {
        return {groups_storage_.data(), groups_};
    }

    // Failure means "flush pending if any; run this copy on the original
    // scheduler path". It never changes the input command or guest state.
    [[nodiscard]] constexpr bool CanAppend(const VerifiedCopy& next) const noexcept {
        if (!Valid(next) || count_ >= MaxCopies) {
            return false;
        }
        if (count_ != 0) {
            if (next.channel_generation != channel_generation_ ||
                next.queue_generation != queue_generation_) {
                return false;
            }
        }
        for (std::size_t i = 0; i < count_; ++i) {
            const auto& pending = copies_[i];
            // Across a later transfer, an earlier destination must not be
            // used as the next source (RAW), and an earlier source must not
            // be overwritten by the next destination (WAR).
            if (next.source_allocation == pending.destination_allocation ||
                next.destination_allocation == pending.source_allocation) {
                return false;
            }
            // Two writes to one allocation may batch only with proven
            // non-overlapping destination ranges. All sizes were checked
            // with subtraction before reaching this comparison.
            if (next.destination_allocation == pending.destination_allocation &&
                Overlap(next.destination_offset, next.bytes,
                        pending.destination_offset, pending.bytes)) {
                return false;
            }
        }
        return true;
    }

    [[nodiscard]] constexpr bool Append(const VerifiedCopy& next) noexcept {
        if (!CanAppend(next)) {
            return false;
        }
        if (count_ == 0) {
            channel_generation_ = next.channel_generation;
            queue_generation_ = next.queue_generation;
        }
        if (groups_ != 0 && groups_storage_[groups_ - 1].source_buffer == next.source_buffer &&
            groups_storage_[groups_ - 1].destination_buffer == next.destination_buffer) {
            ++groups_storage_[groups_ - 1].count;
        } else {
            groups_storage_[groups_++] = UploadGroup{next.source_buffer, next.destination_buffer,
                                                     count_, 1};
        }
        copies_[count_++] = next;
        return true;
    }

    constexpr void Clear() noexcept {
        count_ = 0;
        groups_ = 0;
        channel_generation_ = 0;
        queue_generation_ = 0;
    }

private:
    [[nodiscard]] static constexpr bool Valid(const VerifiedCopy& c) noexcept {
        if (!c.allow_reorder || !c.source_buffer || !c.destination_buffer ||
            !c.source_allocation || !c.destination_allocation ||
            c.source_allocation == c.destination_allocation ||
            !c.channel_generation || !c.queue_generation || !c.bytes ||
            c.source_offset > c.source_backing_bytes ||
            c.destination_offset > c.destination_backing_bytes) {
            return false;
        }
        return c.bytes <= c.source_backing_bytes - c.source_offset &&
               c.bytes <= c.destination_backing_bytes - c.destination_offset;
    }
    [[nodiscard]] static constexpr bool Overlap(U64 start, U64 length,
                                                U64 other, U64 other_length) noexcept {
        // Valid() has already proven that [start, start+length) stays inside
        // the actual finite backing. Subtraction avoids unsigned wrap.
        return start <= other ? other - start < length
                              : start - other < other_length;
    }
    std::array<VerifiedCopy, MaxCopies> copies_{};
    std::array<UploadGroup, MaxCopies> groups_storage_{};
    std::size_t count_{};
    std::size_t groups_{};
    U64 channel_generation_{};
    U64 queue_generation_{};
};

} // namespace Vulkan::NS2Research
