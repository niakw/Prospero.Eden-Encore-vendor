// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

// SPDX-FileCopyrightText: 2022 yuzu Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#ifdef PS5_NATIVE
#include "gpu_failure.h"
#endif
#include "common/assert.h"
#include "common/scope_exit.h"
#include "common/logging.h"
#include "common/settings.h"
#include "core/core.h"
#include "video_core/control/channel_state.h"
#include "video_core/dma_pusher.h"
#include "video_core/engines/fermi_2d.h"
#include "video_core/engines/kepler_compute.h"
#include "video_core/engines/kepler_memory.h"
#include "video_core/engines/maxwell_3d.h"
#include "video_core/engines/maxwell_dma.h"
#include "video_core/engines/puller.h"
#include <span>
#include <memory>
#include <new>
#include <atomic>
#include <algorithm>
#include <limits>
#include <cstdio>
#include <stdexcept>
#include "native_ampere_semaphore.h"
#include "core/hle/service/nvdrv/devices/native_gpu_channel_registry.h"
#include "video_core/gpu.h"
#include "video_core/memory_manager.h"
#include "video_core/rasterizer_interface.h"
#include "video_core/native_qmd.h"
#ifdef PS5_NATIVE
#include "video_core/native_shader_capture.h"
#endif

namespace Service::Nvidia::Devices {
std::shared_ptr<Tegra::NativeQmdSession>
NativeGpuChannelRegistry::AcquireNativeQmdSession(
    const Tegra::Control::ChannelState* channel) {
    if (!channel) return {};
    const std::scoped_lock lock{mutex};
    const auto owner = channels.find(channel);
    if (owner == channels.end()) return {};
    const auto ticket = owner->second;
    if (!NativeGpuProcessRegistry::Instance().IsCurrent(
            {ticket.process, ticket.generation})) return {};
    const auto existing = qmd_sessions.find(ticket.process);
    if (existing != qmd_sessions.end() &&
        existing->second.generation == ticket.generation)
        return existing->second.session;
    if (existing == qmd_sessions.end() && qmd_sessions.size() >= 256)
        return {};
    // Native QMD sessions are created by authenticated but guest-driven
    // GPU opens. Handle both the session allocation and the map-node
    // allocation: an OOM must not kill the GPU worker or publish an
    // incomplete process epoch. A later valid request can retry.
    try {
        auto session = std::make_shared<Tegra::NativeQmdSession>();
        qmd_sessions[ticket.process] = {ticket.generation, session};
        return session;
    } catch (const std::bad_alloc&) {
        return {};
    }
}
} // namespace Service::Nvidia::Devices

