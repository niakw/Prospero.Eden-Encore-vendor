// SPDX-FileCopyrightText: Copyright 2021 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <atomic>

#include "hid_core/resources/controller_base.h"

namespace Core::HID {
class EmulatedConsole;
} // namespace Core::HID

namespace Service::HID {
class ConsoleSixAxis final : public ControllerBase {
public:
    explicit ConsoleSixAxis(Core::HID::HIDCore& hid_core_);
    ~ConsoleSixAxis() override;

    // Called when the controller is initialized
    void OnInit() override;

    // When the controller is released
    void OnRelease() override;

    // When the controller is requesting an update for the shared memory
    void OnUpdate(const Core::Timing::CoreTiming& core_timing) override;

    void StartSampling(u64 aruid);
    void StopSampling(u64 aruid);

private:
    Core::HID::EmulatedConsole* console = nullptr;
    std::atomic<u64> sampling_aruid{};
    std::atomic_bool sampling_enabled{};
};
} // namespace Service::HID
