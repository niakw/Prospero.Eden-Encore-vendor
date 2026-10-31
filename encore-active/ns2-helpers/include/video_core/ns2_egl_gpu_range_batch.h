// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <cstdint>
#include <limits>
namespace Eden::NS2::Eglantine {
// Stateful run accumulator: gaps must flush, without invalidating unrelated PTEs.
class ChangedPageRuns {
public:
    explicit ChangedPageRuns(std::uint64_t page) noexcept : page_{page} {}
    template<class Notify>
    void Record(std::uint64_t address, bool modified, Notify&& notify) {
        if (!modified) { Flush(notify); return; }
        if (!page_ || address > (std::numeric_limits<std::uint64_t>::max)() - page_) {
            Flush(notify);
            return; // Invalid span; native GPU bounds remain authoritative.
        }
        if (size_ && (address < begin_ || address - begin_ != size_ ||
                      page_ > (std::numeric_limits<std::uint64_t>::max)() - size_)) {
            Flush(notify);
        }
        if (!size_) begin_ = address;
        size_ += page_;
    }
    template<class Notify>
    void Flush(Notify&& notify) {
        if (size_) { notify(begin_, size_); size_ = 0; begin_ = 0; }
    }
private:
    std::uint64_t begin_{};
    std::uint64_t size_{};
    const std::uint64_t page_;
};
} // namespace Eden::NS2::Eglantine
