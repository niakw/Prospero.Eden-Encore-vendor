// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

// SPDX-FileCopyrightText: 2022 yuzu Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

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
#include <atomic>
#include <algorithm>
#include <limits>
#include <cstdio>
#include "core/hle/service/nvdrv/devices/native_gpu_channel_registry.h"
#include "video_core/gpu.h"
#include "video_core/memory_manager.h"
#include "video_core/rasterizer_interface.h"
#include "video_core/native_qmd.h"
#if defined(PS5_NATIVE) && defined(EDEN_NS2_VULKAN_SHADER_BRIDGE_EXPERIMENTAL)
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
    auto session = std::make_shared<Tegra::NativeQmdSession>();
    qmd_sessions[ticket.process] = {ticket.generation, session};
    return session;
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

void Puller::ProcessSemaphoreAcquire(DmaPusher& dma_pusher) {
    u32 word = dma_pusher.memory_manager.Read<u32>(regs.semaphore_address.SemaphoreAddress());
    const auto value = regs.semaphore_acquire;
    while (word != value) {
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
        ProcessSemaphoreAcquire(dma_pusher);
        break;
    }
    case BufferMethods::SemaphoreRelease: {
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
        const auto emit=[&](u32 addr,u32 value)->bool {
            if (addr<0x40 || addr>=Tegra::Engines::Maxwell3D::Regs::NUM_REGS)
                return false;
            engine.CallMethod(pusher.system,addr,value,true);
            return true;
        };
        const auto outcome=native_3d->Execute(
            native_macro_method,std::span<const u32>{native_parameters},read,emit);
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
        const auto read = [&](u32 index) {
            return index < Tegra::Engines::KeplerCompute::Regs::NUM_REGS
                       ? engine.regs.reg_array[index] : 0U;
        };
        const auto emit = [&](u32 reg, u32 value) {
            if (reg >= 0xe00 || reg >= Tegra::Engines::KeplerCompute::Regs::NUM_REGS)
                return false;
            if (reg == 0xae || reg == 0xaf || reg == 0xb0) {
                ProcessNativeComputeQmd(pusher, reg, value, subchannel);
            } else if (Tegra::NativeComputePlainRegister(reg)) {
                // Side-effect-free 0xC7C0 register write. Native MME emits
                // these at high frequency; the legacy Kepler method path
                // would only store this exact value in the same register.
                engine.regs.reg_array[reg] = value;
            } else {
                engine.CallMethod(pusher.system, reg, value, true);
                if (reg == 0x6c3)
                    ProcessNativeComputeReportSemaphore(pusher, value);
                if (reg == 0x107)
                    ProcessNativeComputeInlineQmd(pusher, subchannel);
            }
            return true;
        };
        const auto result = native_compute->Execute(
            native_compute_macro_method, native_compute_parameters, read, emit);
        native_compute_macro_method = 0;
        native_compute_parameters.clear();
        if (!result.success) fail("compute MME interpreter rejected instruction");
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
    if ((operation != 0 && operation != 2) || (address & 3U) ||
        !memory->IsFullyMappedRange(address, write_bytes)) {
        LOG_ERROR(HW_GPU, "Native Compute report semaphore rejected: operation={} address={:#x}",
                  operation, address);
        return;
    }
    if (reduction_enabled) {
        const u32 reduction = (control >> 9) & 7U;
        pusher.rasterizer->SignalFence(
            [channel, memory, ticket = *ticket, address, payload, reduction] {
                auto& current =
                    Service::Nvidia::Devices::NativeGpuChannelRegistry::Instance();
                if (!current.IsCurrent(channel, ticket) ||
                    !memory->IsFullyMappedRange(address, sizeof(u32))) return;
                const u32 old_value = memory->Read<u32>(address);
                memory->Write<u32>(
                    address, Tegra::NativeComputeSemaphoreReduction(
                                 old_value, payload, reduction));
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
        if (!pusher.memory_manager.IsFullyMappedRange(addr, sizeof(data))) return false;
        pusher.memory_manager.ReadBlockUnsafe(addr, data.data(), sizeof(data));
        return true;
    };
    auto release = [&](u64 addr, u32 payload, u32 flags) {
        if (!pusher.memory_manager.IsFullyMappedRange(addr, sizeof(u32))) return false;
        const auto* channel = &pusher.channel_state;
        auto* memory = &pusher.memory_manager;
        pusher.rasterizer->SignalFence(
            [channel, memory, ticket = *ticket, addr, payload, flags] {
                const auto& current =
                    Service::Nvidia::Devices::NativeGpuChannelRegistry::Instance();
                if (!current.IsCurrent(channel, ticket) ||
                    !memory->IsFullyMappedRange(addr, sizeof(u32))) return;
                u32 value = payload;
                if (flags & (1u << 26)) value = memory->Read<u32>(addr) + payload;
                else if (flags & (1u << 19))
                    value = std::max(memory->Read<u32>(addr), payload);
                memory->Write<u32>(addr, value);
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
#ifdef PS5_NATIVE
                static std::atomic<unsigned> attempts{0};
                const auto n = attempts.fetch_add(1, std::memory_order_relaxed) + 1;
                const bool sample = n <= 4 || (n & (n - 1)) == 0;
                if (sample) {
                    if (mapped) {
#if defined(EDEN_NS2_VULKAN_SHADER_BRIDGE_EXPERIMENTAL)
                        // Production-facing QMD3 capture: fetch the ACTUAL
                        // mapped guest Ampere program, lower to a bounded
                        // Maxwell image and recheck the owning GPU epoch.
                        // Do not substitute this for a Vulkan dispatch: no
                        // QMD resources or driver pipeline have been bound.
                        auto& registry =
                            Service::Nvidia::Devices::NativeGpuChannelRegistry::Instance();
                        auto cache = registry.AcquireComputeTranslationCache(
                            &pusher.channel_state.payload->kepler_compute,
                            compute_ticket);
                        const auto native = cache
                            ? Shader::Ampere::CaptureNativeComputeShader(
                                  launch->program_address, VideoCommon::ReadNs2GpuShader,
                                  &pusher.memory_manager, cache.get())
                            : Shader::Ampere::NativeCaptureResult{};
                        const bool current =
                            registry.IsCurrent(&pusher.channel_state, channel_ticket) &&
                            registry.IsComputeCurrent(
                                &pusher.channel_state.payload->kepler_compute, compute_ticket);
                        std::fprintf(stderr,
                            "EDEN_NS2_COMPUTE_SHADER_CAPTURE count=%u program=%llx mapped=1 current=%u ready=%u status=%u bytes=%u failing_at=%u backend=missing\n",
                            n, static_cast<unsigned long long>(launch->program_address),
                            static_cast<unsigned>(current),
                            static_cast<unsigned>(current && native.Ready()),
                            static_cast<unsigned>(native.status),
                            native.captured_native_bytes, native.first_error_offset);
#else
                        std::fprintf(stderr,
                            "EDEN_NS2_COMPUTE_PREPARED count=%u program=%llx grid=%u,%u,%u block=%u,%u,%u shader_bridge=off backend=missing\n",
                            n, static_cast<unsigned long long>(launch->program_address),
                            launch->grid[0], launch->grid[1], launch->grid[2],
                            launch->block[0], launch->block[1], launch->block[2]);
#endif
                    } else {
                        std::fprintf(stderr,
                            "EDEN_NS2_COMPUTE_PREPARE_REJECTED count=%u backend=missing\n", n);
                    }
                }
#else
                (void)mapped;
                (void)compute_ticket;
                (void)channel_ticket;
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