namespace Tegra::Engines {

namespace {

constexpr u32 Gf100BindClassMask = 0xffff;
constexpr u32 Gf100BindValidMask =  0x1f0000 | Gf100BindClassMask;

} // Anonymous namespace

void Puller::ProcessBindMethod(DmaPusher& dma_pusher, const MethodCall& method_call) {
    LOG_DEBUG(HW_GPU, "Binding subchannel {} to engine {:#x}", method_call.subchannel, method_call.argument);
    u32 engine = method_call.argument;
    if ((engine & ~Gf100BindClassMask) != 0 && (engine & ~Gf100BindValidMask) == 0) {
        engine &= Gf100BindClassMask;
    }

    const auto engine_id = static_cast<EngineID>(engine);
#ifdef PS5_NATIVE
    // Include Ampere compute even while its native QMD3 execution is not
    // implemented: distinguish an unbound compute queue from a VI2 wait.
    if (engine == 0xc797 || engine == 0xc7c0 || engine == 0xc7b5) {
        static std::atomic<unsigned> native_binds{0};
        const auto n = native_binds.fetch_add(1, std::memory_order_relaxed) + 1;
        if (n <= 24 || (n & (n - 1)) == 0)
            std::fprintf(stderr,
                         "EDEN_NS2_GPU_BIND count=%u cls=%04x sub=%u native=%u\n",
                         n, static_cast<unsigned>(engine),
                         static_cast<unsigned>(method_call.subchannel),
                         static_cast<unsigned>(Service::Nvidia::Devices::
                             NativeGpuChannelRegistry::Instance().IsNative(
                                 &dma_pusher.channel_state)));
    }
#endif
    const auto retire_native_3d = [&] {
        if (bound_engines[method_call.subchannel] == static_cast<EngineID>(0xc797)) {
            Service::Nvidia::Devices::NativeGpuChannelRegistry::Instance()
                .UnbindGraphicsSubchannel(
                    &dma_pusher.channel_state,
                    &dma_pusher.channel_state.payload->maxwell_3d,
                    method_call.subchannel);
        }
    };
    const auto retire_native_compute = [&] {
        if (bound_engines[method_call.subchannel] == static_cast<EngineID>(0xc7c0)) {
            Service::Nvidia::Devices::NativeGpuChannelRegistry::Instance()
                .UnbindComputeSubchannel(
                    &dma_pusher.channel_state,
                    &dma_pusher.channel_state.payload->kepler_compute,
                    method_call.subchannel);
        }
    };
    // Validate native class ownership before changing the effective subchannel
    // binding. A rejected Ampere bind must not poison subsequent dispatch.
    if (engine_id == static_cast<EngineID>(0xc797)) {
        if (!Service::Nvidia::Devices::NativeGpuChannelRegistry::Instance().IsNative(
                &dma_pusher.channel_state)) {
            UNIMPLEMENTED_MSG("Ampere 3D class on a non-native GPU channel");
            return;
        }
        // Rebinding a second 3D subchannel of the SAME GPU session must
        // retain its uploaded MME code and DRAM. An invalid execution state
        // cannot be laundered into a healthy one by rebinding the class.
        if (native_3d_failed || native_macro_method) {
            UNIMPLEMENTED_MSG("Native 3D macro channel requires a fresh session");
            return;
        }
        if (!native_3d) native_3d = std::make_unique<Tegra::NativeAmpereMacroState>();
        // Register the engine BEFORE exposing the binding to GPFIFO dispatch.
        // Only this authenticated 0xC797 path may opt in to native shader
        // capture; an ordinary Maxwell engine can never inherit its identity.
        if (!Service::Nvidia::Devices::NativeGpuChannelRegistry::Instance()
                 .BindGraphicsEngine(
                     &dma_pusher.channel_state,
                     &dma_pusher.channel_state.payload->maxwell_3d,
                     method_call.subchannel)) {
            UNIMPLEMENTED_MSG("Ampere 3D engine ownership unavailable");
            return;
        }
        retire_native_compute();
        bound_engines[method_call.subchannel] = engine_id;
        dma_pusher.BindSubchannel(&dma_pusher.channel_state.payload->maxwell_3d,
                                  method_call.subchannel, EngineTypes::Maxwell3D);
        return;
    }
    if (engine_id == static_cast<EngineID>(0xc7c0)) {
        // Bind the real native compute class to the existing compute
        // register owner, as on NX2 upstream, WITHOUT running the legacy
        // Maxwell QMD launch: Ampere PCAS at 0xAF has different semantics.
        auto& registry = Service::Nvidia::Devices::NativeGpuChannelRegistry::Instance();
        if (registry.IsNative(&dma_pusher.channel_state) &&
            registry.BindComputeEngine(
                &dma_pusher.channel_state,
                &dma_pusher.channel_state.payload->kepler_compute,
                method_call.subchannel)) {
            retire_native_3d();
            bound_engines[method_call.subchannel] = engine_id;
            dma_pusher.BindSubchannel(
                &dma_pusher.channel_state.payload->kepler_compute,
                method_call.subchannel, EngineTypes::KeplerCompute);
        } else {
            UNIMPLEMENTED_MSG("Ampere compute class on a non-native GPU channel");
        }
        return;
    }
    if (engine_id == static_cast<EngineID>(0xc7b5)) {
        // Upstream uses the same Maxwell DMA method layout for Ampere copy.
        // Restrict it to a live process-generation authenticated native channel.
        if (Service::Nvidia::Devices::NativeGpuChannelRegistry::Instance().IsNative(
                &dma_pusher.channel_state)) {
            retire_native_3d();
            retire_native_compute();
            bound_engines[method_call.subchannel] = engine_id;
            dma_pusher.BindSubchannel(&dma_pusher.channel_state.payload->maxwell_dma,
                                      method_call.subchannel, EngineTypes::MaxwellDMA);
        } else {
            UNIMPLEMENTED_MSG("Ampere DMA class on a non-native GPU channel");
        }
        return;
    }
    // Preserve the legacy Switch 1 binding behavior outside the guarded
    // native Ampere classes. Rebinding a subchannel must revoke an earlier
    // 0xC797 renderer identity, including if other NS2 subchannels remain.
    retire_native_3d();
    retire_native_compute();
    bound_engines[method_call.subchannel] = engine_id;
    switch (engine_id) {
    case EngineID::FERMI_TWOD_A:
        dma_pusher.BindSubchannel(&dma_pusher.channel_state.payload->fermi_2d, method_call.subchannel, EngineTypes::Fermi2D);
        break;
    case EngineID::MAXWELL_B:
        dma_pusher.BindSubchannel(&dma_pusher.channel_state.payload->maxwell_3d, method_call.subchannel, EngineTypes::Maxwell3D);
        break;
    case EngineID::KEPLER_COMPUTE_B:
        dma_pusher.BindSubchannel(&dma_pusher.channel_state.payload->kepler_compute, method_call.subchannel, EngineTypes::KeplerCompute);
        break;
    case EngineID::MAXWELL_DMA_COPY_A:
        dma_pusher.BindSubchannel(&dma_pusher.channel_state.payload->maxwell_dma, method_call.subchannel, EngineTypes::MaxwellDMA);
        break;
    case EngineID::KEPLER_INLINE_TO_MEMORY_B:
        dma_pusher.BindSubchannel(&dma_pusher.channel_state.payload->kepler_memory, method_call.subchannel, EngineTypes::KeplerMemory);
        break;
    case EngineID::NV01_TIMER:
        dma_pusher.BindSubchannel(&dma_pusher.channel_state.payload->nv01_timer, method_call.subchannel, EngineTypes::Nv01Timer);
        break;
    default:
        // An unknown class cannot continue dispatching into the previously
        // bound native engine. Drop its live DmaPusher pointer after retiring
        // any Ampere ownership; the null-guarded common DmaPusher refuses
        // subsequent guest methods until a valid bind. Leave legacy NS1's
        // historical binding policy unchanged.
        if (Service::Nvidia::Devices::NativeGpuChannelRegistry::Instance().IsNative(
                &dma_pusher.channel_state)) {
            dma_pusher.BindSubchannel(nullptr, method_call.subchannel,
                                      EngineTypes::Nv01Timer);
        }
        UNIMPLEMENTED_MSG("Unimplemented engine {:04X}", engine_id);
        break;
    }
}

void Puller::ProcessFenceActionMethod(DmaPusher& dma_pusher) {
    switch (regs.fence_action.op) {
    case Puller::FenceOperation::Acquire:
        // UNIMPLEMENTED_MSG("Channel Scheduling pending.");
        // WaitFence(regs.fence_action.syncpoint_id, regs.fence_value);
        dma_pusher.rasterizer->ReleaseFences();
        break;
    case Puller::FenceOperation::Increment:
        dma_pusher.rasterizer->SignalSyncPoint(regs.fence_action.syncpoint_id);
        break;
    default:
        UNIMPLEMENTED_MSG("Unimplemented operation {}", regs.fence_action.op.Value());
        break;
    }
}

void Puller::ProcessSemaphoreTriggerMethod(DmaPusher& dma_pusher) {
    const auto semaphoreOperationMask = 0xF;
    const auto op = GpuSemaphoreOperation(regs.semaphore_trigger & semaphoreOperationMask);
    if (op == GpuSemaphoreOperation::WriteLong) {
        const GPUVAddr sequence_address{regs.semaphore_address.SemaphoreAddress()};
        const u32 payload = regs.semaphore_sequence;
        dma_pusher.rasterizer->Query(sequence_address, VideoCommon::QueryType::Payload, VideoCommon::QueryPropertiesFlags::HasTimeout, payload, 0);
    } else {
        do {
            const u32 word = dma_pusher.memory_manager.Read<u32>(regs.semaphore_address.SemaphoreAddress());
            regs.acquire_source = true;
            regs.acquire_value = regs.semaphore_sequence;
            if (op == GpuSemaphoreOperation::AcquireEqual) {
                regs.acquire_active = true;
                regs.acquire_mode = false;
                if (word != regs.acquire_value) {
                    dma_pusher.rasterizer->ReleaseFences();
                    continue;
                }
            } else if (op == GpuSemaphoreOperation::AcquireGequal) {
                regs.acquire_active = true;
                regs.acquire_mode = true;
                if (word < regs.acquire_value) {
                    dma_pusher.rasterizer->ReleaseFences();
                    continue;
                }
            } else if (op == GpuSemaphoreOperation::AcquireMask) {
                if (word && regs.semaphore_sequence == 0) {
                    dma_pusher.rasterizer->ReleaseFences();
                    continue;
                }
            } else {
                LOG_ERROR(HW_GPU, "Invalid semaphore operation");
            }
        } while (false);
    }
}

void Puller::ProcessSemaphoreRelease(DmaPusher& dma_pusher) {
    const GPUVAddr sequence_address{regs.semaphore_address.SemaphoreAddress()};
    const u32 payload = regs.semaphore_release;
    dma_pusher.rasterizer->Query(sequence_address, VideoCommon::QueryType::Payload, VideoCommon::QueryPropertiesFlags::IsAFence, payload, 0);
}

// 0xC797 Ampere host SEM_EXECUTE (0x1b) uses registers 0x17/0x18
// for the address and 0x19 for payload. Never send the method argument
// through the Maxwell/NS1 semaphore-release path as a fake payload.
void Puller::ProcessNativeSemaphoreExecute(DmaPusher& dma_pusher, u32 control) {
    const GPUVAddr address = NativeSemaphore::Address(
        regs.reg_array[0x18], regs.reg_array[0x17]);
    const u32 value = regs.reg_array[0x19];
    const u32 operation = control & 7U;
    if (!NativeSemaphore::IsAddressAdmissible(address)) {
        throw std::runtime_error("Ampere SEM_EXECUTE: invalid GPU address");
    }
    if (operation == 1) { // RELEASE: schedule a genuine GPU fence write.
        dma_pusher.rasterizer->Query(
            address, VideoCommon::QueryType::Payload,
            VideoCommon::QueryPropertiesFlags::IsAFence, value, 0);
        return;
    }
    // A pending acquire requires suspending the GPFIFO channel. The
    // existing Maxwell busy loop must NEVER run on Ampere's 0x1a method,
    // which is instead SEM_PAYLOAD_HI. An unresolved compare must not
    // advance subsequent guest commands as if a release had occurred.
    dma_pusher.rasterizer->ReleaseFences();
    // A native GPU semaphore may be written by an earlier Compute/3D
    // dispatch but still live only in the Vulkan buffer cache. Read the
    // actual GMMU-backed word, never a stale CPU copy. The release path
    // above remains GPU-ordered and does not require a CPU alias.
    const auto cpu_address =
        dma_pusher.memory_manager.GpuToCpuAddress(address, sizeof(u32));
    if (!cpu_address) {
        throw std::runtime_error("Ampere SEM_EXECUTE: unmapped acquire address");
    }
    if (dma_pusher.rasterizer->MustFlushRegion(
            *cpu_address, sizeof(u32), VideoCommon::CacheType::BufferCache)) {
        dma_pusher.rasterizer->FlushRegion(
            *cpu_address, sizeof(u32), VideoCommon::CacheType::BufferCache);
    }
    const auto ready = NativeSemaphore::AcquireReady(
        operation, dma_pusher.memory_manager.Read<u32>(address), value);
    if (!ready || !*ready) {
        LOG_ERROR(HW_GPU,
                  "Ampere SEM_EXECUTE acquire unresolved or unsupported (op={})",
                  operation);
        throw std::runtime_error("Ampere SEM_EXECUTE: pending acquire unsupported");
    }
}

void Puller::ProcessSemaphoreAcquire(DmaPusher& dma_pusher) {
    u32 word = dma_pusher.memory_manager.Read<u32>(regs.semaphore_address.SemaphoreAddress());
    const auto value = regs.semaphore_acquire;
#ifdef PS5_NATIVE
    // On a failed graphics worker, a stale GPU semaphore can never progress.
    // Sample failure once per 64 unsuccessful polls, starting with the first.
    unsigned acquire_polls = 0;
#endif
    while (word != value && !dma_pusher.system.IsShuttingDown()) {
#ifdef PS5_NATIVE
        if ((acquire_polls++ & 63u) == 0u) {
            if (auto failure = ::Eden::PeekGpuFailure())
                std::rethrow_exception(failure);
        }
#endif
        regs.acquire_active = true;
        regs.acquire_value = value;
        dma_pusher.rasterizer->ReleaseFences();
        word = dma_pusher.memory_manager.Read<u32>(regs.semaphore_address.SemaphoreAddress());
        // TODO(kemathe73) figure out how to do the acquire_timeout
        regs.acquire_mode = false;
        regs.acquire_source = false;
    }
}

/// Calls a GPU puller method.
void Puller::CallPullerMethod(DmaPusher& dma_pusher, const MethodCall& method_call) {
    regs.reg_array[method_call.method] = method_call.argument;
    const auto method = static_cast<BufferMethods>(method_call.method);

    switch (method) {
    case BufferMethods::BindObject: {
        ProcessBindMethod(dma_pusher, method_call);
        break;
    }
    case BufferMethods::Nop:
    case BufferMethods::SemaphoreAddressHigh:
    case BufferMethods::SemaphoreAddressLow:
    case BufferMethods::SemaphoreSequencePayload:
    case BufferMethods::SyncpointPayload:
    case BufferMethods::WrcacheFlush:
        break;
    case BufferMethods::RefCnt:
        dma_pusher.rasterizer->SignalReference();
        break;
    case BufferMethods::SyncpointOperation:
        ProcessFenceActionMethod(dma_pusher);
        break;
    case BufferMethods::WaitForIdle:
        dma_pusher.rasterizer->WaitForIdle();
        break;
    case BufferMethods::SemaphoreOperation: {
        ProcessSemaphoreTriggerMethod(dma_pusher);
        break;
    }
    case BufferMethods::NonStallInterrupt: {
        LOG_ERROR(HW_GPU, "Special puller engine method NonStallInterrupt not implemented");
        break;
    }
    case BufferMethods::MemOpA: {
        LOG_ERROR(HW_GPU, "Memory Operation A");
        break;
    }
    case BufferMethods::MemOpB: {
        // Implement this better.
        dma_pusher.rasterizer->InvalidateGPUCache();
        break;
    }
    case BufferMethods::MemOpC:
    case BufferMethods::MemOpD: {
        LOG_ERROR(HW_GPU, "Memory Operation C,D");
        break;
    }
    case BufferMethods::SemaphoreAcquire: {
        // Native 0x1a carries the high semaphore payload, not a Maxwell wait.
        if (!Service::Nvidia::Devices::NativeGpuChannelRegistry::Instance()
                 .IsNative(&dma_pusher.channel_state))
            ProcessSemaphoreAcquire(dma_pusher);
        break;
    }
    case BufferMethods::SemaphoreRelease: {
        if (Service::Nvidia::Devices::NativeGpuChannelRegistry::Instance()
                .IsNative(&dma_pusher.channel_state))
            ProcessNativeSemaphoreExecute(dma_pusher, method_call.argument);
        else
            ProcessSemaphoreRelease(dma_pusher);
        break;
    }
    case BufferMethods::Yield: {
        // TODO(Kmather73): Research and implement this method.
        LOG_ERROR(HW_GPU, "Special puller engine method Yield not implemented");
        break;
    }
    default:
        LOG_ERROR(HW_GPU, "Special puller engine method {:X} not implemented", method);
        break;
    }
}

// Research-only native Ampere 3D MME. The per-DmaPusher interpreter is
// separate from Switch1 Maxwell macro code; unsupported paths fail closed.
void Puller::CallNative3D(DmaPusher& pusher, u32 method,
                          std::span<const u32> values, bool last) {
    if (!Service::Nvidia::Devices::NativeGpuChannelRegistry::Instance().IsNative(
            &pusher.channel_state) || !native_3d || native_3d_failed) {
        UNIMPLEMENTED_MSG("Native 3D channel or macro state unavailable");
        return;
    }
    auto& engine=pusher.channel_state.payload->maxwell_3d;
    const auto fail=[&](const char* why) {
        LOG_ERROR(HW_GPU, "Ampere native macro rejected: {}", why);
        native_3d_failed=true;
        native_macro_method=0;
        native_parameters.clear();
    };
    if (!native_macro_method) {
        if (method==MAXWELL3D_REG_INDEX(load_mme.instruction_ptr)) {
            if (values.size()!=1 || !native_3d->SetCodePointer(values[0]))
                fail("instruction pointer outside 96-bit code RAM");
            return;
        }
        if (method==MAXWELL3D_REG_INDEX(load_mme.instruction)) {
            for (u32 value:values)
                if (!native_3d->PushCode(value)) {
                    fail("native MME upload outside code RAM");
                    return;
                }
            return;
        }
        if (method==MAXWELL3D_REG_INDEX(load_mme.start_address_ptr)) {
            if (values.size()!=1 || !native_3d->SetPositionPointer(values[0]))
                fail("invalid native macro bind pointer");
            return;
        }
        if (method==MAXWELL3D_REG_INDEX(load_mme.start_address)) {
            for (u32 value:values)
                if (!native_3d->BindPosition(value)) {
                    fail("invalid 96-bit macro entry");
                    return;
                }
            return;
        }
    }
    if (method>=0xe00) {
        if (method>=0xf00 || values.empty()) {
            fail("unsupported native MME method");
            return;
        }
        if (!native_macro_method) {
            if ((method&1u)!=0u) {
                fail("macro trigger must start at even method");
                return;
            }
            native_macro_method=method;
        } else if (method!=native_macro_method+1u) {
            fail("macro FIFO must continue on next method");
            return;
        }
        if (values.size() > Tegra::NativeAmpereMacroState::MaxFifo -
                             native_parameters.size()) {
            fail("native macro FIFO capacity exceeded");
            return;
        }
        native_parameters.insert(native_parameters.end(),values.begin(),values.end());
        if (!last) return;
        engine.ConsumeSink(pusher.system);
        bool bad_read{};
        const auto read=[&](u32 addr)->u32 {
            if (addr>=Tegra::Engines::Maxwell3D::Regs::NUM_REGS) {
                bad_read=true;
                return 0;
            }
            return engine.regs.reg_array[addr];
        };
        const auto allowed = [](u32 addr, u32) {
            return addr >= 0x40 && addr < Tegra::Engines::Maxwell3D::Regs::NUM_REGS;
        };
        const auto commit = [&](u32 addr, u32 value) {
            engine.CallMethod(pusher.system, addr, value, true);
        };
        Tegra::NativeAmpereMacroState::Result outcome{};
        try {
            outcome = native_3d->ExecuteStaged(
                native_macro_method, std::span<const u32>{native_parameters},
                read, allowed, commit, [&] { return !bad_read; });
        } catch (...) {
            // A method commit may already have reached the GPU. Retire
            // this native channel, never resume its partially applied MME.
            fail("native 3D MME staging/commit exception");
            return;
        }
        native_macro_method=0;
        native_parameters.clear();
        if (!outcome.success || bad_read) {
            fail("native MME interpreter or register access failure");
            return;
        }
        engine.draw_manager.DrawDeferred(engine);
        return;
    }
    if (native_macro_method) {
        fail("native macro interrupted by ordinary GPU register write");
        return;
    }
    if (method>=Tegra::Engines::Maxwell3D::Regs::NUM_REGS) {
        fail("native 3D register outside compatible Maxwell range");
        return;
    }
    for (size_t i=0;i<values.size();++i)
        engine.CallMethod(pusher.system,method,values[i],last && i+1==values.size());
}

// TU104 Compute MME upload / execution; separate from 3D and all NS1
// Maxwell macro code. Executed macro PCAS writes reach the real QMD3 owner.
void Puller::CallNativeCompute(DmaPusher& pusher, u32 method,
                               std::span<const u32> values, bool last,
                               u32 subchannel) {
    if (!Service::Nvidia::Devices::NativeGpuChannelRegistry::Instance().IsNative(
            &pusher.channel_state) || native_compute_failed) return;
    if (!native_compute)
        native_compute = std::make_unique<Tegra::NativeAmpereMacroState>();
    const auto fail = [&](const char* reason) {
        native_compute_failed = true;
        native_compute_macro_method = 0;
        native_compute_parameters.clear();
        LOG_ERROR(HW_GPU, "Native Ampere Compute MME rejected: {}", reason);
    };
    if (!native_compute_macro_method && method >= 0x45 && method <= 0x48) {
        if (method == 0x45) {
            if (values.size() != 1 || !native_compute->SetCodePointer(values[0]))
                fail("compute MME code pointer invalid");
        } else if (method == 0x46) {
            for (u32 value : values)
                if (!native_compute->PushCode(value)) {
                    fail("compute MME upload exceeded code RAM");
                    break;
                }
        } else if (method == 0x47) {
            if (values.size() != 1 || !native_compute->SetPositionPointer(values[0]))
                fail("compute MME position pointer invalid");
        } else {
            for (u32 value : values)
                if (!native_compute->BindPosition(value)) {
                    fail("compute MME entry not uploaded");
                    break;
                }
        }
        return;
    }
    if (method >= 0xe00) {
        if (method >= 0xf00 || values.empty()) {
            fail("invalid compute MME method");
            return;
        }
        if (!native_compute_macro_method) {
            if (method & 1u) {
                fail("compute MME must start at even method");
                return;
            }
            native_compute_macro_method = method;
        } else if (method != native_compute_macro_method + 1u) {
            fail("compute MME FIFO did not continue on argument method");
            return;
        }
        if (values.size() > Tegra::NativeAmpereMacroState::MaxFifo -
                                 native_compute_parameters.size()) {
            fail("compute MME FIFO limit");
            return;
        }
        native_compute_parameters.insert(native_compute_parameters.end(),
                                         values.begin(), values.end());
        if (!last) return;
        auto& engine = pusher.channel_state.payload->kepler_compute;
        // Ampere MME register operands are guest-controlled. Returning zero
        // for an out-of-range read used to make malformed scripts appear
        // successful, potentially issuing later QMD work with fake state.
        bool invalid_compute_register_read = false;
        const auto read = [&](u32 index) -> u32 {
            if (index >= Tegra::Engines::KeplerCompute::Regs::NUM_REGS) {
                invalid_compute_register_read = true;
                return 0;
            }
            return engine.regs.reg_array[index];
        };
        const auto allowed = [&](u32 reg, u32) {
            return !invalid_compute_register_read &&
                   reg < 0xe00 &&
                   reg < Tegra::Engines::KeplerCompute::Regs::NUM_REGS;
        };
        const auto commit = [&](u32 reg, u32 value) {
            if (reg == 0xae || reg == 0xaf || reg == 0xb0) {
                ProcessNativeComputeQmd(pusher, reg, value, subchannel);
            } else if (Tegra::NativeComputePlainRegister(reg)) {
                // Commit the validated register snapshot in emission order.
                engine.regs.reg_array[reg] = value;
            } else {
                engine.CallMethod(pusher.system, reg, value, true);
                if (reg == 0x6c3)
                    ProcessNativeComputeReportSemaphore(pusher, value);
                if (reg == 0x107)
                    ProcessNativeComputeInlineQmd(pusher, subchannel);
            }
        };
        Tegra::NativeAmpereMacroState::Result result{};
        try {
            result = native_compute->ExecuteStaged(
                native_compute_macro_method, native_compute_parameters,
                read, allowed, commit,
                [&] { return !invalid_compute_register_read; });
        } catch (...) {
            // Part of a QMD/PCAS method sequence might have committed.
            // Never resume this native Compute channel after an exception.
            fail("native Compute MME staging/commit exception");
            return;
        }
        native_compute_macro_method = 0;
        native_compute_parameters.clear();
        if (!result.success || invalid_compute_register_read)
            fail("compute MME interpreter or register access rejected");
        return;
    }
    if (native_compute_macro_method) {
        fail("compute MME FIFO interrupted");
        return;
    }
    if (method >= Tegra::Engines::KeplerCompute::Regs::NUM_REGS) {
        fail("compute method outside declared register file");
        return;
    }
    auto& engine = pusher.channel_state.payload->kepler_compute;
    if (Tegra::NativeComputePlainRegister(method)) {
        // The native bulk method writes one register repeatedly. No trigger
        // observes intermediate values for plain registers, so one final
        // assignment is identical to calling KeplerCompute for every word.
        if (!values.empty())
            engine.regs.reg_array[method] = values.back();
        return;
    }
    for (u32 value : values) {
        if (method == 0xae || method == 0xaf || method == 0xb0) {
            ProcessNativeComputeQmd(pusher, method, value, subchannel);
        } else {
            engine.CallMethod(pusher.system, method, value, true);
            if (method == 0x6c3)
                ProcessNativeComputeReportSemaphore(pusher, value);
            if (method == 0x107)
                ProcessNativeComputeInlineQmd(pusher, subchannel);
        }
    }
}

// Native Ampere inline QMD: the register window 0xC8..0x107 contains
// all 64 descriptor words. The final write (0x107) publishes a complete
// descriptor to guest GPU memory; NVN2 may then launch it immediately.
// Use the renderer's coherent inline upload, not a raw store that bypasses
// its shader, buffer, texture and query invalidation.
void Puller::ProcessNativeComputeInlineQmd(DmaPusher& pusher, u32 subchannel) {
    auto& registry = Service::Nvidia::Devices::NativeGpuChannelRegistry::Instance();
    const auto ticket = registry.CaptureTicket(&pusher.channel_state);
    const auto compute = registry.CaptureComputeTicket(
        &pusher.channel_state.payload->kepler_compute);
    if (!ticket || !compute || !registry.IsComputeCurrent(
            &pusher.channel_state.payload->kepler_compute, *compute) ||
        ticket->process != compute->process ||
        ticket->generation != compute->generation ||
        ticket->channel_generation != compute->channel_generation ||
        !pusher.rasterizer) return;

    auto& regs = pusher.channel_state.payload->kepler_compute.regs.reg_array;
    const u64 qmd_units = (u64{regs[0xc6]} << 32) | regs[0xc7];
    // Native QMD addresses are 256-byte units in the NS2 49-bit GPU VA
    // domain. Unlike the legacy PCAS pointer, the inline register pair can
    // describe the upper nine address bits without truncation.
    if (qmd_units >= (u64{1} << 41)) {
        LOG_ERROR(HW_GPU, "Native inline QMD destination exceeds 49-bit GPU VA");
        return;
    }
    const GPUVAddr address = qmd_units << 8;
    Tegra::NativeQmdScheduler::Descriptor descriptor{};
    std::copy_n(regs.begin() + 0xc8, descriptor.size(), descriptor.begin());
    if (!pusher.memory_manager.IsFullyMappedRange(address, sizeof(descriptor))) {
        LOG_ERROR(HW_GPU, "Native inline QMD destination is unmapped");
        return;
    }
    pusher.rasterizer->AccelerateInlineToMemory(
        address, sizeof(descriptor),
        std::span<const u8>{reinterpret_cast<const u8*>(descriptor.data()),
                            sizeof(descriptor)});
    // Some inline descriptors are staged without immediate execution.
    // Honor the same PCAS admission condition as the NX2 compute engine.
    const u32 control = regs[0xa7];
    if (!(control & (1U << 24)) && (descriptor[4] & (1U << 12)))
        return;
    // The full address is already validated and the 256-byte descriptor is
    // uploaded. Never downcast it into a 32-bit PCAS register: upper QMD3
    // addresses are legitimate and the legacy register must remain untouched.
    ProcessNativeComputeQmdAt(pusher, address, 3, subchannel, 0x107);
}

// Native Ampere SET_REPORT_SEMAPHORE_A-D (0x6c0-0x6c3).
// Keep this path separate from Maxwell compute; direct writes and the TU104
// MME emit both reach this owner after the register file is updated.
void Puller::ProcessNativeComputeReportSemaphore(DmaPusher& pusher, u32 control) {
    auto& registry = Service::Nvidia::Devices::NativeGpuChannelRegistry::Instance();
    const auto* channel = &pusher.channel_state;
    auto* memory = &pusher.memory_manager;
    const auto ticket = registry.CaptureTicket(channel);
    const auto compute = registry.CaptureComputeTicket(
        &pusher.channel_state.payload->kepler_compute);
    if (!ticket || !compute || !registry.IsComputeCurrent(
            &pusher.channel_state.payload->kepler_compute, *compute) ||
        ticket->process != compute->process ||
        ticket->generation != compute->generation ||
        ticket->channel_generation != compute->channel_generation ||
        !pusher.rasterizer) return;

    const auto& regs = pusher.channel_state.payload->kepler_compute.regs.reg_array;
    const GPUVAddr address =
        (GPUVAddr{regs[0x6c0] & 0x1ffffffU} << 32) | regs[0x6c1];
    const u32 payload = regs[0x6c2];
    const u32 operation = control & 3U;
    const bool one_word = (control & (1U << 28)) != 0;
    const bool reduction_enabled = (control & (1U << 3)) != 0;
    const std::size_t write_bytes =
        (reduction_enabled || one_word) ? sizeof(u32) : 16U;
    if ((operation != 0 && operation != 2) ||
        !Tegra::NativeComputeSemaphoreSpanValid(address, write_bytes) ||
        !memory->IsFullyMappedRange(address, write_bytes)) {
        LOG_ERROR(HW_GPU, "Native Compute report semaphore rejected: operation={} address={:#x}",
                  operation, address);
        return;
    }
    if (reduction_enabled) {
        const u32 reduction = (control >> 9) & 7U;
        pusher.rasterizer->SignalFence(
            [channel, memory, ticket = *ticket, address, payload, reduction] {
                // The deferred fence may execute after a sparse GMMU unmap.
                // Never let a guest-memory exception unwind through Vulkan
                // retirement or fabricate a completed reduction.
                try {
                    auto& current =
                        Service::Nvidia::Devices::NativeGpuChannelRegistry::Instance();
                    if (!current.IsCurrent(channel, ticket) ||
                        !Tegra::NativeComputeSemaphoreSpanValid(address, sizeof(u32)) ||
                        !memory->IsFullyMappedRange(address, sizeof(u32))) return;
                    const u32 old_value = memory->Read<u32>(address);
                    memory->Write<u32>(
                        address, Tegra::NativeComputeSemaphoreReduction(
                                     old_value, payload, reduction));
                } catch (...) {
#ifdef PS5_NATIVE
                    Eden::RecordGpuFailure(std::current_exception());
#endif
                }
            });
    } else {
        pusher.rasterizer->Query(
            address, VideoCommon::QueryType::Payload,
            one_word ? VideoCommon::QueryPropertiesFlags::IsAFence
                     : VideoCommon::QueryPropertiesFlags::HasTimeout,
            payload, 0);
    }
}

// Native QMD3 PCAS: zero-grid commands may release real fences, but a
// nonempty GPU program cannot complete without its compute shader backend.
void Puller::ProcessNativeComputeQmd(DmaPusher& pusher, u32 method,
                                     u32 argument, u32 subchannel) {
    if (method == 0xae || (method == 0xaf && !(argument & 2)) ||
        (method == 0xb0 && (argument & 15) <= 1)) return;
    const GPUVAddr address =
        GPUVAddr{pusher.channel_state.payload->kepler_compute.regs.reg_array[0xad]} << 8;
    ProcessNativeComputeQmdAt(pusher, address, argument, subchannel, method);
}

void Puller::ProcessNativeComputeQmdAt(DmaPusher& pusher, GPUVAddr address,
                                       u32 argument, u32 subchannel, u32 method) {
    auto& registry = Service::Nvidia::Devices::NativeGpuChannelRegistry::Instance();
    const auto ticket = registry.CaptureTicket(&pusher.channel_state);
    const auto compute_ticket = registry.CaptureComputeTicket(
        &pusher.channel_state.payload->kepler_compute);
    auto session = registry.AcquireNativeQmdSession(&pusher.channel_state);
    if (!ticket || !compute_ticket ||
        !registry.IsComputeCurrent(
            &pusher.channel_state.payload->kepler_compute, *compute_ticket) ||
        ticket->process != compute_ticket->process ||
        ticket->generation != compute_ticket->generation ||
        ticket->channel_generation != compute_ticket->channel_generation ||
        !session || !pusher.rasterizer) return;
    auto read = [&](u64 addr, Tegra::NativeQmdScheduler::Descriptor& data) {
        // Avoid passing an overflowing or non-49-bit GPU range to the
        // page-table walker, including dependent guest-supplied QMD VAs.
        constexpr u64 native_limit = u64{1} << 49;
        if (addr >= native_limit || sizeof(data) > native_limit - addr ||
            !pusher.memory_manager.IsFullyMappedRange(addr, sizeof(data)))
            return false;
        // QMD3 may be generated by a preceding Ampere compute/DMA command.
        // Unsafe reads bypass BufferCache/QueryCache downloads and can
        // snapshot an obsolete host copy, leaving guest dependencies stuck.
        // Use the same mapped, cache-coherent GMMU read as native shader
        // capture; it flushes the affected physical ranges before copying.
        // Do NOT wait on the GPU pusher's own queue or synthesize a fence.
        pusher.memory_manager.ReadBlock(addr, data.data(), sizeof(data));
        return true;
    };
    auto release = [&](u64 addr, u32 payload, u32 flags) {
        constexpr u64 native_limit = u64{1} << 49;
        if (!addr || (addr & 3U) || addr >= native_limit ||
            sizeof(u32) > native_limit - addr ||
            !pusher.memory_manager.IsFullyMappedRange(addr, sizeof(u32)))
            return false;
        const auto* channel = &pusher.channel_state;
        auto* memory = &pusher.memory_manager;
        pusher.rasterizer->SignalFence(
            [channel, memory, ticket = *ticket, addr, payload, flags] {
                // A QMD3 dispatch can already be accepted when this
                // completion callback runs. If guest memory disappears or
                // fails during Read/Write, latch the fatal worker error;
                // neither publish a guessed semaphore nor unwind Vulkan.
                try {
                    const auto& current =
                        Service::Nvidia::Devices::NativeGpuChannelRegistry::Instance();
                    if (!current.IsCurrent(channel, ticket) ||
                        !memory->IsFullyMappedRange(addr, sizeof(u32))) return;
                    u32 value = payload;
                    if (flags & (1u << 26)) value = memory->Read<u32>(addr) + payload;
                    else if (flags & (1u << 19))
                        value = std::max(memory->Read<u32>(addr), payload);
                    memory->Write<u32>(addr, value);
                } catch (...) {
#ifdef PS5_NATIVE
                    Eden::RecordGpuFailure(std::current_exception());
#endif
                }
            });
        return true;
    };
    std::string_view error;
    {
        const std::scoped_lock lock{session->mutex};
        error = session->scheduler.Signal(
            address, argument, read, release,
            [&pusher, compute_ticket = *compute_ticket,
             channel_ticket = *ticket](const Tegra::NativeQmdScheduler::Descriptor& descriptor) {
                // Capture and verify a bounded native Ampere launch and
                // its guest GMMU spans. The eventual backend must still
                // compile, bind, submit and fence the real shader; never
                // report completion for a prepared-only launch.
                const auto launch = Tegra::DecodeNativeComputeLaunch(descriptor);
                const bool mapped = launch && Tegra::NativeComputeLaunchMapped(
                    *launch, [&pusher](u64 address, std::size_t bytes) {
                        return pusher.memory_manager.IsFullyMappedRange(address, bytes);
                    });
                // GPU work must not be submitted when one of its enabled
                // completion semaphores points at an invalid/unmapped GMMU
                // span. Otherwise the shader can run but never release its
                // dependent queue, and a later retry can publish a false
                // completion for work already submitted.
                const bool release_targets_mapped =
                    Tegra::NativeQmdReleaseTargetsMapped(
                        descriptor, [&pusher](u64 address, std::size_t bytes) {
                            return pusher.memory_manager.IsFullyMappedRange(address, bytes);
                        });
                // The shader bridge can be switched off during host
                // preflight; still compile the same shared source with
                // -Werror=unused-but-set-variable under either definition.
                (void)release_targets_mapped;
#ifdef PS5_NATIVE
                static std::atomic<unsigned> attempts{0};
                const auto n = attempts.fetch_add(1, std::memory_order_relaxed) + 1;
                const bool sample = n <= 4 || (n & (n - 1)) == 0;
                // Do not invoke CaptureNativeComputeShader here for sampled
                // diagnostics: DispatchNativeCompute authenticates and
                // translates the same QMD3 program below, with real Vulkan
                // submission and channel-epoch admission. Double capture
                // previously repeated GMMU readback and cache lookup on
                // the critical GPU worker path without contributing a
                // frame, fence, or distinct validation.
                if (sample && (!mapped || !release_targets_mapped))
                    std::fprintf(stderr,
                        "EDEN_NS2_COMPUTE_PREPARE_REJECTED count=%u mapped=%u release_targets=%u\n",
                        n, static_cast<unsigned>(mapped),
                        static_cast<unsigned>(release_targets_mapped));
#else
                (void)mapped;
                (void)release_targets_mapped;
                (void)compute_ticket;
                (void)channel_ticket;
#endif
#ifdef PS5_NATIVE
                if (mapped && release_targets_mapped) {
                    // This is the only native QMD3 -> rasterizer handoff.
                    // Do not treat a valid capture as a submitted shader, and
                    // never expose dependent releases for a retired GPU epoch.
                    auto& live_registry =
                        Service::Nvidia::Devices::NativeGpuChannelRegistry::Instance();
                    if (!live_registry.IsCurrent(&pusher.channel_state, channel_ticket) ||
                        !live_registry.IsComputeCurrent(
                            &pusher.channel_state.payload->kepler_compute, compute_ticket))
                        return false;
                    const bool accepted =
                        pusher.rasterizer->DispatchNativeCompute(*launch);
                    if (sample)
                        std::fprintf(stderr,
                            "EDEN_NS2_COMPUTE_DISPATCH count=%u accepted=%u program=%llx grid=%u,%u,%u\n",
                            n, static_cast<unsigned>(accepted),
                            static_cast<unsigned long long>(launch->program_address),
                            launch->grid[0], launch->grid[1], launch->grid[2]);
                    return accepted;
                }
#endif
                return false;
            });
    }
    if (!error.empty()) {
#ifdef PS5_NATIVE
        static std::atomic<unsigned> refused{0};
        const auto n = refused.fetch_add(1, std::memory_order_relaxed) + 1;
        if (n <= 32 || (n & (n - 1)) == 0)
            std::fprintf(stderr,
                         "EDEN_NS2_COMPUTE_QMD_UNSUPPORTED count=%u sub=%u method=%03x address=%llx reason=%.*s\n",
                         n, static_cast<unsigned>(subchannel),
                         static_cast<unsigned>(method),
                         static_cast<unsigned long long>(address),
                         static_cast<int>(error.size()), error.data());
#endif
        LOG_ERROR(HW_GPU, "Native QMD3 rejected: {}", error);
    }
}

/// Calls a GPU engine method.
void Puller::CallEngineMethod(DmaPusher& dma_pusher, const MethodCall& method_call) {
    const EngineID engine = bound_engines[method_call.subchannel];
    if (engine == static_cast<EngineID>(0xc797)) {
        CallNative3D(dma_pusher,method_call.method,
                     std::span<const u32>{&method_call.argument,1},
                     method_call.IsLastCall());
        return;
    }
    if (engine == static_cast<EngineID>(0xc7c0)) {
        if (!Service::Nvidia::Devices::NativeGpuChannelRegistry::Instance().IsNative(
                &dma_pusher.channel_state)) {
            UNIMPLEMENTED_MSG("Ampere compute command on non-native channel");
            return;
        }
        CallNativeCompute(dma_pusher, method_call.method,
                          std::span<const u32>{&method_call.argument, 1},
                          method_call.IsLastCall(), method_call.subchannel);
        return;
    }
    if (engine == static_cast<EngineID>(0xc7b5)) {
        if (Service::Nvidia::Devices::NativeGpuChannelRegistry::Instance().IsNative(
                &dma_pusher.channel_state)) {
            dma_pusher.channel_state.payload->maxwell_dma.CallMethod(
                dma_pusher.system, method_call.method, method_call.argument,
                method_call.IsLastCall());
        } else {
            UNIMPLEMENTED_MSG("Ampere DMA method on a non-native GPU channel");
        }
        return;
    }
    switch (engine) {
    case EngineID::FERMI_TWOD_A:
        dma_pusher.channel_state.payload->fermi_2d.CallMethod(dma_pusher.system, method_call.method, method_call.argument, method_call.IsLastCall());
        break;
    case EngineID::MAXWELL_B:
        dma_pusher.channel_state.payload->maxwell_3d.CallMethod(dma_pusher.system, method_call.method, method_call.argument, method_call.IsLastCall());
        break;
    case EngineID::KEPLER_COMPUTE_B:
        dma_pusher.channel_state.payload->kepler_compute.CallMethod(dma_pusher.system, method_call.method, method_call.argument, method_call.IsLastCall());
        break;
    case EngineID::MAXWELL_DMA_COPY_A:
        dma_pusher.channel_state.payload->maxwell_dma.CallMethod(dma_pusher.system, method_call.method, method_call.argument, method_call.IsLastCall());
        break;
    case EngineID::KEPLER_INLINE_TO_MEMORY_B:
        dma_pusher.channel_state.payload->kepler_memory.CallMethod(dma_pusher.system, method_call.method, method_call.argument, method_call.IsLastCall());
        break;
    case EngineID::NV01_TIMER:
        dma_pusher.channel_state.payload->nv01_timer.CallMethod(dma_pusher.system, method_call.method, method_call.argument, method_call.IsLastCall());
        break;
    default:
        UNIMPLEMENTED_MSG("Unimplemented engine");
        break;
    }
}

/// Calls a GPU engine multivalue method.
void Puller::CallEngineMultiMethod(DmaPusher& dma_pusher, u32 method, u32 subchannel, const u32* base_start, u32 amount, u32 methods_pending) {
    const EngineID engine = bound_engines[subchannel];
    if (engine == static_cast<EngineID>(0xc797)) {
        CallNative3D(dma_pusher,method,
                     std::span<const u32>{base_start,amount},
                     amount==methods_pending);
        return;
    }
    if (engine == static_cast<EngineID>(0xc7c0)) {
        if (!Service::Nvidia::Devices::NativeGpuChannelRegistry::Instance().IsNative(
                &dma_pusher.channel_state)) {
            UNIMPLEMENTED_MSG("Ampere compute multimethod on non-native channel");
            return;
        }
        if ((amount && !base_start) || amount > 4096) {
            UNIMPLEMENTED_MSG("Ampere compute multiword payload invalid");
            return;
        }
        CallNativeCompute(dma_pusher, method,
                          std::span<const u32>{base_start, amount},
                          amount == methods_pending, subchannel);
        return;
    }
    if (engine == static_cast<EngineID>(0xc7b5)) {
        if (Service::Nvidia::Devices::NativeGpuChannelRegistry::Instance().IsNative(
                &dma_pusher.channel_state)) {
            dma_pusher.channel_state.payload->maxwell_dma.CallMultiMethod(
                dma_pusher.system, method, base_start, amount, methods_pending);
        } else {
            UNIMPLEMENTED_MSG("Ampere DMA multimethod on a non-native GPU channel");
        }
        return;
    }
    switch (engine) {
    case EngineID::FERMI_TWOD_A:
        dma_pusher.channel_state.payload->fermi_2d.CallMultiMethod(dma_pusher.system, method, base_start, amount, methods_pending);
        break;
    case EngineID::MAXWELL_B:
        dma_pusher.channel_state.payload->maxwell_3d.CallMultiMethod(dma_pusher.system, method, base_start, amount, methods_pending);
        break;
    case EngineID::KEPLER_COMPUTE_B:
        dma_pusher.channel_state.payload->kepler_compute.CallMultiMethod(dma_pusher.system, method, base_start, amount, methods_pending);
        break;
    case EngineID::MAXWELL_DMA_COPY_A:
        dma_pusher.channel_state.payload->maxwell_dma.CallMultiMethod(dma_pusher.system, method, base_start, amount, methods_pending);
        break;
    case EngineID::KEPLER_INLINE_TO_MEMORY_B:
        dma_pusher.channel_state.payload->kepler_memory.CallMultiMethod(dma_pusher.system, method, base_start, amount, methods_pending);
        break;
    case EngineID::NV01_TIMER:
        dma_pusher.channel_state.payload->nv01_timer.CallMultiMethod(dma_pusher.system, method, base_start, amount, methods_pending);
        break;
    default:
        UNIMPLEMENTED_MSG("Unimplemented engine");
        break;
    }
}

/// Calls a GPU method.
void Puller::CallMethod(DmaPusher& dma_pusher, const MethodCall& method_call) {
    LOG_TRACE(HW_GPU, "Processing method {:08X} on subchannel {}", method_call.method, method_call.subchannel);
    ASSERT(method_call.subchannel < bound_engines.size());

    if (ExecuteMethodOnEngine(dma_pusher, method_call.method)) {
        CallEngineMethod(dma_pusher, method_call);
    } else {
        CallPullerMethod(dma_pusher, method_call);
    }
}

/// Calls a GPU multivalue method.
void Puller::CallMultiMethod(DmaPusher& dma_pusher, u32 method, u32 subchannel, const u32* base_start, u32 amount, u32 methods_pending) {
    LOG_TRACE(HW_GPU, "Processing method {:08X} on subchannel {}", method, subchannel);
    ASSERT(subchannel < bound_engines.size());
    if (ExecuteMethodOnEngine(dma_pusher, method)) {
        CallEngineMultiMethod(dma_pusher, method, subchannel, base_start, amount, methods_pending);
    } else {
        for (u32 i = 0; i < amount; i++) {
            CallPullerMethod(dma_pusher, MethodCall{
                method,
                base_start[i],
                subchannel,
                methods_pending - i,
            });
        }
    }
}

/// Determines where the method should be executed.
[[nodiscard]] bool Puller::ExecuteMethodOnEngine(DmaPusher& dma_pusher, u32 method) {
    const auto buffer_method = BufferMethods(method);
    return buffer_method >= BufferMethods::NonPullerMethods;
}

} // namespace Tegra::Engines
