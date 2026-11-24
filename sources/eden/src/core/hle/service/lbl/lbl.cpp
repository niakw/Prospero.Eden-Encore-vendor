// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

// SPDX-FileCopyrightText: Copyright 2018 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <cmath>
#include <memory>

#include "common/logging.h"
#include "core/hle/service/ipc_helpers.h"
#include "core/hle/service/lbl/lbl.h"
#include "core/hle/service/server_manager.h"
#include "core/hle/service/service.h"
#include "core/hle/service/sm/sm.h"

namespace Service::LBL {

class LBL final : public ServiceFramework<LBL> {
public:
    explicit LBL(Core::System& system_) : ServiceFramework{system_, "lbl"} {
        // clang-format off
        static const FunctionInfo functions[] = {
            {0, &LBL::SaveCurrentSetting, "SaveCurrentSetting"},
            {1, &LBL::LoadCurrentSetting, "LoadCurrentSetting"},
            {2, &LBL::SetCurrentBrightnessSetting, "SetCurrentBrightnessSetting"},
            {3, &LBL::GetCurrentBrightnessSetting, "GetCurrentBrightnessSetting"},
            {4, &LBL::ApplyCurrentBrightnessSettingToBacklight, "ApplyCurrentBrightnessSettingToBacklight"},
            {5, &LBL::GetBrightnessSettingAppliedToBacklight, "GetBrightnessSettingAppliedToBacklight"},
            {6, &LBL::SwitchBacklightOn, "SwitchBacklightOn"},
            {7, &LBL::SwitchBacklightOff, "SwitchBacklightOff"},
            {8, &LBL::GetBacklightSwitchStatus, "GetBacklightSwitchStatus"},
            {9, &LBL::EnableDimming, "EnableDimming"},
            {10, &LBL::DisableDimming, "DisableDimming"},
            {11, &LBL::IsDimmingEnabled, "IsDimmingEnabled"},
            {12, &LBL::EnableAutoBrightnessControl, "EnableAutoBrightnessControl"},
            {13, &LBL::DisableAutoBrightnessControl, "DisableAutoBrightnessControl"},
            {14, &LBL::IsAutoBrightnessControlEnabled, "IsAutoBrightnessControlEnabled"},
            {15, &LBL::SetAmbientLightSensorValue, "SetAmbientLightSensorValue"},
            {16, &LBL::GetAmbientLightSensorValue, "GetAmbientLightSensorValue"},
            {17, &LBL::SetBrightnessReflectionDelayLevel, "SetBrightnessReflectionDelayLevel"},
            {18, &LBL::GetBrightnessReflectionDelayLevel, "GetBrightnessReflectionDelayLevel"},
            {19, &LBL::SetCurrentBrightnessMapping, "SetCurrentBrightnessMapping"},
            {20, &LBL::GetCurrentBrightnessMapping, "GetCurrentBrightnessMapping"},
            {21, &LBL::SetCurrentAmbientLightSensorMapping, "SetCurrentAmbientLightSensorMapping"},
            {22, &LBL::GetCurrentAmbientLightSensorMapping, "GetCurrentAmbientLightSensorMapping"},
            {23, &LBL::IsAmbientLightSensorAvailable, "IsAmbientLightSensorAvailable"},
            {24, &LBL::SetCurrentBrightnessSettingForVrMode, "SetCurrentBrightnessSettingForVrMode"},
            {25, &LBL::GetCurrentBrightnessSettingForVrMode, "GetCurrentBrightnessSettingForVrMode"},
            {26, &LBL::EnableVrMode, "EnableVrMode"},
            {27, &LBL::DisableVrMode, "DisableVrMode"},
            {28, &LBL::IsVrModeEnabled, "IsVrModeEnabled"},
            {29, &LBL::IsAutoBrightnessControlSupported, "IsAutoBrightnessControlSupported"},
        };
        // clang-format on

        RegisterHandlers(functions);
    }

private:
    enum class BacklightSwitchStatus : u32 {
        Off = 0,
        On = 1,
    };

