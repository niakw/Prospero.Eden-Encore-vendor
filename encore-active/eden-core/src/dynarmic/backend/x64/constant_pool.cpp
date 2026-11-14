// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

/* This file is part of the dynarmic project.
 * Copyright (c) 2016 MerryMage
 * SPDX-License-Identifier: 0BSD
 */

#include <cstring>

#include "common/assert.h"

#include "dynarmic/backend/x64/block_of_code.h"
#include "dynarmic/backend/x64/constant_pool.h"

namespace Dynarmic::Backend::X64 {

ConstantPool::ConstantPool(BlockOfCode& code, size_t size)
{
    code.EnsureMemoryCommitted(align_size + size);
    code.int3();
    code.align(align_size);
    pool = std::span<ConstantT>(reinterpret_cast<ConstantT*>(code.AllocateFromCodeSpace(size)), size / align_size);
}

Xbyak::Address ConstantPool::GetConstant(BlockOfCode& code, const Xbyak::AddressFrame& frame, u64 lower, u64 upper) {
    const auto constant = ConstantT(lower, upper);
    // Track the slot index rather than a duplicate pointer. Retain the
    // PS5 W^X writable alias: never write constants directly into RX pages.
    const auto next_slot = constant_info.size();
    auto it = constant_info.find(constant);
    if (it == constant_info.end()) {
        ASSERT(next_slot < pool.size());
        const auto slot = static_cast<u32>(next_slot);
        ConstantT& target_constant = pool[slot];
        *reinterpret_cast<ConstantT*>(code.writableAddress(&target_constant)) = constant;
        it = constant_info.emplace(constant, slot).first;
    }
    return frame[code.rip + static_cast<void*>(&pool[it->second])];
}

}  // namespace Dynarmic::Backend::X64
