// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

// SPDX-FileCopyrightText: Copyright 2024 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "core/hle/service/cmif_types.h"
#include "core/hle/service/service.h"

namespace Service::AM {

struct Applet;
class IStorage;

class IAppletCommonFunctions final : public ServiceFramework<IAppletCommonFunctions> {
public:
    explicit IAppletCommonFunctions(Core::System& system_, std::shared_ptr<Applet> applet_);
    ~IAppletCommonFunctions() override;

private:
    Result SetTerminateResult(Result terminate_result);
    Result ReadThemeStorage(Out<u64> out_transfer_size, u64 offset,
                            OutBuffer<BufferAttr_HipcAutoSelect> out_buffer);
    Result WriteThemeStorage(u64 offset, InBuffer<BufferAttr_HipcAutoSelect> buffer);
    Result PushToAppletBoundChannel(SharedPointer<IStorage> storage);
    Result TryPopFromAppletBoundChannel(Out<SharedPointer<IStorage>> out_storage);
    Result SetHomeButtonDoubleClickEnabled(bool home_button_double_click_enabled);
    Result GetHomeButtonDoubleClickEnabled(Out<bool> out_home_button_double_click_enabled);
    Result IsHomeButtonShortPressedBlocked(Out<bool> out_is_home_button_short_pressed_blocked);
    Result IsVrModeCurtainRequired(Out<bool> out_is_required);
    Result IsSleepRequiredByHighTemperature(Out<bool> out_is_required);
    Result IsSleepRequiredByLowBattery(Out<bool> out_is_required);
    Result GetDisplayLogicalResolution(Out<s32> out_width, Out<s32> out_height);
    Result SetDisplayMagnification(f32 x, f32 y, f32 width, f32 height);
    Result SetCpuBoostRequestPriority(s32 priority);
    Result SetHandlingCaptureButtonShortPressedMessageEnabledForApplet(bool enabled);
    Result SetHandlingCaptureButtonLongPressedMessageEnabledForApplet(bool enabled);
    Result SetApplicationCoreUsageMode(u32 core_usage_mode);
    Result GetCurrentApplicationId(Out<u64> out_application_id);
    Result IsSystemAppletHomeMenu(Out<bool> out_is_home_menu);
    Result SetGpuTimeSliceBoost(s64 time_span);
    Result Unknown350(Out<u16> out_unknown);

    const std::shared_ptr<Applet> applet;
};

} // namespace Service::AM
