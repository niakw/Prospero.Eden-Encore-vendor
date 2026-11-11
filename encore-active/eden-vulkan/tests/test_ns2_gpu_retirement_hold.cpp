// SPDX-License-Identifier: GPL-3.0-or-later
// Exact host-only retention-contract tests, NOT a real GPU submission.
#include "ns2_gpu_retirement_hold.h"
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <utility>
#include <memory>
using Vulkan::NS2Research::GpuRetirementHold;
struct Owner {
    int* destruction_count{};
    explicit Owner(int* count) : destruction_count(count) {}
    Owner(const Owner&) = delete;
    Owner& operator=(const Owner&) = delete;
    Owner(Owner&& other) noexcept : destruction_count(std::exchange(other.destruction_count, nullptr)) {}
    Owner& operator=(Owner&& other) noexcept {
        if (this != &other) {
            if (destruction_count) ++*destruction_count;
            destruction_count = std::exchange(other.destruction_count, nullptr);
        }
        return *this;
    }
    ~Owner() { if (destruction_count) ++*destruction_count; }
};
int main() {
    int checks = 0;
    auto ck = [&](bool ok) {
        ++checks;
        if (!ok) throw std::runtime_error("GPU retention host test #" + std::to_string(checks));
    };
    int destroyed = 0;
    {
        GpuRetirementHold<Owner, 2> held;
        Owner a{&destroyed}, b{&destroyed}, c{&destroyed};
        ck(!held.TryRetain(std::move(a), 0));
        ck(a.destruction_count == &destroyed && held.Outstanding() == 0);
        ck(held.TryRetain(std::move(a), 5));
        ck(held.TryRetain(std::move(b), 6));
        ck(!held.TryRetain(std::move(c), 7));
        ck(c.destruction_count == &destroyed);
        ck(held.Outstanding() == 2 && destroyed == 0);
        ck(held.RetireCompleted(0) == 0);
        ck(held.RetireCompleted(4) == 0 && destroyed == 0);
        ck(held.RetireCompleted(5) == 1 && destroyed == 1);
        ck(held.Outstanding() == 1);
        ck(held.RetireCompleted(5) == 0 && destroyed == 1);
        ck(held.RetireCompleted(6) == 1 && destroyed == 2);
        ck(held.Outstanding() == 0);
        ck(held.TryRetain(std::move(c), 9));
        ck(held.RetireCompleted(8) == 0 && destroyed == 2);
        ck(held.RetireCompleted(9) == 1 && destroyed == 3);
    }
    ck(destroyed == 3);
    {
        GpuRetirementHold<Owner, 3> held;
        Owner a{&destroyed}, b{&destroyed};
        ck(held.TryRetain(std::move(a), 10));
        ck(!held.TryRetain(std::move(b), 9));
        ck(b.destruction_count == &destroyed);
        ck(held.TryRetain(std::move(b), 10));
        ck(held.RetireCompleted(9) == 0);
        ck(held.RetireCompleted(10) == 2 && held.Outstanding() == 0);
    }
    ck(destroyed == 5);
    {
        // Runtime's device-loss fallback deliberately abandons owners.
        GpuRetirementHold<std::unique_ptr<int>, 2> held;
        auto owner = std::make_unique<int>(73);
        int* escaped = owner.get();
        ck(held.TryRetain(std::move(owner), 17));
        ck(held.Outstanding() == 1 && owner == nullptr);
        held.AbandonUnsafeOwners();
        ck(held.Outstanding() == 0);
        // A simulated device loss does not own a real GPU. Clean the
        // deliberately abandoned fixture allocation to satisfy ASan/LSan.
        delete escaped;
    }
    std::cout << "PASS: " << checks << " strong-owner GPU retirement host checks\n";
}