    void SaveCurrentSetting(HLERequestContext& ctx) {
        saved_current_brightness = current_brightness;
        saved_ambient_light_value = ambient_light_value;
        saved_current_vr_brightness = current_vr_brightness;
        saved_dimming = dimming;
        saved_backlight_enabled = backlight_enabled;
        saved_auto_brightness = auto_brightness;
        saved_vr_mode_enabled = vr_mode_enabled;
        has_saved_setting = true;
        LOG_DEBUG(Service_LBL, "Saved local backlight settings");

        IPC::ResponseBuilder rb{ctx, 2};
        rb.Push(ResultSuccess);
    }

    void LoadCurrentSetting(HLERequestContext& ctx) {
        if (has_saved_setting) {
            current_brightness = saved_current_brightness;
            ambient_light_value = saved_ambient_light_value;
            current_vr_brightness = saved_current_vr_brightness;
            dimming = saved_dimming;
            backlight_enabled = saved_backlight_enabled;
            auto_brightness = saved_auto_brightness;
            vr_mode_enabled = saved_vr_mode_enabled;
            update_instantly = true;
        }
        LOG_DEBUG(Service_LBL, "Loaded local backlight settings, has_saved={}", has_saved_setting);

        IPC::ResponseBuilder rb{ctx, 2};
        rb.Push(ResultSuccess);
    }

    void SetCurrentBrightnessSetting(HLERequestContext& ctx) {
        IPC::RequestParser rp{ctx};
        auto brightness = rp.Pop<float>();

        if (!std::isfinite(brightness)) {
            LOG_ERROR(Service_LBL, "Brightness is infinite!");
            brightness = 0.0f;
        }

        LOG_DEBUG(Service_LBL, "called brightness={}", brightness);

        current_brightness = brightness;
        update_instantly = true;

        IPC::ResponseBuilder rb{ctx, 2};
        rb.Push(ResultSuccess);
    }

    void GetCurrentBrightnessSetting(HLERequestContext& ctx) {
        auto brightness = current_brightness;
        if (!std::isfinite(brightness)) {
            LOG_ERROR(Service_LBL, "Brightness is infinite!");
            brightness = 0.0f;
        }

        LOG_DEBUG(Service_LBL, "called brightness={}", brightness);

        IPC::ResponseBuilder rb{ctx, 3};
        rb.Push(ResultSuccess);
        rb.Push(brightness);
    }

    void ApplyCurrentBrightnessSettingToBacklight(HLERequestContext& ctx) {
        applied_brightness = current_brightness;
        update_instantly = false;
        LOG_DEBUG(Service_LBL, "Applied guest brightness={}", applied_brightness);

        IPC::ResponseBuilder rb{ctx, 2};
        rb.Push(ResultSuccess);
    }

    void GetBrightnessSettingAppliedToBacklight(HLERequestContext& ctx) {
        auto brightness = applied_brightness;
        if (!std::isfinite(brightness)) {
            LOG_ERROR(Service_LBL, "Applied brightness is infinite!");
            brightness = 0.0f;
        }

        IPC::ResponseBuilder rb{ctx, 3};
        rb.Push(ResultSuccess);
        rb.Push(brightness);
    }

    void SwitchBacklightOn(HLERequestContext& ctx) {
        IPC::RequestParser rp{ctx};
        const auto fade_time = rp.Pop<u64_le>();
        LOG_DEBUG(Service_LBL, "SwitchBacklightOn fade_time={} (guest fade not mapped to PS5 host)", fade_time);

        backlight_enabled = true;

        IPC::ResponseBuilder rb{ctx, 2};
        rb.Push(ResultSuccess);
    }

    void SwitchBacklightOff(HLERequestContext& ctx) {
        IPC::RequestParser rp{ctx};
        const auto fade_time = rp.Pop<u64_le>();
        LOG_DEBUG(Service_LBL, "SwitchBacklightOff fade_time={} (guest fade not mapped to PS5 host)", fade_time);

        backlight_enabled = false;

        IPC::ResponseBuilder rb{ctx, 2};
        rb.Push(ResultSuccess);
    }

