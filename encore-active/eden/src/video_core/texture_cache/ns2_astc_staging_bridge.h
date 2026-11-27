// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
// N59: host-tested bounded ASTC staging transaction over N55's page-safe planner.
// Callback backend is deliberately external: no speculative Vulkan guest access.
#include <cstdint>
#include <limits>
#include <span>
#include <vector>
#include "video_core/texture_cache/ns2_astc_layer_plan.h"

namespace Eden::NS2::Eglantine {

enum class AstcTransferStatus {
    InvalidPlan,
    NoChanges,
    FullUploadRequired,
    Completed,
    StagingFailure,
    ReadFailure,
    DispatchFailure,
    ConcurrentWrite,
};
struct AstcTransferResult {
    AstcTransferStatus status{AstcTransferStatus::InvalidPlan};
    std::uint64_t uploaded_bytes{};
    std::uint32_t dispatched_batches{};
};

// The callbacks MUST complete their work synchronously before returning:
// begin_batch(batch_id, total_bytes)
// read_guest(guest_gpu_address, staging_offset, byte_count)
// decode_batch(batch_id, span<layer indices>)
// If a callback fails after GPU submission may have begun, force a full
// upload on the next frame. Planner state commits only after all batches pass.
template<class BeginBatch, class ReadGuest, class DecodeBatch>
[[nodiscard]] AstcTransferResult UploadAstcDirtyLayerBatches(
    AstcDirtyLayerPlanner& planner, std::uint64_t guest_gpu_base,
    BeginBatch&& begin_batch, ReadGuest&& read_guest,
    DecodeBatch&& decode_batch) {
    const LayerPlan plan = planner.Snapshot();
    AstcTransferResult out{};
    if (plan.status == LayerPlanStatus::Invalid) return out;
    if (plan.status == LayerPlanStatus::NoChanges) {
        out.status = AstcTransferStatus::NoChanges;
        return out;
    }
    if (plan.status == LayerPlanStatus::FullUpload) {
        out.status = AstcTransferStatus::FullUploadRequired;
        return out;
    }
    const auto max = (std::numeric_limits<std::uint64_t>::max)();
    try {
    for (std::size_t b = 0; b < plan.batch_bytes.size(); ++b) {
        const auto bytes = plan.batch_bytes[b];
        if (!begin_batch(static_cast<std::uint32_t>(b), bytes)) {
            planner.Invalidate();
            out.status = AstcTransferStatus::StagingFailure;
            return out;
        }
        std::vector<std::uint32_t> layers;
        for (const auto& copy : plan.copies) {
            if (copy.batch != b) continue;
            // Even if the planner currently guarantees these invariants,
            // preserve a distinct read-backend trust boundary.
            if (copy.guest_offset > max - guest_gpu_base ||
                copy.staging_offset > bytes || copy.bytes > bytes - copy.staging_offset) {
                planner.Invalidate();
                out.status = AstcTransferStatus::InvalidPlan;
                return out;
            }
            if (!read_guest(guest_gpu_base + copy.guest_offset,
                            copy.staging_offset, copy.bytes)) {
                planner.Invalidate();
                out.status = AstcTransferStatus::ReadFailure;
                return out;
            }
            layers.push_back(copy.layer);
            out.uploaded_bytes += copy.bytes;
        }
        if (layers.empty() ||
            !decode_batch(static_cast<std::uint32_t>(b),
                          std::span<const std::uint32_t>{layers})) {
            planner.Invalidate();
            out.status = AstcTransferStatus::DispatchFailure;
            return out;
        }
        ++out.dispatched_batches;
    }
    } catch (...) {
        // An exception during staging/dispatch leaves partial GPU updates
        // possible. Require a full upload before returning control upstream.
        planner.Invalidate();
        throw;
    }
    if (!planner.Commit(plan)) {
        // Concurrent guest writes after the initial snapshot are still dirty.
        out.status = AstcTransferStatus::ConcurrentWrite;
        return out;
    }
    out.status = AstcTransferStatus::Completed;
    return out;
}

} // namespace Eden::NS2::Eglantine
