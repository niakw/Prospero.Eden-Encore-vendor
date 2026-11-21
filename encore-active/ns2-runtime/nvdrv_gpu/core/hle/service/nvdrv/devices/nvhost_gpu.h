// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

// SPDX-FileCopyrightText: Copyright 2018 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <memory>
#include <vector>
#include "common/bit_field.h"
#include "common/common_funcs.h"
#include "common/common_types.h"
#include "common/swap.h"
#include "core/hle/service/nvdrv/devices/nvdevice.h"
#include "core/hle/service/nvdrv/nvdata.h"
#include "video_core/dma_pusher.h"

namespace Core::Memory {
class Memory;
}

namespace Tegra {
namespace Control {
struct ChannelState;
}
} // namespace Tegra

namespace Service::Nvidia {

namespace NvCore {
class Container;
class NvMap;
class SyncpointManager;
} // namespace NvCore

class EventInterface;
} // namespace Service::Nvidia

namespace Service::Nvidia::Devices {

class nvhost_as_gpu;
class nvmap;
class nvhost_gpu final : public nvdevice {
public:
    explicit nvhost_gpu(Core::System& system_, EventInterface& events_interface_,
                        NvCore::Container& core);
    ~nvhost_gpu() override;

    NvResult Ioctl1(DeviceFD fd, Ioctl command, std::span<const u8> input,
                    std::span<u8> output) override;
    NvResult Ioctl2(DeviceFD fd, Ioctl command, std::span<const u8> input,
                    std::span<const u8> inline_input, std::span<u8> output) override;
    NvResult Ioctl3(DeviceFD fd, Ioctl command, std::span<const u8> input, std::span<u8> output,
                    std::span<u8> inline_output) override;

    void OnOpen(NvCore::SessionId session_id, DeviceFD fd) override;
    void OnClose(DeviceFD fd) override;
    bool CanBindNativeTsgChannel(DeviceFD fd, NvCore::SessionId session) override;
    bool IsGpuChannelDevice() const override { return true; }

    Kernel::KEvent* QueryEvent(u32 event_id) override;

private:
    friend class nvhost_as_gpu;
    enum class CtxClasses : u32_le {
        Ctx2D = 0x902D,
        Ctx3D = 0xB197,
        CtxCompute = 0xB1C0,
        CtxKepler = 0xA140,
        CtxDMA = 0xB0B5,
        CtxChannelGPFIFO = 0xB06F,
    };

    enum class ChannelPriority : u32 {
        Low = 0x32,
        Medium = 0x64,
        High = 0x96,
    };

    struct IoctlSetNvmapFD {
        s32_le nvmap_fd{};
    };
    static_assert(sizeof(IoctlSetNvmapFD) == 4, "IoctlSetNvmapFD is incorrect size");

    struct IoctlChannelOpen {
        s32_le runlist_id{};
    };
    static_assert(sizeof(IoctlChannelOpen) == 4, "IoctlChannelOpen ABI size");
    // Switch 2 NVGPU_IOCTL_CHANNEL_SETUP_BIND (0xC0504821), 80-byte INOUT ABI.
    // Unlike older Linux layouts, the final 40 bytes expose usermode GPU VAs
    // and the hardware channel id, followed by only three reserved words.
    struct IoctlChannelSetupBind {
        u32_le num_gpfifo_entries{};
        u32_le num_inflight_jobs{};
        u32_le flags{};
        s32_le userd_dmabuf_fd{};
        s32_le gpfifo_dmabuf_fd{};
        u32_le work_submit_token{};
        u64_le userd_dmabuf_offset{};
        u64_le gpfifo_dmabuf_offset{};
        u64_le gpfifo_gpu_va{};
        u64_le userd_gpu_va{};
        u64_le usermode_mmio_gpu_va{};
        u32_le hw_channel_id{};
        std::array<u32_le, 3> reserved{};
    };
    static_assert(sizeof(IoctlChannelSetupBind) == 80, "IoctlChannelSetupBind ABI size");

    // NVIDIA nvgpu UAPI: 0xC010481E, exactly 16 output bytes.
    // The address must name the channel syncpoint's actual GMMU shim word.
    struct IoctlGetUserSyncpoint {
        u64_le gpu_va{};
        u32_le syncpoint_id{};
        u32_le syncpoint_max{};
    };
    static_assert(sizeof(IoctlGetUserSyncpoint) == 16);

