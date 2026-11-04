// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "core/core.h"
#include "core/hle/kernel/k_shared_memory.h"
#include "core/hle/kernel/svc_results.h"
#include "core/hle/service/ipc_helpers.h"
#include "core/hle/service/service.h"

namespace Service {

// The stpl:u user service returns a read-only guest HID shared-memory handle
// for command 300. This memory is not an event, and is not a claim that any
// GameShare remote player or input stream has been established.
struct Nx2GuestHidMemoryConfig {
    u64 size;
    u64 offset;
};
static_assert(sizeof(Nx2GuestHidMemoryConfig) == 0x10);

class INx2StreamPlayUserService final : public ServiceFramework<INx2StreamPlayUserService> {
public:
    explicit INx2StreamPlayUserService(Core::System& system_)
        : ServiceFramework{system_, "nx2:stpl:u::0"} {
        static const FunctionInfo functions[] = {
            {300, &INx2StreamPlayUserService::GetGuestHidSharedMemory,
             "GetGuestHidSharedMemory"},
        };
        RegisterHandlers(functions);
    }

    ~INx2StreamPlayUserService() override {
        if (guest_hid_memory) {
            guest_hid_memory->Close(kernel);
        }
    }

private:
    void GetGuestHidSharedMemory(HLERequestContext& ctx) {
        constexpr u64 GuestHidMemorySize = 0x9b000;
        auto& owner_kernel = system.Kernel();
        if (!guest_hid_memory) {
            auto* memory = Kernel::KSharedMemory::Create(owner_kernel);
            const auto result =
                memory ? memory->Initialize(owner_kernel, system.DeviceMemory(), nullptr,
                                            Kernel::Svc::MemoryPermission::ReadWrite,
                                            Kernel::Svc::MemoryPermission::Read,
                                            GuestHidMemorySize)
                       : Kernel::ResultOutOfResource;
            if (R_FAILED(result)) {
                if (memory) memory->Close(owner_kernel);
                // Never publish a success or copy a null handle on OOM.
                IPC::ResponseBuilder rb{ctx, 2};
                rb.Push(result);
                return;
            }
            Kernel::KSharedMemory::Register(owner_kernel, memory);
            guest_hid_memory = memory;
        }
        IPC::ResponseBuilder rb{ctx, 6, 1};
        rb.Push(ResultSuccess);
        rb.PushRaw(Nx2GuestHidMemoryConfig{GuestHidMemorySize, 0});
        rb.PushCopyObjects(ctx, guest_hid_memory);
    }

    Kernel::KSharedMemory* guest_hid_memory{};
};

// Switch 2 stream play (GameShare), stpl:u. Command 0 (u32 0x12) opens the user service the SDK
// keeps for the process; without an object the first nn::streamplay call jumps through null.
class INx2StreamPlayService final : public ServiceFramework<INx2StreamPlayService> {
public:
    explicit INx2StreamPlayService(Core::System& system_)
        : ServiceFramework{system_, "nx2:stpl:u"} {
        static const FunctionInfo functions[] = {
            {0, &INx2StreamPlayService::GetUserService, "GetUserService (guessed)"},
        };
        RegisterHandlers(functions);
    }

private:
    void GetUserService(HLERequestContext& ctx) {
        LOG_WARNING(Service, "(STUBBED) stpl:u 0 [8]={:#x}, returning a user service",
                    ctx.CommandBuffer()[8]);
        IPC::ResponseBuilder rb{ctx, 2, 0, 1};
        rb.Push(ResultSuccess);
        rb.PushIpcInterface<INx2StreamPlayUserService>(ctx, system);
    }
};

} // namespace Service
