// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

// SPDX-FileCopyrightText: 2022 yuzu Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#include "common/assert.h"
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
#include "core/hle/service/nvdrv/devices/native_gpu_channel_registry.h"
#include "video_core/gpu.h"
#include "video_core/memory_manager.h"
#include "video_core/rasterizer_interface.h"

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
    bound_engines[method_call.subchannel] = engine_id;
    if (engine_id == static_cast<EngineID>(0xc797)) {
        if (!Service::Nvidia::Devices::NativeGpuChannelRegistry::Instance().IsNative(
                &dma_pusher.channel_state)) {
            UNIMPLEMENTED_MSG("Ampere 3D class on a non-native GPU channel");
            return;
        }
        native_3d = std::make_unique<Tegra::NativeAmpereMacroState>();
        native_parameters.clear();
        native_macro_method = 0;
        native_3d_failed = false;
        dma_pusher.BindSubchannel(&dma_pusher.channel_state.payload->maxwell_3d,
                                  method_call.subchannel, EngineTypes::Maxwell3D);
        return;
    }
    if (engine_id == static_cast<EngineID>(0xc7b5)) {
        // Upstream uses the same Maxwell DMA method layout for Ampere copy.
        // Restrict it to a live process-generation authenticated native channel.
        if (Service::Nvidia::Devices::NativeGpuChannelRegistry::Instance().IsNative(
                &dma_pusher.channel_state)) {
            dma_pusher.BindSubchannel(&dma_pusher.channel_state.payload->maxwell_dma,
                                      method_call.subchannel, EngineTypes::MaxwellDMA);
        } else {
            UNIMPLEMENTED_MSG("Ampere DMA class on a non-native GPU channel");
        }
        return;
    }
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

/// Calls a GPU engine method.
void Puller::CallEngineMethod(DmaPusher& dma_pusher, const MethodCall& method_call) {
    const EngineID engine = bound_engines[method_call.subchannel];
    if (engine == static_cast<EngineID>(0xc797)) {
        CallNative3D(dma_pusher,method_call.method,
                     std::span<const u32>{&method_call.argument,1},
                     method_call.IsLastCall());
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
