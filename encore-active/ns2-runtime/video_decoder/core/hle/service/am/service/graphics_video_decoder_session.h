// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <deque>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <vector>

#include "core/hle/service/service.h"
#include "core/hle/service/kernel_helpers.h"
#include "core/hle/kernel/k_auto_object.h"
#include "core/hle/kernel/k_transfer_memory.h"
#include "video_core/host1x/nx2_video_decoder.h"

namespace Kernel { class KEvent; }
namespace Service::AM {
class IGraphicsSupervisor;

// A process-owned, bounded nn::codec::video session. It consumes real
// FFmpeg frames and writes only to the owner's live 911 decoder-image pools.
// Missing codec, buffer, mapping and syncpoint states fail closed.
class IGraphicsVideoDecoderSession final : public ServiceFramework<IGraphicsVideoDecoderSession> {
public:
    IGraphicsVideoDecoderSession(Core::System& system,
                                 std::shared_ptr<IGraphicsSupervisor> owner, bool creator);
    ~IGraphicsVideoDecoderSession() override;
    Result HandleSyncRequest(Kernel::KServerSession& session, HLERequestContext& ctx) override;

private:
    static constexpr u32 NoSyncpoint = ~u32{0};
    static constexpr std::size_t MaxFrames = 16;
    static constexpr std::size_t MaxOutputs = 128;
    struct ReleaseFence { u32 id{}; u32 value{}; };
    struct AvailableImage {
        u32 id{};
        std::optional<ReleaseFence> release;
    };
    struct PendingPicture {
        Tegra::Host1x::Nx2Picture picture;
        bool eos{};
    };
    struct Output {
        u32 image{};
        u32 width{};
        u32 height{};
        u64 pts{};
        bool eos{};
    };
    void CreateDecoderService(HLERequestContext& ctx);
    void Initialize(HLERequestContext& ctx);
    void Configure(HLERequestContext& ctx);
    void Start(HLERequestContext& ctx);
    void Stop(HLERequestContext& ctx);
    void Finalize(HLERequestContext& ctx);
    void GetEvent(HLERequestContext& ctx);
    void RegisterBuffer(HLERequestContext& ctx);
    void ReleaseBuffer(HLERequestContext& ctx);
    void UnregisterBuffer(HLERequestContext& ctx);
    void SubmitInput(HLERequestContext& ctx);
    void GetOutputInfo(HLERequestContext& ctx);
    void Pump();
    void Acknowledge(HLERequestContext& ctx, Result result);
    [[nodiscard]] bool EnsureSyncpoint();
    [[nodiscard]] std::optional<std::array<u32, 4>> ReserveFence();
    std::shared_ptr<IGraphicsSupervisor> m_owner;
    KernelHelpers::ServiceContext m_context;
    // A session-owned kernel reference: guest closing its original COPY
    // handle must not finalize the transfer memory during active decoding.
    Kernel::KScopedAutoObject<Kernel::KTransferMemory> m_work_buffer;
    Kernel::KEvent* m_event{};
    std::unique_ptr<Tegra::Host1x::Nx2VideoDecoder> m_decoder;
    std::deque<PendingPicture> m_pending;
    std::deque<AvailableImage> m_available;
    std::deque<Output> m_outputs;
    std::set<u32> m_registered;
    std::set<u32> m_guest_owned;
    u32 m_syncpoint{NoSyncpoint};
    u32 m_reserved_value{};
    u64 m_submitted{};
    bool m_creator{};
    bool m_initialized{};
    bool m_started{};
    bool m_finalized{};
};
} // namespace Service::AM
