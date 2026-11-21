// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

// SPDX-FileCopyrightText: Copyright 2018 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <array>
#include <cstring>
#include <limits>
#include <new>
#include <atomic>
#include <cstdio>
#include "common/assert.h"
#include "common/logging.h"
#include "core/core.h"
#include "core/hle/kernel/k_process.h"
#include "core/hle/service/nvdrv/core/container.h"
#include "core/hle/service/nvdrv/core/nvmap.h"
#include "core/hle/service/nvdrv/core/syncpoint_manager.h"
#include "core/hle/service/nvdrv/devices/ioctl_serialization.h"
#include "core/hle/service/nvdrv/devices/nvhost_gpu.h"
#include "core/hle/service/nvdrv/devices/native_gpu_context.h"
#include "core/hle/service/nvdrv/devices/native_gpu_process_registry.h"
#include "core/hle/service/nvdrv/devices/native_gpu_channel_registry.h"
#include "core/hle/service/nvdrv/devices/native_gpu_submission.h"
#include "core/hle/service/nvdrv/nvdrv.h"
#include "core/memory.h"
#include "video_core/control/channel_state.h"
#include "video_core/engines/puller.h"
#include "video_core/gpu.h"
#include "video_core/host1x/host1x.h"
#include "video_core/memory_manager.h"
// GPU pushbuffer and syncpoint method types come from dma_pusher.h.

#ifdef PS5_NATIVE
extern "C" bool eden_native_detailed_logging() noexcept __attribute__((weak));
#endif

