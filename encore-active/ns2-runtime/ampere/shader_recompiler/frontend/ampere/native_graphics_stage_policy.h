// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
// Only independent native graphics stages can safely produce a standalone
// SPIR-V module. Other stages need pipeline state, interstage linkage and/or
// a separate QMD decoder; never reinterpret them as a VertexB shader.
// Maintained Eden stage.h uses the global size_t from the C header.
#include <stddef.h>
#include "shader_recompiler/stage.h"

namespace Shader::Ampere {
[[nodiscard]] constexpr bool IsStandaloneNativeGraphicsStage(Shader::Stage stage) noexcept {
    return stage == Shader::Stage::VertexB || stage == Shader::Stage::Fragment;
}
[[nodiscard]] constexpr bool RequiresNativeVertexMerge(Shader::Stage stage) noexcept {
    return stage == Shader::Stage::VertexA;
}

// Maxwell graphics_key.unique_hashes has six slots: VertexA, VertexB,
// tessellation control/evaluation, geometry and Fragment. Native Ampere
// tessellation requires the linked control + evaluation pair. It is admitted
// exclusively by the real Vulkan pipeline builder, where the guest tess
// state and adjacent stage interfaces are available. Standalone compilation
// must not fabricate a tessellation or geometry RuntimeInfo.
// A pipeline requires a VertexB implementation, including when
// rasterization is discarded; a standalone VertexA program must never be
// sent to the Vulkan pipeline builder as a fictitious vertex shader.
[[nodiscard]] constexpr bool NativeGraphicsStageMaskSupported(u32 mask) noexcept {
    constexpr u32 vertex_b = u32{1} << 1;
    constexpr u32 tessellation = (u32{1} << 2) | (u32{1} << 3);
    constexpr u32 available = (u32{1} << 0) | vertex_b | tessellation |
                              (u32{1} << 4) | (u32{1} << 5);
    const u32 active_tessellation = mask & tessellation;
    return (mask & vertex_b) != 0 && (mask & ~available) == 0 &&
           (active_tessellation == 0 || active_tessellation == tessellation);
}

// NVN SET_MESH_CONTROL selects an alternate *linked* graphics pipeline.
// VertexB binds Mesh directly when there is no Task shader; when TessEval
// is also bound, VertexB binds Task and TessEval binds Mesh. Geometry is a
// pass-through output-header, not a third executable shader stage. Never
// interpret TessControl as Task or allow unpaired native stages.
[[nodiscard]] constexpr bool NativeMeshGraphicsStageMaskSupported(
    u32 mask, bool mesh_control) noexcept {
    if (!mesh_control) return NativeGraphicsStageMaskSupported(mask);
    constexpr u32 vertex_b = 1U << 1;
    constexpr u32 tess_control = 1U << 2;
    constexpr u32 allowed = vertex_b | (1U << 3) | (1U << 4) | (1U << 5);
    return (mask & vertex_b) != 0 && (mask & tess_control) == 0 &&
           (mask & ~allowed) == 0;
}
[[nodiscard]] constexpr bool NativeMeshTaskPresent(u32 mask, bool mesh_control) noexcept {
    return mesh_control && NativeMeshGraphicsStageMaskSupported(mask, true) &&
           (mask & (1U << 3)) != 0;
}
static_assert(NativeMeshGraphicsStageMaskSupported(0x02, true));
static_assert(NativeMeshGraphicsStageMaskSupported(0x22, true));
static_assert(NativeMeshGraphicsStageMaskSupported(0x0a, true));
static_assert(NativeMeshGraphicsStageMaskSupported(0x3a, true));
static_assert(!NativeMeshGraphicsStageMaskSupported(0x03, true));
static_assert(!NativeMeshGraphicsStageMaskSupported(0x06, true));
static_assert(!NativeMeshGraphicsStageMaskSupported(0x0e, true));
static_assert(!NativeMeshGraphicsStageMaskSupported(0x00, true));
static_assert(!NativeMeshGraphicsStageMaskSupported(0x12, true));
static_assert(NativeMeshTaskPresent(0x0a, true));
static_assert(!NativeMeshTaskPresent(0x02, true));
static_assert(!NativeMeshTaskPresent(0x0a, false));

// Maxwell's graphics CBUF stage banks are VertexA/VertexB=0,
// TessControl=1, TessEval=2, Geometry=3, Fragment=4. The shared IR
// stage enum follows that order except VertexA, which is appended.
[[nodiscard]] constexpr u32 NativeGraphicsCbufBank(Shader::Stage stage) noexcept {
    // The appended NVN Task stage owns the VertexB CBUF bank.
    // An NVN Mesh stage is currently admissible only in the TessEval
    // slot with a matching linked Task stage; solo Mesh-in-VertexB must
    // wait for a verified slot owner rather than guessing its CBUF bank.
    const u32 stage_id = static_cast<u32>(stage);
    const u32 vertex_a = static_cast<u32>(Shader::Stage::VertexA);
    return stage == Shader::Stage::VertexA || stage_id == vertex_a + 1U ? 0U :
           stage_id == vertex_a + 2U ? 2U :
           stage <= Shader::Stage::Fragment ? stage_id : ~u32{0};
}

static_assert(NativeGraphicsStageMaskSupported(0x02)); // VertexB only
static_assert(NativeGraphicsStageMaskSupported(0x03)); // VertexA + VertexB
static_assert(NativeGraphicsStageMaskSupported(0x22)); // VertexB + Fragment
static_assert(NativeGraphicsStageMaskSupported(0x23)); // merged + Fragment
static_assert(NativeGraphicsStageMaskSupported(0x12)); // VertexB + Geometry
static_assert(NativeGraphicsStageMaskSupported(0x13)); // merged + Geometry
static_assert(NativeGraphicsStageMaskSupported(0x32)); // Geometry + Fragment
static_assert(NativeGraphicsStageMaskSupported(0x33)); // merged + Geometry + Fragment
static_assert(NativeGraphicsStageMaskSupported(0x0e)); // linked TessControl + TessEval
static_assert(NativeGraphicsStageMaskSupported(0x2e)); // linked tessellation + Fragment
static_assert(NativeGraphicsStageMaskSupported(0x3f)); // all linked graphics stages
static_assert(!NativeGraphicsStageMaskSupported(0x00));
static_assert(!NativeGraphicsStageMaskSupported(0x01)); // unmerged VertexA
static_assert(!NativeGraphicsStageMaskSupported(0x20)); // Fragment only
static_assert(!NativeGraphicsStageMaskSupported(0x21)); // VertexA + Fragment
static_assert(!NativeGraphicsStageMaskSupported(0x06)); // control without evaluation
static_assert(!NativeGraphicsStageMaskSupported(0x0a)); // eval without control
static_assert(NativeGraphicsCbufBank(Shader::Stage::VertexA) == 0U);
static_assert(NativeGraphicsCbufBank(Shader::Stage::VertexB) == 0U);
static_assert(NativeGraphicsCbufBank(Shader::Stage::TessellationControl) == 1U);
static_assert(NativeGraphicsCbufBank(Shader::Stage::TessellationEval) == 2U);
static_assert(NativeGraphicsCbufBank(Shader::Stage::Geometry) == 3U);
static_assert(NativeGraphicsCbufBank(Shader::Stage::Fragment) == 4U);
static_assert(NativeGraphicsCbufBank(Shader::Stage::Compute) == ~u32{0});
static_assert(NativeGraphicsCbufBank(
    static_cast<Shader::Stage>(static_cast<u32>(Shader::Stage::VertexA) + 1U)) == 0U);
static_assert(NativeGraphicsCbufBank(
    static_cast<Shader::Stage>(static_cast<u32>(Shader::Stage::VertexA) + 2U)) == 2U);
static_assert(IsStandaloneNativeGraphicsStage(Shader::Stage::VertexB));
static_assert(IsStandaloneNativeGraphicsStage(Shader::Stage::Fragment));
static_assert(!IsStandaloneNativeGraphicsStage(Shader::Stage::VertexA));
static_assert(!IsStandaloneNativeGraphicsStage(Shader::Stage::Compute));
static_assert(!IsStandaloneNativeGraphicsStage(Shader::Stage::Geometry));
static_assert(!IsStandaloneNativeGraphicsStage(Shader::Stage::TessellationControl));
static_assert(!IsStandaloneNativeGraphicsStage(Shader::Stage::TessellationEval));
} // namespace Shader::Ampere
