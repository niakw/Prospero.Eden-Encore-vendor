// SPDX-License-Identifier: MIT
// Split-Swin Q/K/V -> 64-token window attention: exact GPU handoff contract.
// This covers one mathematical pair, not the full Original/Compact graph.
#pragma once
#include "rdna2_nr_qkv_projection_policy.h"
#include "rdna2_nr_window_attention_policy.h"

namespace Eden::Graphics::NeuralRDNA2 {
struct SplitWindowLink {
    QkvProjection qkv{};
    WindowAttention attention{};
};
struct SplitWindowLinkStorage {
    QkvStorage qkv{};
    AttentionStorage attention{};
};
// The output of the first dispatch must be the exact FP8 input consumed by
// the second. A shape-valid but disconnected pair is not a working layer.
[[nodiscard]] constexpr bool CanSplitWindowLink(
    const SplitWindowLink& plan,
    const SplitWindowLinkStorage& buffers) noexcept {
    return CanQkvProjection(plan.qkv,buffers.qkv) &&
           CanWindowAttention(plan.attention,buffers.attention) &&
           plan.qkv.windows==plan.attention.windows &&
           plan.qkv.heads==plan.attention.heads &&
           plan.qkv.q_base_bytes==plan.attention.q_base_bytes &&
           plan.qkv.k_base_bytes==plan.attention.k_base_bytes &&
           plan.qkv.v_base_bytes==plan.attention.v_base_bytes &&
           buffers.qkv.q_bytes==buffers.attention.q_bytes &&
           buffers.qkv.k_bytes==buffers.attention.k_bytes &&
           buffers.qkv.v_bytes==buffers.attention.v_bytes;
}
} // namespace Eden::Graphics::NeuralRDNA2