    void GetBacklightSwitchStatus(HLERequestContext& ctx) {
        LOG_DEBUG(Service_LBL, "called");

        IPC::ResponseBuilder rb{ctx, 3};
        rb.Push(ResultSuccess);
        rb.PushEnum<BacklightSwitchStatus>(backlight_enabled ? BacklightSwitchStatus::On
                                                             : BacklightSwitchStatus::Off);
    }

    void EnableDimming(HLERequestContext& ctx) {
        LOG_DEBUG(Service_LBL, "called");

        dimming = true;

        IPC::ResponseBuilder rb{ctx, 2};
        rb.Push(ResultSuccess);
    }

    void DisableDimming(HLERequestContext& ctx) {
        LOG_DEBUG(Service_LBL, "called");

        dimming = false;

        IPC::ResponseBuilder rb{ctx, 2};
        rb.Push(ResultSuccess);
    }

    void IsDimmingEnabled(HLERequestContext& ctx) {
        LOG_DEBUG(Service_LBL, "called");

        IPC::ResponseBuilder rb{ctx, 3};
        rb.Push(ResultSuccess);
        rb.Push(dimming);
    }

    void EnableAutoBrightnessControl(HLERequestContext& ctx) {
        LOG_DEBUG(Service_LBL, "called");
        auto_brightness = true;
        update_instantly = true;

        IPC::ResponseBuilder rb{ctx, 2};
        rb.Push(ResultSuccess);
    }

    void DisableAutoBrightnessControl(HLERequestContext& ctx) {
        LOG_DEBUG(Service_LBL, "called");
        auto_brightness = false;

        IPC::ResponseBuilder rb{ctx, 2};
        rb.Push(ResultSuccess);
    }

    void IsAutoBrightnessControlEnabled(HLERequestContext& ctx) {
        LOG_DEBUG(Service_LBL, "called");

        IPC::ResponseBuilder rb{ctx, 3};
        rb.Push(ResultSuccess);
        rb.Push(auto_brightness);
    }

    void SetAmbientLightSensorValue(HLERequestContext& ctx) {
        IPC::RequestParser rp{ctx};
        const auto light_value = rp.Pop<float>();

        LOG_DEBUG(Service_LBL, "called light_value={}", light_value);

        ambient_light_value = light_value;

        IPC::ResponseBuilder rb{ctx, 2};
        rb.Push(ResultSuccess);
    }

    void GetAmbientLightSensorValue(HLERequestContext& ctx) {
        LOG_DEBUG(Service_LBL, "called");

        IPC::ResponseBuilder rb{ctx, 3};
        rb.Push(ResultSuccess);
        rb.Push(ambient_light_value);
    }

    void SetBrightnessReflectionDelayLevel(HLERequestContext& ctx) {
        // This is Intentional, this function does absolutely nothing
        LOG_DEBUG(Service_LBL, "called");

        IPC::ResponseBuilder rb{ctx, 2};
        rb.Push(ResultSuccess);
    }

    void GetBrightnessReflectionDelayLevel(HLERequestContext& ctx) {
        // This is intentional, the function is hard coded to return 0.0f on hardware
        LOG_DEBUG(Service_LBL, "called");

        IPC::ResponseBuilder rb{ctx, 3};
        rb.Push(ResultSuccess);
        rb.Push(0.0f);
    }

    void SetCurrentBrightnessMapping(HLERequestContext& ctx) {
        // This is Intentional, this function does absolutely nothing
        LOG_DEBUG(Service_LBL, "called");

        IPC::ResponseBuilder rb{ctx, 2};
        rb.Push(ResultSuccess);
    }

    void GetCurrentBrightnessMapping(HLERequestContext& ctx) {
        // This is Intentional, this function does absolutely nothing
        LOG_DEBUG(Service_LBL, "called");

        IPC::ResponseBuilder rb{ctx, 2};
        rb.Push(ResultSuccess);
        // This function is suppose to return something but it seems like it doesn't
    }

    void SetCurrentAmbientLightSensorMapping(HLERequestContext& ctx) {
        // This is Intentional, this function does absolutely nothing
        LOG_DEBUG(Service_LBL, "called");

        IPC::ResponseBuilder rb{ctx, 2};
        rb.Push(ResultSuccess);
    }

