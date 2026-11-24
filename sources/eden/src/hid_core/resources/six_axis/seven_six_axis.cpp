// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

// SPDX-FileCopyrightText: Copyright 2023 yuzu Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#include <cstring>
#include <utility>
#include "common/common_types.h"
#include "core/core.h"
#include "core/core_timing.h"
#include "core/frontend/emu_window.h"
#include "core/hle/kernel/k_process.h"
#include "core/hle/kernel/k_transfer_memory.h"
#include "core/hle/kernel/svc_results.h"
#include "core/memory.h"
#include "hid_core/frontend/emulated_console.h"
#include "hid_core/frontend/emulated_devices.h"
#include "hid_core/hid_core.h"
#include "hid_core/resources/six_axis/seven_six_axis.h"

namespace Service::HID {
SevenSixAxis::SevenSixAxis(Core::System& system_)
    : ControllerBase{system_.HIDCore()}, system{system_} {
    console = hid_core.GetEmulatedConsole();
}

SevenSixAxis::~SevenSixAxis() {
    ReleaseTransferMemory();
}

void SevenSixAxis::OnInit() {}
void SevenSixAxis::OnRelease() {}

void SevenSixAxis::OnUpdate(const Core::Timing::CoreTiming& core_timing) {
    std::scoped_lock lk{transfer_mutex};
    if (!sampling_enabled || transfer_memory == nullptr) {
        return;
    }

    const auto& last_entry = seven_sixaxis_lifo.ReadCurrentEntry().state;
    next_seven_sixaxis_state.sampling_number = last_entry.sampling_number + 1;

    const auto motion_status = console->GetMotion();
    last_global_timestamp = core_timing.GetGlobalTimeNs().count();

    // This value increments every time the switch goes to sleep
    next_seven_sixaxis_state.unknown = 1;
    next_seven_sixaxis_state.timestamp = last_global_timestamp - last_saved_timestamp;
    next_seven_sixaxis_state.accel = motion_status.accel;
    next_seven_sixaxis_state.gyro = motion_status.gyro;
    next_seven_sixaxis_state.quaternion = {
        motion_status.quaternion[1],
        motion_status.quaternion[0],
        -motion_status.quaternion[3],
        -motion_status.quaternion[2],
    };
    seven_sixaxis_lifo.WriteNextEntry(next_seven_sixaxis_state);
    transfer_memory->GetOwner()->GetMemory().WriteBlock(
        transfer_memory->GetSourceAddress(), &seven_sixaxis_lifo, sizeof(seven_sixaxis_lifo));
}

Result SevenSixAxis::InitializeTransferMemory(u64 aruid, Kernel::KTransferMemory* first,
                                              Kernel::KTransferMemory* second) {
    R_UNLESS(first && second && first != second && first->GetOwner() == second->GetOwner(),
             Kernel::ResultInvalidHandle);
    R_UNLESS(first->GetSize() == 0x1000 && second->GetSize() == 0x7F000,
             Kernel::ResultInvalidSize);
    std::scoped_lock lk{transfer_mutex};
    R_UNLESS(transfer_memory == nullptr, Kernel::ResultInvalidState);
    R_UNLESS(first->Open(system.Kernel()), Kernel::ResultInvalidHandle);
    if (!second->Open(system.Kernel())) {
        first->Close(system.Kernel());
        R_THROW(Kernel::ResultInvalidHandle);
    }
    transfer_memory = first;
    auxiliary_memory = second;
    owner_aruid = aruid;
    sampling_enabled = false;
    seven_sixaxis_lifo.buffer_count = 0;
    seven_sixaxis_lifo.buffer_tail = 0;
    next_seven_sixaxis_state = {};
    last_global_timestamp = system.CoreTiming().GetGlobalTimeNs().count();
    last_saved_timestamp = last_global_timestamp;
    R_SUCCEED();
}

Result SevenSixAxis::StartSampling(u64 aruid) {
    std::scoped_lock lk{transfer_mutex};
    R_UNLESS(transfer_memory && owner_aruid == aruid, Kernel::ResultInvalidState);
    sampling_enabled = true;
    R_SUCCEED();
}

Result SevenSixAxis::StopSampling(u64 aruid) {
    std::scoped_lock lk{transfer_mutex};
    R_UNLESS(transfer_memory && owner_aruid == aruid, Kernel::ResultInvalidState);
    sampling_enabled = false;
    R_SUCCEED();
}

Result SevenSixAxis::FinalizeTransferMemory(u64 aruid) {
    Kernel::KTransferMemory* first{};
    Kernel::KTransferMemory* second{};
    {
        std::scoped_lock lk{transfer_mutex};
        R_UNLESS(transfer_memory && owner_aruid == aruid, Kernel::ResultInvalidState);
        sampling_enabled = false;
        owner_aruid = 0;
        first = std::exchange(transfer_memory, nullptr);
        second = std::exchange(auxiliary_memory, nullptr);
    }
    second->Close(system.Kernel());
    first->Close(system.Kernel());
    R_SUCCEED();
}

void SevenSixAxis::ReleaseTransferMemory() {
    Kernel::KTransferMemory* first{};
    Kernel::KTransferMemory* second{};
    {
        std::scoped_lock lk{transfer_mutex};
        sampling_enabled = false;
        owner_aruid = 0;
        first = std::exchange(transfer_memory, nullptr);
        second = std::exchange(auxiliary_memory, nullptr);
    }
    if (second) {
        second->Close(system.Kernel());
    }
    if (first) {
        first->Close(system.Kernel());
    }
}

void SevenSixAxis::ResetTimestamp() {
    std::scoped_lock lk{transfer_mutex};
    last_saved_timestamp = last_global_timestamp;
}

} // namespace Service::HID
