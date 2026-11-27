// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// A native BRX dispatch is a reviewed list of original CBUF words and exact
// source instruction indices. The plans carry no guest pointers and cannot
// replace independent 3D engine / GMMU / CBUF snapshot authentication.
#include <vector>
#include "common/common_types.h"

namespace Shader::Ampere {
struct NativeIndirectBranchTarget {
    u32 native_value{};
    u32 native_instruction_index{};
};
struct NativeIndirectBranchPlan {
    u32 branch_instruction_index{};
    // Exactly zero (native instruction zero) or 0x80 (graphics SPH).
    // The captured CBUF word must equal raw_pc_base + source_index*16.
    u32 raw_pc_base{};
    std::vector<NativeIndirectBranchTarget> targets;
};
} // namespace Shader::Ampere