namespace Service::Nvidia::Devices {
namespace {
#ifdef PS5_NATIVE
constexpr unsigned GpfifoTraceLimit = 32;
std::atomic<unsigned> g_gpfifo_route_trace{0};
std::atomic<unsigned> g_gpfifo_base_trace{0};
std::atomic<unsigned> g_gpfifo_impl_trace{0};

[[nodiscard]] bool NextDetailedGpfifoTrace(std::atomic<unsigned>& counter,
                                           unsigned& ticket) noexcept {
    if (!eden_native_detailed_logging || !eden_native_detailed_logging())
        return false;
    ticket = counter.fetch_add(1, std::memory_order_relaxed) + 1;
    return ticket <= GpfifoTraceLimit;
}
#endif

Tegra::CommandHeader BuildFenceAction(Tegra::Engines::Puller::FenceOperation op, u32 syncpoint_id) {
    Tegra::Engines::Puller::FenceAction result{};
    result.op.Assign(op);
    result.syncpoint_id.Assign(syncpoint_id);
    return {result.raw};
}
} // namespace

nvhost_gpu::nvhost_gpu(Core::System& system_, EventInterface& events_interface_,
                       NvCore::Container& core_)
    : nvdevice{system_}, events_interface{events_interface_}, core{core_},
      syncpoint_manager{core_.GetSyncpointManager()}, nvmap{core.GetNvMapFile()},
      channel_state{system.GPU().AllocateChannel()} {
    channel_syncpoint = syncpoint_manager.AllocateSyncpoint(false);
    sm_exception_breakpoint_int_report_event =
        events_interface.CreateEvent("GpuChannelSMExceptionBreakpointInt");
    sm_exception_breakpoint_pause_report_event =
        events_interface.CreateEvent("GpuChannelSMExceptionBreakpointPause");
    error_notifier_event = events_interface.CreateEvent("GpuChannelErrorNotifier");
}

nvhost_gpu::~nvhost_gpu() {
    // Guest close: retire the exact bound channel on the GPU worker BEFORE
    // its syncpoint, rasterizer state and GMMU-backed NvMap pins are freed.
    // Global shutdown destroys GPU before NVDRV services, so the worker is
    // no longer available then; GPU::Impl teardown already drains it.
    // Empty leases are Maxwell/NS1 channels and stay on their old path.
    if (channel_state && channel_state->address_space_owner &&
        system.IsPoweredOn() && !system.IsShuttingDown()) {
        system.GPU().ReleaseChannel(*channel_state);
    }
    NativeGpuChannelRegistry::Instance().Unbind(channel_state.get());
    events_interface.FreeEvent(sm_exception_breakpoint_int_report_event);
    events_interface.FreeEvent(sm_exception_breakpoint_pause_report_event);
    events_interface.FreeEvent(error_notifier_event);
    syncpoint_manager.FreeSyncpoint(channel_syncpoint);
}

NvResult nvhost_gpu::Ioctl1(DeviceFD fd, Ioctl command, std::span<const u8> input,
                            std::span<u8> output) {
    switch (command.group) {
    case 0x0:
        switch (command.cmd) {
        case 0x3:
            return WrapFixed(this, &nvhost_gpu::GetWaitbase, input, output);
        default:
            break;
        }
        break;
    case 'H':
        switch (command.cmd) {
        case 0x1:
            return WrapFixed(this, &nvhost_gpu::SetNVMAPfd, input, output);
        case 0x3:
            return WrapFixed(this, &nvhost_gpu::ChannelSetTimeout, input, output);
        case 0x8: {
#ifdef PS5_NATIVE
            unsigned ticket{};
            if (NextDetailedGpfifoTrace(g_gpfifo_route_trace, ticket))
                std::fprintf(stderr,
                             "EDEN_NS2_GPFIFO_ROUTE ticket=%u route=ioctl1-submit input=%zu output=%zu\n",
                             ticket, input.size(), output.size());
#endif
            return WrapFixedVariable(this, &nvhost_gpu::SubmitGPFIFOBase1, input, output, fd, false);
        }
        case 0x9:
            return WrapFixed(this, &nvhost_gpu::AllocateObjectContext, input, output, fd);
        case 0xb:
            return WrapFixed(this, &nvhost_gpu::ZCullBind, input, output);
        case 0xc:
            return WrapFixed(this, &nvhost_gpu::SetErrorNotifier, input, output);
        case 0xd:
            return WrapFixed(this, &nvhost_gpu::SetChannelPriority, input, output);
        case 0x18:
            return WrapFixed(this, &nvhost_gpu::AllocGPFIFOEx, input, output, fd);
        case 0x1a:
            return WrapFixed(this, &nvhost_gpu::AllocGPFIFOEx2, input, output, fd);
        case 0x1b: {
#ifdef PS5_NATIVE
            unsigned ticket{};
            if (NextDetailedGpfifoTrace(g_gpfifo_route_trace, ticket))
                std::fprintf(stderr,
                             "EDEN_NS2_GPFIFO_ROUTE ticket=%u route=ioctl1-kickoff input=%zu output=%zu\n",
                             ticket, input.size(), output.size());
#endif
            return WrapFixedVariable(this, &nvhost_gpu::SubmitGPFIFOBase1, input, output, fd, true);
        }
        case 0x1d:
            return WrapFixed(this, &nvhost_gpu::ChannelSetTimeslice, input, output);
        case 0x1e: { // Switch 2 NVGPU_IOCTL_CHANNEL_GET_USER_SYNCPOINT
            if (command.raw != 0xC010481EU) break;
            if (output.size() != sizeof(IoctlGetUserSyncpoint) ||
                (!input.empty() && input.size() != sizeof(IoctlGetUserSyncpoint)))
                return NvResult::InvalidSize;
            return WrapFixed(this, &nvhost_gpu::GetUserSyncpoint, input, output, fd);
        }
        case 0x1f: { // Switch 2 NVGPU_IOCTL_CHANNEL_SET_PREEMPTION_MODE
            // The guest provides two little-endian u32 preemption modes.
            // Encore's serialized GPU worker supports Wait-For-Idle only;
            // pretending to support mid-draw or mid-compute preemption
            // would make a later fence/scheduler guarantee untrue.
            if (command.raw != 0xC008481FU) break;
            if (input.size() != 8 || output.size() != 8)
                return NvResult::InvalidSize;
            const std::scoped_lock lock{channel_mutex};
            if (!UsesNativeAbi(fd)) return NvResult::NotImplemented;
            u64 program_id{};
            if (const auto status = ValidateChannelOwner(fd, program_id);
                status != NvResult::Success)
                return status;
            std::array<u32_le, 2> modes{};
            std::memcpy(modes.data(), input.data(), sizeof(modes));
            // NVGPU encodes the WFI choice as BIT 0 (value 1), not
            // the zero-valued NV2080 control enum. Zero means no explicit
            // selection: keep this channel's existing WFI behaviour.
            // CTA/GFXP/CILP cannot be acknowledged without a real
            // mid-job preemption model and guest-visible fence semantics.
            constexpr u32 NativeWfiBit = 1u;
            if ((static_cast<u32>(modes[0]) & ~NativeWfiBit) != 0 ||
                (static_cast<u32>(modes[1]) & ~NativeWfiBit) != 0)
                return NvResult::NotImplemented;
            std::memcpy(output.data(), modes.data(), sizeof(modes));
            return NvResult::Success;
        }
        case 0x20: // Switch 2 NVGPU_IOCTL_CHANNEL_OPEN
            if (command.raw != 0x40044820U) break;
            if (input.size() != sizeof(IoctlChannelOpen)) return NvResult::InvalidSize;
            return WrapFixed(this, &nvhost_gpu::ChannelOpen, input, output, fd);
        default:
            break;
        }
        break;
    case 'G':
        switch (command.cmd) {
        case 0x14:
            return WrapFixed(this, &nvhost_gpu::SetClientData, input, output);
        case 0x15:
            return WrapFixed(this, &nvhost_gpu::GetClientData, input, output);
        default:
            break;
        }
        break;
    }
    UNIMPLEMENTED_MSG("Unimplemented ioctl={:08X}", command.raw);
    return NvResult::NotImplemented;
};

NvResult nvhost_gpu::Ioctl2(DeviceFD fd, Ioctl command, std::span<const u8> input,
                            std::span<const u8> inline_input, std::span<u8> output) {
    switch (command.group) {
    case 'H':
        switch (command.cmd) {
        case 0x1b: {
#ifdef PS5_NATIVE
            unsigned ticket{};
            if (NextDetailedGpfifoTrace(g_gpfifo_route_trace, ticket))
                std::fprintf(stderr,
                             "EDEN_NS2_GPFIFO_ROUTE ticket=%u route=ioctl2-kickoff input=%zu inline=%zu output=%zu\n",
                             ticket, input.size(), inline_input.size(), output.size());
#endif
            return WrapFixedInlIn(this, &nvhost_gpu::SubmitGPFIFOBase2, input, inline_input,
                                  output, fd);
        }
        }
        break;
    }
    UNIMPLEMENTED_MSG("Unimplemented ioctl={:08X}", command.raw);
    return NvResult::NotImplemented;
}

NvResult nvhost_gpu::Ioctl3(DeviceFD fd, Ioctl command, std::span<const u8> input,
                            std::span<u8> output, std::span<u8> inline_output) {
    UNIMPLEMENTED_MSG("Unimplemented ioctl={:08X}", command.raw);
    return NvResult::NotImplemented;
}

void nvhost_gpu::OnOpen(NvCore::SessionId session_id, DeviceFD fd) {
    sessions[fd] = session_id;
}

void nvhost_gpu::OnClose(DeviceFD fd) {
    sessions.erase(fd);
}

bool nvhost_gpu::CanBindNativeTsgChannel(DeviceFD fd, NvCore::SessionId owner) {
    const std::scoped_lock lk{channel_mutex};
    const auto it = sessions.find(fd);
    if (it == sessions.end()) return false;
    const auto* channel = core.GetSession(it->second);
    const auto* tsg = core.GetSession(owner);
    return channel && tsg && channel->process &&
           channel->process == tsg->process &&
           NativeGpuProcessRegistry::Instance().Contains(channel->process);
}

NvResult nvhost_gpu::ChannelOpen(IoctlChannelOpen& params, DeviceFD fd) {
    // This ioctl selects an engine runlist for the *existing*, live
    // scheduler channel. The two advertised runlists are graphics (0)
    // and asynchronous copy (1). Creating the GPFIFO ring and USERD
    // still belongs to CHANNEL_SETUP_BIND, never to this method.
    const std::scoped_lock lock{channel_mutex};
    if (!UsesNativeAbi(fd)) return NvResult::NotImplemented;
    u64 program_id{};
    if (const auto status = ValidateChannelOwner(fd, program_id);
        status != NvResult::Success)
        return status;
    const s32 runlist_id = static_cast<s32>(params.runlist_id);
    if (runlist_id != 0 && runlist_id != 1) return NvResult::BadParameter;
    if (!channel_state) return NvResult::InvalidState;
    if (native_runlist_id)
        return *native_runlist_id == runlist_id
            ? NvResult::Success : NvResult::AlreadyAllocated;
    if (channel_state->initialized) return NvResult::InvalidState;
    // No fake host GPU runlist, memory mapping, syncpoint or fence:
    // the existing virtual GPU queue already owns this ChannelState.
    channel_state->program_id = program_id;
    native_runlist_id = runlist_id;
    return NvResult::Success;
}

NvResult nvhost_gpu::GetUserSyncpoint(IoctlGetUserSyncpoint& params, DeviceFD fd) {
    const std::scoped_lock lock{channel_mutex};
    if (!UsesNativeAbi(fd)) return NvResult::NotImplemented;
    u64 program_id{};
    if (const auto status = ValidateChannelOwner(fd, program_id);
        status != NvResult::Success)
        return status;
    if (!channel_state || !channel_state->memory_manager ||
        channel_syncpoint == 0 ||
        channel_syncpoint >= Tegra::MemoryManager::NumShimSyncpoints ||
        !syncpoint_manager.IsSyncpointAllocated(channel_syncpoint))
        return NvResult::InvalidState;

    // The AS ioctl A:0xB must have already installed a real, readable
    // syncpoint shim in the exact GMMU bound to this channel. Never return
    // an invented GPU VA: NVN subsequently dereferences this address.
    const GPUVAddr shim = channel_state->memory_manager->GetNativeSyncpointShim();
    if (!shim || shim > std::numeric_limits<GPUVAddr>::max() -
                             u64{channel_syncpoint} * sizeof(u32))
        return NvResult::InvalidState;
    // NVIDIA's nvgpu_channel_get_user_syncpoint returns the CURRENT
    // Host1x syncpoint read (syncpt_read_ext_check), not the projected
    // counter_max of GetSyncpointFence(). That future fence value could
    // make an NVN user channel wait for a GPU job it has not submitted.
    // Match the very same guest-visible atomic word that GPU reads from
    // the shim address returned below.
    const u32 current = system.Host1x().GetSyncpointManager()
                            .GetGuestSyncpointValue(channel_syncpoint);
    params.gpu_va = shim + u64{channel_syncpoint} * sizeof(u32);
    params.syncpoint_id = channel_syncpoint;
    params.syncpoint_max = current;
    return NvResult::Success;
}

NvResult nvhost_gpu::SetNVMAPfd(IoctlSetNvmapFD& params) {
    LOG_DEBUG(Service_NVDRV, "called, fd={}", params.nvmap_fd);

    nvmap_fd = params.nvmap_fd;
    return NvResult::Success;
}

NvResult nvhost_gpu::SetClientData(IoctlClientData& params) {
    LOG_DEBUG(Service_NVDRV, "called");
    user_data = params.data;
    return NvResult::Success;
}

NvResult nvhost_gpu::GetClientData(IoctlClientData& params) {
    LOG_DEBUG(Service_NVDRV, "called");
    params.data = user_data;
    return NvResult::Success;
}

NvResult nvhost_gpu::ZCullBind(IoctlZCullBind& params) {
    zcull_params = params;
    LOG_DEBUG(Service_NVDRV, "called, gpu_va={:X}, mode={:X}", zcull_params.gpu_va,
              zcull_params.mode);
    return NvResult::Success;
}

NvResult nvhost_gpu::SetErrorNotifier(IoctlSetErrorNotifier& params) {
    LOG_WARNING(Service_NVDRV, "(STUBBED) called, offset={:X}, size={:X}, mem={:X}", params.offset,
                params.size, params.mem);
    return NvResult::Success;
}

NvResult nvhost_gpu::SetChannelPriority(IoctlChannelSetPriority& params) {
    channel_priority = params.priority;
    LOG_INFO(Service_NVDRV, "called, priority={:X}", channel_priority);

    switch (static_cast<ChannelPriority>(channel_priority)) {
    case ChannelPriority::Low: channel_timeslice = 1300; break;
    case ChannelPriority::Medium: channel_timeslice = 2600; break;
    case ChannelPriority::High: channel_timeslice = 5200; break;
    default : return NvResult::BadParameter;
    }

    return NvResult::Success;
}

NvResult nvhost_gpu::ValidateChannelOwner(DeviceFD fd, u64& program_id) const {
    // Never index the session map: a malformed FD creates a ghost entry, and a
    // null GetSession/process could dereference during GPFIFO allocation.
    const auto it = sessions.find(fd);
    if (it == sessions.end()) return NvResult::InvalidState;
    const auto* session = core.GetSession(it->second);
    if (!session || !session->process) return NvResult::InvalidState;
    const auto* process = session->process;

    // Unlike a legacy Maxwell channel, a native Ampere channel may not be
    // initialised by a foreign FD that shares the same NVDRV device object.
    // Match the real per-FD owner BEFORE InitChannel, program_id or ctxObjs
    // are changed, not only at the later SubmitGPFIFOImpl boundary.
    const bool native = NativeGpuProcessRegistry::Instance().Contains(process);
    for (const auto& opened_fd : sessions) {
        const auto* opened = core.GetSession(opened_fd.second);
        if (native) {
            if (!opened || opened->process != process) return NvResult::InvalidState;
        } else if (opened && opened->process &&
                   NativeGpuProcessRegistry::Instance().Contains(opened->process)) {
            // A legacy FD cannot initialise a device with any native owner.
            // Mixed legacy/legacy FDs keep their original compatibility.
            return NvResult::InvalidState;
        }
    }
    program_id = process->GetProgramId();
    return NvResult::Success;
}

NvResult nvhost_gpu::AllocGPFIFOEx(IoctlAllocGpfifoEx& params, DeviceFD fd) {
    LOG_DEBUG(Service_NVDRV, "called, num_entries={:X}, flags={:X}, reserved1={:X}, "
              "reserved2={:X}, reserved3={:X}",
              params.num_entries, params.flags, params.reserved[0], params.reserved[1],
              params.reserved[2]);

    std::scoped_lock lk(channel_mutex);
    u64 program_id{};
    if (const auto status = ValidateChannelOwner(fd, program_id);
        status != NvResult::Success)
        return status;
    if (channel_state->initialized) {
        LOG_DEBUG(Service_NVDRV, "Channel already initialized; AllocGPFIFOEx returning AlreadyAllocated");
        return NvResult::AlreadyAllocated;
    }

    // Store program id for later lazy initialization
    channel_state->program_id = program_id;

    // If address space is not yet bound, defer channel initialization.
    if (!channel_state->memory_manager) {
        params.fence_out = syncpoint_manager.GetSyncpointFence(channel_syncpoint);
        return NvResult::Success;
    }

    system.GPU().InitChannel(*channel_state, program_id);

    params.fence_out = syncpoint_manager.GetSyncpointFence(channel_syncpoint);

    return NvResult::Success;
}

NvResult nvhost_gpu::AllocGPFIFOEx2(IoctlAllocGpfifoEx& params, DeviceFD fd) {
    LOG_DEBUG(Service_NVDRV,
              "called, num_entries={:X}, flags={:X}, reserved1={:X}, "
              "reserved2={:X}, reserved3={:X}",
              params.num_entries, params.flags, params.reserved[0], params.reserved[1],
              params.reserved[2]);

    std::scoped_lock lk(channel_mutex);
    u64 program_id{};
    if (const auto status = ValidateChannelOwner(fd, program_id);
        status != NvResult::Success)
        return status;
    if (channel_state->initialized) {
        LOG_DEBUG(Service_NVDRV, "Channel already initialized; AllocGPFIFOEx2 returning AlreadyAllocated");
        return NvResult::AlreadyAllocated;
    }

    // Store program id for later lazy initialization
    channel_state->program_id = program_id;

    // If address space is not yet bound, defer channel initialization.
    if (!channel_state->memory_manager) {
        params.fence_out = syncpoint_manager.GetSyncpointFence(channel_syncpoint);
        return NvResult::Success;
    }

    system.GPU().InitChannel(*channel_state, program_id);

    params.fence_out = syncpoint_manager.GetSyncpointFence(channel_syncpoint);

    return NvResult::Success;
}

s32_le nvhost_gpu::GetObjectContextClassNumberIndex(CtxClasses class_number) {
    constexpr s32_le invalid_class_number_index = -1;
    switch (class_number) {
    case CtxClasses::Ctx2D: return 0;
    case CtxClasses::Ctx3D: return 1;
    case CtxClasses::CtxCompute: return 2;
    case CtxClasses::CtxKepler: return 3;
    case CtxClasses::CtxDMA: return 4;
    case CtxClasses::CtxChannelGPFIFO: return 5;
    default: return invalid_class_number_index;
    }
}

NvResult nvhost_gpu::AllocateObjectContext(IoctlAllocObjCtx& params, DeviceFD fd) {
    LOG_DEBUG(Service_NVDRV, "called, class_num={:#x}, flags={:#x}, obj_id={:#x}", params.class_num,
              params.flags, params.obj_id);

    // Do not require channel initialization here: some clients allocate contexts before binding.
    if (!channel_state) {
        LOG_ERROR(Service_NVDRV, "No channel state available!");
        return NvResult::InvalidState;
    }

    std::scoped_lock lk(channel_mutex);
    u64 verified_program_id{};
    if (const auto status = ValidateChannelOwner(fd, verified_program_id);
        status != NvResult::Success)
        return status;

    if (params.flags) {
        LOG_WARNING(Service_NVDRV, "non-zero flags={:#x} for class={:#x}", params.flags,
                    params.class_num);

        constexpr u32 allowed_mask{};
        params.flags = allowed_mask;
    }

    s32_le ctx_class_number_index = 
        GetObjectContextClassNumberIndex(static_cast<CtxClasses>(params.class_num));
    if (UsesNativeAbi(fd)) {
        if (const auto slot = GetNativeGpuContextSlot(params.class_num))
            ctx_class_number_index = static_cast<s32_le>(*slot);
    }
    if (ctx_class_number_index < 0) {
        LOG_ERROR(Service_NVDRV, "Invalid class number for object context: {:#x}",
                  params.class_num);
        return NvResult::BadParameter;
    }

    if (ctxObjs[ctx_class_number_index].has_value()) {
        LOG_WARNING(Service_NVDRV, "Object context for class {:#x} already allocated on this channel",
                    params.class_num);
        return NvResult::AlreadyAllocated;
    }

    // Defer actual hardware context binding until channel is initialized.
    ctxObjs[ctx_class_number_index] = params;

    return NvResult::Success;
}

static boost::container::small_vector<Tegra::CommandHeader, 512> BuildWaitCommandList(
    NvFence fence) {
    return {
        Tegra::BuildCommandHeader(Tegra::BufferMethods::SyncpointPayload, 1,
                                  Tegra::SubmissionMode::Increasing),
        {fence.value},
        Tegra::BuildCommandHeader(Tegra::BufferMethods::SyncpointOperation, 1,
                                  Tegra::SubmissionMode::Increasing),
        BuildFenceAction(Tegra::Engines::Puller::FenceOperation::Acquire, fence.id),
    };
}

static boost::container::small_vector<Tegra::CommandHeader, 512> BuildIncrementCommandList(
    NvFence fence, u32 count = 2) {
    boost::container::small_vector<Tegra::CommandHeader, 512> result{
        Tegra::BuildCommandHeader(Tegra::BufferMethods::SyncpointPayload, 1,
                                  Tegra::SubmissionMode::Increasing),
        {}};

    for (u32 index = 0; index < count; ++index) {
        result.push_back(Tegra::BuildCommandHeader(Tegra::BufferMethods::SyncpointOperation, 1,
                                                   Tegra::SubmissionMode::Increasing));
        result.push_back(
            BuildFenceAction(Tegra::Engines::Puller::FenceOperation::Increment, fence.id));
    }

    return result;
}

static boost::container::small_vector<Tegra::CommandHeader, 512> BuildIncrementWithWfiCommandList(
    NvFence fence, u32 count = 2) {
    boost::container::small_vector<Tegra::CommandHeader, 512> result{
        Tegra::BuildCommandHeader(Tegra::BufferMethods::WaitForIdle, 1,
                                  Tegra::SubmissionMode::Increasing),
        {}};
    auto increment_list{BuildIncrementCommandList(fence, count)};
    result.insert(result.end(), increment_list.begin(), increment_list.end());
    return result;
}

NvResult nvhost_gpu::SubmitGPFIFOImpl(IoctlSubmitGpfifo& params, Tegra::CommandList&& entries, DeviceFD fd) {
    LOG_TRACE(Service_NVDRV, "called, gpfifo={:X}, num_entries={:X}, flags={:X}", params.address,
              params.num_entries, params.flags.raw);

    auto& gpu = system.GPU();

    std::scoped_lock lock(channel_mutex);

    // Reject malformed native submits BEFORE InitChannel, GPU channel
    // registry mutations or syncpoint reservations. Invalid guest flags
    // used to initialise a channel and bind an owner despite returning
    // BadParameter later. Maxwell/NS1 retains its original admission.
    const bool native = UsesNativeAbi(fd);
    auto& flags = params.flags;
#ifdef PS5_NATIVE
    unsigned impl_trace_ticket{};
    const bool trace_impl = NextDetailedGpfifoTrace(g_gpfifo_impl_trace, impl_trace_ticket);
    if (trace_impl)
        std::fprintf(stderr,
                     "EDEN_NS2_GPFIFO_IMPL ticket=%u stage=enter native=%u initialized=%u gmmu=%u entries=%u flags=%08x fence_id=%u fence_value=%u\n",
                     impl_trace_ticket, static_cast<unsigned>(native),
                     static_cast<unsigned>(channel_state->initialized),
                     static_cast<unsigned>(channel_state->memory_manager != nullptr),
                     static_cast<unsigned>(params.num_entries),
                     static_cast<unsigned>(flags.raw),
                     static_cast<unsigned>(params.fence.id),
                     static_cast<unsigned>(params.fence.value));
#endif
    const auto native_plan = native
        ? PlanNativeSubmission(static_cast<u32>(flags.raw),
                               static_cast<u32>(params.fence.value), true)
        : std::optional<NativeSubmissionPlan>{};
    if (native && !native_plan) {
#ifdef PS5_NATIVE
        if (trace_impl)
            std::fprintf(stderr,
                         "EDEN_NS2_GPFIFO_REJECT ticket=%u stage=impl reason=native-plan flags=%08x fence_value=%u\n",
                         impl_trace_ticket, static_cast<unsigned>(flags.raw),
                         static_cast<unsigned>(params.fence.value));
#endif
        return NvResult::BadParameter;
    }

    // A native GPU device may expose multiple FDs. A rejected or foreign
    // process must not cause InitChannel on another guest's address space.
    const Kernel::KProcess* native_owner = nullptr;
    if (native) {
        const auto it = sessions.find(fd);
        const auto* owner = it == sessions.end() ? nullptr : core.GetSession(it->second);
        if (!owner || !owner->process) {
#ifdef PS5_NATIVE
            if (trace_impl)
                std::fprintf(stderr,
                             "EDEN_NS2_GPFIFO_REJECT ticket=%u stage=impl reason=owner-session\n",
                             impl_trace_ticket);
#endif
            return NvResult::InvalidState;
        }
        native_owner = owner->process;
        for (const auto& [other_fd, session_id] : sessions) {
            const auto* opened = core.GetSession(session_id);
            if (!opened || opened->process != native_owner) {
#ifdef PS5_NATIVE
                if (trace_impl)
                    std::fprintf(stderr,
                                 "EDEN_NS2_GPFIFO_REJECT ticket=%u stage=impl reason=mixed-owner\n",
                                 impl_trace_ticket);
#endif
                return NvResult::InvalidState;
            }
        }
    }

    // A guest may request FENCE_WAIT with an arbitrary negative, out-of-
    // range or released syncpoint ID. HasSyncpointExpired uses at() and
    // asserts on unreserved IDs; never let an untrusted NVDRV ioctl reach
    // that host assertion before GPU channel admission is complete.
    // Keep the Maxwell/NS1 fence path untouched.
    if (native && flags.fence_wait.Value() &&
        !syncpoint_manager.IsSyncpointAllocated(
            static_cast<u32>(params.fence.id)))
        return NvResult::BadParameter;

    // Native FENCE_ACTION ACQUIRE is not yet scheduled by the Ampere
    // puller. If this input fence is unresolved, submitting its legacy
    // wait packet would kill the GPU worker after an apparently successful
    // ioctl, and possibly strand an already-reserved completion syncpoint.
    // Reject BEFORE InitChannel, bind or fence reservation. A satisfied
    // wait needs no command; NS1 retains its existing wait path.
    if (native && flags.fence_wait.Value() &&
        !syncpoint_manager.IsFenceSignalled(params.fence)) {
#ifdef PS5_NATIVE
        if (trace_impl)
            std::fprintf(stderr,
                         "EDEN_NS2_GPFIFO_REJECT ticket=%u stage=impl reason=unresolved-native-wait fence_id=%u fence_value=%u\n",
                         impl_trace_ticket, static_cast<unsigned>(params.fence.id),
                         static_cast<unsigned>(params.fence.value));
#endif
        return NvResult::NotImplemented;
    }

    // P2.7: every native GPFIFO entry must refer to a fully mapped GMMU
    // command span BEFORE InitChannel, owner binding or fence reservation.
    // A valid guest ioctl pointer only proves that the *list of headers*
    // was readable, not the GPU virtual addresses inside its entries.
    // Otherwise an unmapped command buffer can be read back as zero words
    // and falsely acknowledged as work submitted. Maxwell retains its
    // original command admission; the native mapper is synchronous here.
    if (native && channel_state->memory_manager) {
        for (const auto& command : entries.command_lists) {
            if (command.size == 0) continue;
            const u64 address = command.addr;
            const u64 bytes = static_cast<u64>(command.size) * sizeof(Tegra::CommandHeader);
            if (!channel_state->memory_manager->IsFullyMappedRange(address, bytes)) {
                LOG_ERROR(Service_NVDRV,
                          "Native GPFIFO rejected unmapped command span address={:#x} bytes={:#x}",
                          address, bytes);
#ifdef PS5_NATIVE
                if (trace_impl)
                    std::fprintf(stderr,
                                 "EDEN_NS2_GPFIFO_REJECT ticket=%u stage=impl reason=unmapped-command address=%016llx bytes=%llu\n",
                                 impl_trace_ticket, static_cast<unsigned long long>(address),
                                 static_cast<unsigned long long>(bytes));
#endif
                return NvResult::InvalidSize;
            }
        }
    }

    // Lazily initialize channel when address space is available
    if (!channel_state->initialized && channel_state->memory_manager) {
        system.GPU().InitChannel(*channel_state, channel_state->program_id);
    }

    // A real native GPU channel requires a bound GMMU and initialized
    // command scheduler. Never acknowledge 2nd-gen submissions before those
    // resources exist, and never change the existing Switch 1 path.
#ifdef PS5_NATIVE
    // Capture the actual NVDRV -> GPFIFO boundary, including cases where a
    // guest submission is incorrectly treated as legacy. Bounded: quiet in
    // the rest of gameplay, and never prints guest memory contents.
    static std::atomic<unsigned> submissions{0};
    const auto n = submissions.fetch_add(1, std::memory_order_relaxed) + 1;
    if (n <= 24 || (n & (n - 1)) == 0)
        std::fprintf(stderr,
                     "EDEN_NS2_GPFIFO submit=%u native=%u initialized=%u gmmu=%u entries=%u flags=%08x\n",
                     n, static_cast<unsigned>(native),
                     static_cast<unsigned>(channel_state->initialized),
                     static_cast<unsigned>(channel_state->memory_manager != nullptr),
                     static_cast<unsigned>(params.num_entries),
                     static_cast<unsigned>(params.flags.raw));
#endif
    if (native && (!channel_state->initialized || !channel_state->memory_manager)) {
#ifdef PS5_NATIVE
        if (trace_impl)
            std::fprintf(stderr,
                         "EDEN_NS2_GPFIFO_REJECT ticket=%u stage=impl reason=channel-not-ready initialized=%u gmmu=%u\n",
                         impl_trace_ticket, static_cast<unsigned>(channel_state->initialized),
                         static_cast<unsigned>(channel_state->memory_manager != nullptr));
#endif
        return NvResult::InvalidState;
    }
    // Bind only a validated process, after the channel has a real GMMU
    // and the complete native fence plan has already passed admission.
    // Only an authenticated Switch 2 channel may switch its address-space
    // texture layout. NS1 never enters this branch and keeps Maxwell tiling.
    if (native) {
        if (!NativeGpuChannelRegistry::Instance().Bind(
                channel_state.get(), native_owner)) {
#ifdef PS5_NATIVE
            if (trace_impl)
                std::fprintf(stderr,
                             "EDEN_NS2_GPFIFO_REJECT ticket=%u stage=impl reason=channel-bind\n",
                             impl_trace_ticket);
#endif
            return NvResult::InvalidState;
        }
        channel_state->memory_manager->EnableNativeTiling();
    }

    const auto bind_id = channel_state->bind_id;
    const u32 native_completion = native_plan ? native_plan->completion_count : 0;

    if (flags.fence_wait.Value()) {
        if (flags.increment_value.Value()) {
            return NvResult::BadParameter;
        }

        if (!syncpoint_manager.IsFenceSignalled(params.fence)) {
            gpu.PushGPUEntries(bind_id, Tegra::CommandList{BuildWaitCommandList(params.fence)});
        }
    }

    params.fence.id = channel_syncpoint;

    // Use the SAME reviewed native envelope that determined how many
    // completion commands the GPU queue will issue. Never reserve a fence
    // using a second, independently computed native increment count.
    const u32 increment = native
        ? native_plan->increment_total
        : (flags.fence_increment.Value() != 0 ? 2U : 0U) +
          (flags.increment_value.Value() != 0 ? u32{params.fence.value} : 0U);
    params.fence.value = syncpoint_manager.IncrementSyncpointMaxExt(channel_syncpoint, increment);
    gpu.PushGPUEntries(bind_id, std::move(entries));

    if (flags.fence_increment.Value()) {
        if (flags.suppress_wfi.Value()) {
            gpu.PushGPUEntries(bind_id,
                               Tegra::CommandList{BuildIncrementCommandList(params.fence)});
        } else {
            gpu.PushGPUEntries(bind_id,
                               Tegra::CommandList{BuildIncrementWithWfiCommandList(params.fence)});
        }
    }

    // The completed pushbuffer precedes its counter increment on the same
    // ordered GPU queue. Do not signal before work has been submitted.
    if (native_completion != 0) {
        gpu.PushGPUEntries(bind_id, Tegra::CommandList{
            flags.suppress_wfi.Value()
                ? BuildIncrementCommandList(params.fence, native_completion)
                : BuildIncrementWithWfiCommandList(params.fence, native_completion)});
    }

    flags.raw = 0;

    return NvResult::Success;
}

Core::Memory::Memory& nvhost_gpu::GetSessionMemory(DeviceFD fd) {
    if (const auto it = sessions.find(fd); it != sessions.end())
        if (auto* const session = core.GetSession(it->second);
            session != nullptr && session->process != nullptr)
            return session->process->GetMemory();

    LOG_ERROR(Service_NVDRV, "No session for fd={}, falling back to application memory", fd);
    return system.ApplicationMemory();
}

NvResult nvhost_gpu::SubmitGPFIFOBase1(IoctlSubmitGpfifo& params,
                                       std::span<Tegra::CommandListHeader> commands, DeviceFD fd,
                                       bool kickoff) {
#ifdef PS5_NATIVE
    unsigned base_trace_ticket{};
    const bool trace_base = NextDetailedGpfifoTrace(g_gpfifo_base_trace, base_trace_ticket);
    if (trace_base)
        std::fprintf(stderr,
                     "EDEN_NS2_GPFIFO_BASE ticket=%u route=%s entries=%u span=%zu address=%016llx flags=%08x\n",
                     base_trace_ticket, kickoff ? "base1-kickoff" : "base1-submit",
                     static_cast<unsigned>(params.num_entries), commands.size(),
                     static_cast<unsigned long long>(params.address),
                     static_cast<unsigned>(params.flags.raw));
#endif
    // Kickoff H:0x1b passes only the fixed ioctl header. Its GPFIFO list
    // lives in guest memory at params.address, NOT in the inline variable
    // span (which may legitimately be empty). Comparing its count against
    // commands.size() silently rejected the native GPU's first submission.
    // Bound pointer-backed lists independently before allocation/guest read.
    constexpr u32 MaxKickoffEntries = 1U << 16; // 512 KiB of 8-byte headers
    if (kickoff) {
        if (params.num_entries > MaxKickoffEntries ||
            (params.num_entries != 0 && params.address == 0)) {
#ifdef PS5_NATIVE
            if (trace_base)
                std::fprintf(stderr,
                             "EDEN_NS2_GPFIFO_REJECT ticket=%u stage=base1 reason=kickoff-shape entries=%u address=%016llx\n",
                             base_trace_ticket, static_cast<unsigned>(params.num_entries),
                             static_cast<unsigned long long>(params.address));
#endif
            return NvResult::InvalidSize;
        }
        // A pointer-backed command list must be read from the exact owner
        // process for THIS FD. GetSessionMemory's legacy fallback to the
        // application process would otherwise read a different title's RAM.
        const auto it = sessions.find(fd);
        const auto* session = it == sessions.end() ? nullptr : core.GetSession(it->second);
        if (!session || !session->process) {
#ifdef PS5_NATIVE
            if (trace_base)
                std::fprintf(stderr,
                             "EDEN_NS2_GPFIFO_REJECT ticket=%u stage=base1 reason=session\n",
                             base_trace_ticket);
#endif
            return NvResult::InvalidState;
        }
        // A native FIFO pointer is guest-controlled. A partial/unmapped
        // ReadBlock can return zero-filled commands yet still acknowledge
        // the GPU fence as if real work had been submitted. Require that
        // every header belongs to this FD's actual guest address space.
        if (UsesNativeAbi(fd) &&
            !NativeGpfifoGuestHeadersReadable(
                params.address, params.num_entries,
                sizeof(Tegra::CommandListHeader),
                [&](std::uint64_t address, std::size_t bytes) {
                    return session->process->GetMemory().IsValidVirtualAddressRange(
                        address, bytes);
                })) {
#ifdef PS5_NATIVE
            if (trace_base)
                std::fprintf(stderr,
                             "EDEN_NS2_GPFIFO_REJECT ticket=%u stage=base1 reason=guest-header-range entries=%u address=%016llx\n",
                             base_trace_ticket, static_cast<unsigned>(params.num_entries),
                             static_cast<unsigned long long>(params.address));
#endif
            return NvResult::InvalidSize;
        }
    } else if (params.num_entries > commands.size()) {
#ifdef PS5_NATIVE
        if (trace_base)
            std::fprintf(stderr,
                         "EDEN_NS2_GPFIFO_REJECT ticket=%u stage=base1 reason=inline-span entries=%u span=%zu\n",
                         base_trace_ticket, static_cast<unsigned>(params.num_entries),
                         commands.size());
#endif
        UNIMPLEMENTED();
        return NvResult::InvalidSize;
    }

    Tegra::CommandList entries;
    try {
        // Size the guest command list before any fence is reserved; a host
        // OOM must return NVDRV InsufficientMemory, not unwind through IPC.
        entries.command_lists.resize(params.num_entries);
    } catch (const std::bad_alloc&) {
        return NvResult::InsufficientMemory;
    }
    // A zero-entry submission can still carry a syncpoint fence. Never
    // hand a null vector pointer to guest ReadBlock or libc memcpy(0).
    if (params.num_entries != 0) {
        if (kickoff) {
            this->GetSessionMemory(fd).ReadBlock(params.address, entries.command_lists.data(),
                                                 params.num_entries * sizeof(Tegra::CommandListHeader));
        } else {
            std::memcpy(entries.command_lists.data(), commands.data(),
                        params.num_entries * sizeof(Tegra::CommandListHeader));
        }
    }

    return SubmitGPFIFOImpl(params, std::move(entries), fd);
}

NvResult nvhost_gpu::SubmitGPFIFOBase2(IoctlSubmitGpfifo& params,
                                       std::span<const Tegra::CommandListHeader> commands, DeviceFD fd) {
#ifdef PS5_NATIVE
    unsigned base_trace_ticket{};
    const bool trace_base = NextDetailedGpfifoTrace(g_gpfifo_base_trace, base_trace_ticket);
    if (trace_base)
        std::fprintf(stderr,
                     "EDEN_NS2_GPFIFO_BASE ticket=%u route=base2-kickoff entries=%u span=%zu flags=%08x\n",
                     base_trace_ticket, static_cast<unsigned>(params.num_entries),
                     commands.size(), static_cast<unsigned>(params.flags.raw));
#endif
    if (params.num_entries > commands.size()) {
#ifdef PS5_NATIVE
        if (trace_base)
            std::fprintf(stderr,
                         "EDEN_NS2_GPFIFO_REJECT ticket=%u stage=base2 reason=inline-span entries=%u span=%zu\n",
                         base_trace_ticket, static_cast<unsigned>(params.num_entries),
                         commands.size());
#endif
        UNIMPLEMENTED();
        return NvResult::InvalidSize;
    }

    Tegra::CommandList entries;
    try {
        // Size the guest command list before any fence is reserved; a host
        // OOM must return NVDRV InsufficientMemory, not unwind through IPC.
        entries.command_lists.resize(params.num_entries);
    } catch (const std::bad_alloc&) {
        return NvResult::InsufficientMemory;
    }
    // Ioctl2 H:0x1b may carry a fence-only zero-entry submit, just like
    // Ioctl H:0x1b/H:0x8. A zero-length memcpy on null vector/span data
    // pointers is not guaranteed valid by the C/C++ memory contract.
    if (params.num_entries != 0)
        std::memcpy(entries.command_lists.data(), commands.data(),
                    params.num_entries * sizeof(Tegra::CommandListHeader));
    return SubmitGPFIFOImpl(params, std::move(entries), fd);
}

NvResult nvhost_gpu::GetWaitbase(IoctlGetWaitbase& params) {
    LOG_INFO(Service_NVDRV, "called, unknown={:#x}", params.unknown);

    params.value = 0; // Seems to be hard coded at 0
    return NvResult::Success;
}

NvResult nvhost_gpu::ChannelSetTimeout(IoctlChannelSetTimeout& params) {
    LOG_INFO(Service_NVDRV, "called, timeout={:#x}", params.timeout);

    return NvResult::Success;
}

NvResult nvhost_gpu::ChannelSetTimeslice(IoctlSetTimeslice& params) {
    LOG_INFO(Service_NVDRV, "called, timeslice={:#x}", params.timeslice);

    if (params.timeslice < 1000 || params.timeslice > 5000) {
        return NvResult::BadParameter;
    }

    channel_timeslice = params.timeslice;

    return NvResult::Success;
}

Kernel::KEvent* nvhost_gpu::QueryEvent(u32 event_id) {
    switch (event_id) {
    case 1:
        return sm_exception_breakpoint_int_report_event;
    case 2:
        return sm_exception_breakpoint_pause_report_event;
    case 3:
        return error_notifier_event;
    case 7:
        // Native driver error notification; original console clients use 3.
        // QueryEvent has no FD parameter. Do not expose the native-only event
        // through a mixed/shared NVDRV device; fail closed if ambiguous.
        return sessions.size() == 1 && UsesNativeAbi(sessions.begin()->first)
                   ? error_notifier_event : nullptr;
    default:
        LOG_CRITICAL(Service_NVDRV, "Unknown Ctrl GPU Event {}", event_id);
        return nullptr;
    }
}

bool nvhost_gpu::UsesNativeAbi(DeviceFD fd) const {
    // A device can hold FDs for different guest processes. Never promote a
    // Switch 1 request because an unrelated FD owns a Switch 2 GPU session.
    const auto it = sessions.find(fd);
    if (it == sessions.end()) return false;
    const auto* session = core.GetSession(it->second);
    return session && session->process &&
           NativeGpuProcessRegistry::Instance().Contains(session->process);
}
} // namespace Service::Nvidia::Devices