    struct IoctlChannelSetTimeout {
        u32_le timeout{};
    };
    static_assert(sizeof(IoctlChannelSetTimeout) == 4, "IoctlChannelSetTimeout is incorrect size");

    struct IoctlAllocGPFIFO {
        u32_le num_entries{};
        u32_le flags{};
    };
    static_assert(sizeof(IoctlAllocGPFIFO) == 8, "IoctlAllocGPFIFO is incorrect size");

    struct IoctlClientData {
        u64_le data{};
    };
    static_assert(sizeof(IoctlClientData) == 8, "IoctlClientData is incorrect size");

    struct IoctlZCullBind {
        u64_le gpu_va{};
        u32_le mode{}; // 0=global, 1=no_ctxsw, 2=separate_buffer, 3=part_of_regular_buf
        INSERT_PADDING_WORDS(1);
    };
    static_assert(sizeof(IoctlZCullBind) == 16, "IoctlZCullBind is incorrect size");

    struct IoctlSetErrorNotifier {
        u64_le offset{};
        u64_le size{};
        u32_le mem{}; // nvmap object handle
        INSERT_PADDING_WORDS(1);
    };
    static_assert(sizeof(IoctlSetErrorNotifier) == 24, "IoctlSetErrorNotifier is incorrect size");

    struct IoctlChannelSetPriority {
        u32_le priority{};
    };
    static_assert(sizeof(IoctlChannelSetPriority) == 4,
                  "IoctlChannelSetPriority is incorrect size");

    struct IoctlSetTimeslice {
        u32_le timeslice{};
    };
    static_assert(sizeof(IoctlSetTimeslice) == 4, "IoctlSetTimeslice is incorrect size");

    struct IoctlEventIdControl {
        u32_le cmd{}; // 0=disable, 1=enable, 2=clear
        u32_le id{};
    };
    static_assert(sizeof(IoctlEventIdControl) == 8, "IoctlEventIdControl is incorrect size");

    struct IoctlGetErrorNotification {
        u64_le timestamp{};
        u32_le info32{};
        u16_le info16{};
        u16_le status{}; // 0 = healthy; non-zero = channel/GPU error
    };
    static_assert(sizeof(IoctlGetErrorNotification) == 16,
                  "IoctlGetErrorNotification is incorrect size");

    static_assert(sizeof(NvFence) == 8, "Fence is incorrect size");

    struct IoctlAllocGpfifoEx {
        u32_le num_entries{}; // in
        u32_le num_jobs{};    // in
        u32_le flags{};       // in
        NvFence fence_out{};  // out
        std::array<u32_le, 3> reserved{};   // in and ingored (for now) according to switch brew
    };
    static_assert(sizeof(IoctlAllocGpfifoEx) == 32, "IoctlAllocGpfifoEx2 is incorrect size");

    struct IoctlAllocObjCtx {
        u32_le class_num{}; // 0x902D=2d, 0xB197=3d, 0xB1C0=compute, 0xA140=kepler, 0xB0B5=DMA,
                            // 0xB06F=channel_gpfifo
        u32_le flags{};
        u64_le obj_id{}; // (ignored) used for FREE_OBJ_CTX ioctl, which is not supported
    };
    static_assert(sizeof(IoctlAllocObjCtx) == 16, "IoctlAllocObjCtx is incorrect size");

    struct IoctlSubmitGpfifo {
        u64_le address{};     // pointer to gpfifo entry structs
        u32_le num_entries{}; // number of fence objects being submitted
        union {
            u32_le raw;
            BitField<0, 1, u32_le> fence_wait;      // append a wait sync_point to the list
            BitField<1, 1, u32_le> fence_increment; // append an increment to the list
            BitField<2, 1, u32_le> new_hw_format;   // mostly ignored
            BitField<4, 1, u32_le> suppress_wfi;    // suppress wait for interrupt
            BitField<8, 1, u32_le> increment_value; // increment the returned fence
        } flags;
        NvFence fence{}; // returned new fence object for others to wait on
    };
    static_assert(sizeof(IoctlSubmitGpfifo) == 16 + sizeof(NvFence),
                  "IoctlSubmitGpfifo is incorrect size");

