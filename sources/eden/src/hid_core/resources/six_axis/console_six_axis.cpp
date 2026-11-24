// SPDX-FileCopyrightText: Copyright 2021 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "core/core_timing.h"
#include "hid_core/frontend/emulated_console.h"
#include "hid_core/hid_core.h"
#include "hid_core/resources/shared_memory_format.h"
#include "hid_core/resources/six_axis/console_six_axis.h"

namespace Service::HID {

ConsoleSixAxis::ConsoleSixAxis(Core::HID::HIDCore& hid_core_) : ControllerBase{hid_core_} {
    console = hid_core.GetEmulatedConsole();
}

ConsoleSixAxis::~ConsoleSixAxis() = default;

void ConsoleSixAxis::OnInit() {}

void ConsoleSixAxis::OnRelease() {
    sampling_enabled.store(false, std::memory_order_release);
    sampling_aruid.store(0, std::memory_order_relaxed);
}

void ConsoleSixAxis::StartSampling(u64 aruid) {
    sampling_aruid.store(aruid, std::memory_order_relaxed);
    sampling_enabled.store(true, std::memory_order_release);
}

void ConsoleSixAxis::StopSampling(u64 aruid) {
    if (sampling_aruid.load(std::memory_order_relaxed) == aruid) {
        sampling_enabled.store(false, std::memory_order_release);
        sampling_aruid.store(0, std::memory_order_relaxed);
    }
}

void ConsoleSixAxis::OnUpdate(const Core::Timing::CoreTiming& core_timing) {
    std::scoped_lock shared_lock{*shared_mutex};
    const u64 aruid = applet_resource->GetActiveAruid();
    auto* data = applet_resource->GetAruidData(aruid);

    if (data == nullptr || !data->flag.is_assigned) {
        return;
    }

    if (!IsControllerActivated() ||
        !sampling_enabled.load(std::memory_order_acquire) ||
        sampling_aruid.load(std::memory_order_relaxed) != aruid) {
        return;
    }

    ConsoleSixAxisSensorSharedMemoryFormat& shared_memory = data->shared_memory_format->console;

    const auto motion_status = console->GetMotion();

    shared_memory.sampling_number++;
    shared_memory.is_seven_six_axis_sensor_at_rest = motion_status.is_at_rest;
    shared_memory.verticalization_error = motion_status.verticalization_error;
    shared_memory.gyro_bias = motion_status.gyro_bias;
}

} // namespace Service::HID