    void GetCurrentAmbientLightSensorMapping(HLERequestContext& ctx) {
        // This is Intentional, this function does absolutely nothing
        LOG_DEBUG(Service_LBL, "called");

        IPC::ResponseBuilder rb{ctx, 2};
        rb.Push(ResultSuccess);
        // This function is suppose to return something but it seems like it doesn't
    }

    void IsAmbientLightSensorAvailable(HLERequestContext& ctx) {
        // PS5 does not provide a Switch ambient-light sensor to the guest. Keep the synthetic
        // Set/GetAmbientLightSensorValue path available, but report physical sensor absence.
        LOG_DEBUG(Service_LBL, "Ambient-light sensor unavailable on this host");
        IPC::ResponseBuilder rb{ctx, 3};
        rb.Push(ResultSuccess);
        rb.Push(false);
    }

    void SetCurrentBrightnessSettingForVrMode(HLERequestContext& ctx) {
        IPC::RequestParser rp{ctx};
        auto brightness = rp.Pop<float>();

        if (!std::isfinite(brightness)) {
            LOG_ERROR(Service_LBL, "Brightness is infinite!");
            brightness = 0.0f;
        }

        LOG_DEBUG(Service_LBL, "called brightness={}", brightness);

        current_vr_brightness = brightness;

        IPC::ResponseBuilder rb{ctx, 2};
        rb.Push(ResultSuccess);
    }

    void GetCurrentBrightnessSettingForVrMode(HLERequestContext& ctx) {
        auto brightness = current_vr_brightness;
        if (!std::isfinite(brightness)) {
            LOG_ERROR(Service_LBL, "Brightness is infinite!");
            brightness = 0.0f;
        }

        LOG_DEBUG(Service_LBL, "called brightness={}", brightness);

        IPC::ResponseBuilder rb{ctx, 3};
        rb.Push(ResultSuccess);
        rb.Push(brightness);
    }

    void EnableVrMode(HLERequestContext& ctx) {
        LOG_DEBUG(Service_LBL, "called");

        IPC::ResponseBuilder rb{ctx, 2};
        rb.Push(ResultSuccess);

        vr_mode_enabled = true;
    }

    void DisableVrMode(HLERequestContext& ctx) {
        LOG_DEBUG(Service_LBL, "called");

        IPC::ResponseBuilder rb{ctx, 2};
        rb.Push(ResultSuccess);

        vr_mode_enabled = false;
    }

    void IsVrModeEnabled(HLERequestContext& ctx) {
        LOG_DEBUG(Service_LBL, "called");

        IPC::ResponseBuilder rb{ctx, 3};
        rb.Push(ResultSuccess);
        rb.Push(vr_mode_enabled);
    }

    void IsAutoBrightnessControlSupported(HLERequestContext& ctx) {
        LOG_DEBUG(Service_LBL, "called");

        IPC::ResponseBuilder rb{ctx, 3};
        rb.Push(ResultSuccess);
        rb.Push<u8>(auto_brightness_supported);
    }

    bool vr_mode_enabled = false;
    float current_brightness = 1.0f;
    float applied_brightness = 1.0f;
    float ambient_light_value = 0.0f;
    float current_vr_brightness = 1.0f;
    bool dimming = true;
    bool backlight_enabled = true;
    bool update_instantly = false;
    bool auto_brightness = false;
    bool auto_brightness_supported = true; // TODO(ogniK): Move to system settings

    bool has_saved_setting = false;
    float saved_current_brightness = 1.0f;
    float saved_ambient_light_value = 0.0f;
    float saved_current_vr_brightness = 1.0f;
    bool saved_dimming = true;
    bool saved_backlight_enabled = true;
    bool saved_auto_brightness = false;
    bool saved_vr_mode_enabled = false;
};

void LoopProcess(Core::System& system) {
    auto server_manager = std::make_unique<ServerManager>(system);

    server_manager->RegisterNamedService("lbl", std::make_shared<LBL>(system));
    ServerManager::RunServer(std::move(server_manager));
}

} // namespace Service::LBL