    struct IoctlGetWaitbase {
        u32 unknown{}; // seems to be ignored? Nintendo added this
        u32 value{};
    };
    static_assert(sizeof(IoctlGetWaitbase) == 8, "IoctlGetWaitbase is incorrect size");

    s32_le nvmap_fd{};
    u64_le user_data{};
    IoctlZCullBind zcull_params{};
    std::array<std::optional<IoctlAllocObjCtx>, 6> ctxObjs{};
    u32_le channel_priority{};
    u32_le channel_timeslice{};
    // Switch 2 H:0x1F scheduling policy selected by the guest. Configuration
    // is persisted even though explicit hardware-style preempt remains unsupported.
    u32 native_graphics_preemption_mode{};
    u32 native_compute_preemption_mode{};
    // Native H:0x0C retains the exact NvMap backing just like nvgpu retains
    // its dma-buf. The offset is kept for future real GPU-error publication.
    std::optional<u32> native_error_notifier_handle{};
    u64 native_error_notifier_offset{};

    NvResult ChannelOpen(IoctlChannelOpen& params, DeviceFD fd);
    NvResult ChannelSetupBind(IoctlChannelSetupBind& params, DeviceFD fd);
    NvResult GetUserSyncpoint(IoctlGetUserSyncpoint& params, DeviceFD fd);
    NvResult SetNVMAPfd(IoctlSetNvmapFD& params);
    NvResult SetClientData(IoctlClientData& params);
    NvResult GetClientData(IoctlClientData& params);
    NvResult ZCullBind(IoctlZCullBind& params);
    NvResult SetErrorNotifier(IoctlSetErrorNotifier& params, DeviceFD fd);
    NvResult GetErrorNotification(IoctlGetErrorNotification& params, DeviceFD fd);
    NvResult SetChannelPriority(IoctlChannelSetPriority& params);
    // Authenticate the exact FD before changing channel/engine state. Native
    // sessions may not share a GPU channel with another guest process.
    [[nodiscard]] NvResult ValidateChannelOwner(DeviceFD fd, u64& program_id) const;
    NvResult AllocGPFIFOEx(IoctlAllocGpfifoEx& params, DeviceFD fd);
    NvResult AllocGPFIFOEx2(IoctlAllocGpfifoEx& params, DeviceFD fd);

    s32_le GetObjectContextClassNumberIndex(CtxClasses class_number);
    NvResult AllocateObjectContext(IoctlAllocObjCtx& params, DeviceFD fd);

    NvResult SubmitGPFIFOImpl(IoctlSubmitGpfifo& params, Tegra::CommandList&& entries, DeviceFD fd);
    [[nodiscard]] bool UsesNativeAbi(DeviceFD fd) const;

    Core::Memory::Memory& GetSessionMemory(DeviceFD fd);

    NvResult SubmitGPFIFOBase1(IoctlSubmitGpfifo& params,
                               std::span<Tegra::CommandListHeader> commands, DeviceFD fd,
                               bool kickoff = false);
    NvResult SubmitGPFIFOBase2(IoctlSubmitGpfifo& params,
                               std::span<const Tegra::CommandListHeader> commands, DeviceFD fd);

    NvResult GetWaitbase(IoctlGetWaitbase& params);
    NvResult ChannelSetTimeout(IoctlChannelSetTimeout& params);
    NvResult ChannelSetTimeslice(IoctlSetTimeslice& params);

    EventInterface& events_interface;
    NvCore::Container& core;
    NvCore::SyncpointManager& syncpoint_manager;
    NvCore::NvMap& nvmap;
    std::shared_ptr<Tegra::Control::ChannelState> channel_state;
    ::Common::unordered_map<DeviceFD, NvCore::SessionId> sessions;
    u32 channel_syncpoint;
    u32 native_submit_logs{};
    // An authenticated NS2 /dev/nvhost-gpu channel retains its selected
    // logical runlist until this NVDRV device is closed.
    std::optional<s32> native_runlist_id{};
    std::mutex channel_mutex;

    // Events
    Kernel::KEvent* sm_exception_breakpoint_int_report_event;
    Kernel::KEvent* sm_exception_breakpoint_pause_report_event;
    Kernel::KEvent* error_notifier_event;
};

} // namespace Service::Nvidia::Devices
