// SPDX-FileCopyrightText: Copyright 2025 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

// SPDX-FileCopyrightText: Copyright 2023 yuzu Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#include <algorithm>

#include "core/hle/kernel/svc_results.h"
#include "core/hle/service/cmif_serialization.h"
#include "core/hle/service/hid/hid_debug_server.h"
#include "core/hle/service/ipc_helpers.h"
#include "hid_core/hid_types.h"
#include "hid_core/resource_manager.h"
#include "hid_core/resources/hid_firmware_settings.h"
#include "hid_core/resources/debug_pad/debug_pad.h"
#include "hid_core/resources/digitizer/digitizer.h"
#include "hid_core/resources/keyboard/keyboard.h"
#include "hid_core/resources/mouse/mouse.h"
#include "hid_core/resources/npad/npad.h"
#include "hid_core/resources/six_axis/console_six_axis.h"
#include "hid_core/resources/six_axis/seven_six_axis.h"
#include "hid_core/resources/system_buttons/capture_button.h"
#include "hid_core/resources/system_buttons/home_button.h"
#include "hid_core/resources/system_buttons/sleep_button.h"

#include "hid_core/resources/touch_screen/gesture.h"
#include "hid_core/resources/touch_screen/touch_screen.h"

namespace Service::HID {

IHidDebugServer::IHidDebugServer(Core::System& system_, std::shared_ptr<ResourceManager> resource,
                                 std::shared_ptr<HidFirmwareSettings> settings)
    : ServiceFramework{system_, "hid:dbg"}, resource_manager{resource}, firmware_settings{
                                                                            settings} {
    // clang-format off
    static const FunctionInfo functions[] = {
        {0, C<&IHidDebugServer::DeactivateDebugPad>, "DeactivateDebugPad"},
        {1, &IHidDebugServer::RejectUnsupportedDebugCommand, "SetDebugPadAutoPilotState"},
        {2, &IHidDebugServer::RejectUnsupportedDebugCommand, "UnsetDebugPadAutoPilotState"},
        {10, C<&IHidDebugServer::DeactivateTouchScreen>, "DeactivateTouchScreen"},
        {11, C<&IHidDebugServer::SetTouchScreenAutoPilotState>, "SetTouchScreenAutoPilotState"},
        {12, C<&IHidDebugServer::UnsetTouchScreenAutoPilotState>, "UnsetTouchScreenAutoPilotState"},
        {13, C<&IHidDebugServer::GetTouchScreenConfiguration>, "GetTouchScreenConfiguration"},
        {14, C<&IHidDebugServer::ProcessTouchScreenAutoTune>, "ProcessTouchScreenAutoTune"},
        {15, C<&IHidDebugServer::ForceStopTouchScreenManagement>, "ForceStopTouchScreenManagement"},
        {16, C<&IHidDebugServer::ForceRestartTouchScreenManagement>, "ForceRestartTouchScreenManagement"},
        {17, C<&IHidDebugServer::IsTouchScreenManaged>, "IsTouchScreenManaged"},
        {20, C<&IHidDebugServer::DeactivateMouse>, "DeactivateMouse"},
        {21, &IHidDebugServer::RejectUnsupportedDebugCommand, "SetMouseAutoPilotState"},
        {22, &IHidDebugServer::RejectUnsupportedDebugCommand, "UnsetMouseAutoPilotState"},
        {23, &IHidDebugServer::RejectUnsupportedDebugCommand, "AddMouseSideWheelDelta"},
        {25, &IHidDebugServer::RejectUnsupportedDebugCommand, "SetDebugMouseAutoPilotState"},
        {26, &IHidDebugServer::RejectUnsupportedDebugCommand, "UnsetDebugMouseAutoPilotState"},
        {30, C<&IHidDebugServer::DeactivateKeyboard>, "DeactivateKeyboard"},
        {31, &IHidDebugServer::RejectUnsupportedDebugCommand, "SetKeyboardAutoPilotState"},
        {32, &IHidDebugServer::RejectUnsupportedDebugCommand, "UnsetKeyboardAutoPilotState"},
        {50, &IHidDebugServer::RejectUnsupportedDebugCommand, "DeactivateXpad"},
        {51, &IHidDebugServer::RejectUnsupportedDebugCommand, "SetXpadAutoPilotState"},
        {52, &IHidDebugServer::RejectUnsupportedDebugCommand, "UnsetXpadAutoPilotState"},
        {53, &IHidDebugServer::RejectUnsupportedDebugCommand, "DeactivateJoyXpad"},
        {60, C<&IHidDebugServer::ClearNpadSystemCommonPolicy>, "ClearNpadSystemCommonPolicy"},
        {61, &IHidDebugServer::RejectUnsupportedDebugCommand, "DeactivateNpad"},
        {62, &IHidDebugServer::RejectUnsupportedDebugCommand, "ForceDisconnectNpad"},
        {91, C<&IHidDebugServer::DeactivateGesture>, "DeactivateGesture"},
        {110, C<&IHidDebugServer::DeactivateHomeButton>, "DeactivateHomeButton"},
        {111, &IHidDebugServer::RejectUnsupportedDebugCommand, "SetHomeButtonAutoPilotState"},
        {112, &IHidDebugServer::RejectUnsupportedDebugCommand, "UnsetHomeButtonAutoPilotState"},
        {120, C<&IHidDebugServer::DeactivateSleepButton>, "DeactivateSleepButton"},
        {121, &IHidDebugServer::RejectUnsupportedDebugCommand, "SetSleepButtonAutoPilotState"},
        {122, &IHidDebugServer::RejectUnsupportedDebugCommand, "UnsetSleepButtonAutoPilotState"},
        {123, &IHidDebugServer::RejectUnsupportedDebugCommand, "DeactivateInputDetector"},
        {130, C<&IHidDebugServer::DeactivateCaptureButton>, "DeactivateCaptureButton"},
        {131, &IHidDebugServer::RejectUnsupportedDebugCommand, "SetCaptureButtonAutoPilotState"},
        {132, &IHidDebugServer::RejectUnsupportedDebugCommand, "UnsetCaptureButtonAutoPilotState"},
        {133, &IHidDebugServer::RejectUnsupportedDebugCommand, "SetShiftAccelerometerCalibrationValue"},
        {134, &IHidDebugServer::RejectUnsupportedDebugCommand, "GetShiftAccelerometerCalibrationValue"},
        {135, &IHidDebugServer::RejectUnsupportedDebugCommand, "SetShiftGyroscopeCalibrationValue"},
        {136, &IHidDebugServer::RejectUnsupportedDebugCommand, "GetShiftGyroscopeCalibrationValue"},
        {140, C<&IHidDebugServer::DeactivateConsoleSixAxisSensor>, "DeactivateConsoleSixAxisSensor"},
        {141, &IHidDebugServer::RejectUnsupportedDebugCommand, "GetConsoleSixAxisSensorSamplingFrequency"},
        {142, C<&IHidDebugServer::DeactivateSevenSixAxisSensor>, "DeactivateSevenSixAxisSensor"},
        {143, &IHidDebugServer::RejectUnsupportedDebugCommand, "GetConsoleSixAxisSensorCountStates"},
        {144, &IHidDebugServer::RejectUnsupportedDebugCommand, "GetAccelerometerFsr"},
        {145, &IHidDebugServer::RejectUnsupportedDebugCommand, "SetAccelerometerFsr"},
        {146, &IHidDebugServer::RejectUnsupportedDebugCommand, "GetAccelerometerOdr"},
        {147, &IHidDebugServer::RejectUnsupportedDebugCommand, "SetAccelerometerOdr"},
        {148, &IHidDebugServer::RejectUnsupportedDebugCommand, "GetGyroscopeFsr"},
        {149, &IHidDebugServer::RejectUnsupportedDebugCommand, "SetGyroscopeFsr"},
        {150, &IHidDebugServer::RejectUnsupportedDebugCommand, "GetGyroscopeOdr"},
        {151, &IHidDebugServer::RejectUnsupportedDebugCommand, "SetGyroscopeOdr"},
        {152, &IHidDebugServer::RejectUnsupportedDebugCommand, "GetWhoAmI"},
        {201, &IHidDebugServer::RejectUnsupportedDebugCommand, "ActivateFirmwareUpdate"},
        {202, &IHidDebugServer::RejectUnsupportedDebugCommand, "DeactivateFirmwareUpdate"},
        {203, &IHidDebugServer::RejectUnsupportedDebugCommand, "StartFirmwareUpdate"},
        {204, &IHidDebugServer::RejectUnsupportedDebugCommand, "GetFirmwareUpdateStage"},
        {205, &IHidDebugServer::RejectUnsupportedDebugCommand, "GetFirmwareVersion"},
        {206, &IHidDebugServer::RejectUnsupportedDebugCommand, "GetDestinationFirmwareVersion"},
        {207, &IHidDebugServer::RejectUnsupportedDebugCommand, "DiscardFirmwareInfoCacheForRevert"},
        {208, &IHidDebugServer::RejectUnsupportedDebugCommand, "StartFirmwareUpdateForRevert"},
        {209, &IHidDebugServer::RejectUnsupportedDebugCommand, "GetAvailableFirmwareVersionForRevert"},
        {210, &IHidDebugServer::RejectUnsupportedDebugCommand, "IsFirmwareUpdatingDevice"},
        {211, &IHidDebugServer::RejectUnsupportedDebugCommand, "StartFirmwareUpdateIndividual"},
        {212, &IHidDebugServer::RejectUnsupportedDebugCommand, "GetDetailFirmwareVersion"}, // 19.0.0+
        {215, &IHidDebugServer::RejectUnsupportedDebugCommand, "SetUsbFirmwareForceUpdateEnabled"},
        {216, &IHidDebugServer::RejectUnsupportedDebugCommand, "SetAllKuinaDevicesToFirmwareUpdateMode"},
        {221, &IHidDebugServer::RejectUnsupportedDebugCommand, "UpdateControllerColor"},
        {222, &IHidDebugServer::RejectUnsupportedDebugCommand, "ConnectUsbPadsAsync"},
        {223, &IHidDebugServer::RejectUnsupportedDebugCommand, "DisconnectUsbPadsAsync"},
        {224, &IHidDebugServer::RejectUnsupportedDebugCommand, "UpdateDesignInfo"},
        {225, &IHidDebugServer::RejectUnsupportedDebugCommand, "GetUniquePadDriverState"},
        {226, &IHidDebugServer::RejectUnsupportedDebugCommand, "GetSixAxisSensorDriverStates"},
        {227, &IHidDebugServer::RejectUnsupportedDebugCommand, "GetRxPacketHistory"},
        {228, &IHidDebugServer::RejectUnsupportedDebugCommand, "AcquireOperationEventHandle"},
        {229, &IHidDebugServer::RejectUnsupportedDebugCommand, "ReadSerialFlash"},
        {230, &IHidDebugServer::RejectUnsupportedDebugCommand, "WriteSerialFlash"},
        {231, &IHidDebugServer::RejectUnsupportedDebugCommand, "GetOperationResult"},
        {232, &IHidDebugServer::RejectUnsupportedDebugCommand, "EnableShipmentMode"},
        {233, &IHidDebugServer::RejectUnsupportedDebugCommand, "ClearPairingInfo"},
        {234, &IHidDebugServer::RejectUnsupportedDebugCommand, "GetUniquePadDeviceTypeSetInternal"},
        {235, &IHidDebugServer::RejectUnsupportedDebugCommand, "EnableAnalogStickPower"},
        {236, &IHidDebugServer::RejectUnsupportedDebugCommand, "RequestKuinaUartClockCal"},
        {237, &IHidDebugServer::RejectUnsupportedDebugCommand, "GetKuinaUartClockCal"},
        {238, &IHidDebugServer::RejectUnsupportedDebugCommand, "SetKuinaUartClockTrim"},
        {239, &IHidDebugServer::RejectUnsupportedDebugCommand, "KuinaLoopbackTest"},
        {240, &IHidDebugServer::RejectUnsupportedDebugCommand, "RequestBatteryVoltage"},
        {241, &IHidDebugServer::RejectUnsupportedDebugCommand, "GetBatteryVoltage"},
        {242, &IHidDebugServer::RejectUnsupportedDebugCommand, "GetUniquePadPowerInfo"},
        {243, &IHidDebugServer::RejectUnsupportedDebugCommand, "RebootUniquePad"},
        {244, &IHidDebugServer::RejectUnsupportedDebugCommand, "RequestKuinaFirmwareVersion"},
        {245, &IHidDebugServer::RejectUnsupportedDebugCommand, "GetKuinaFirmwareVersion"},
        {246, &IHidDebugServer::RejectUnsupportedDebugCommand, "GetVidPid"},
        {247, &IHidDebugServer::RejectUnsupportedDebugCommand, "GetAnalogStickCalibrationValue"},
        {248, &IHidDebugServer::RejectUnsupportedDebugCommand, "GetUniquePadIdsFull"},
        {249, &IHidDebugServer::RejectUnsupportedDebugCommand, "ConnectUniquePad"},
        {250, &IHidDebugServer::RejectUnsupportedDebugCommand, "IsVirtual"},
        {251, &IHidDebugServer::RejectUnsupportedDebugCommand, "GetAnalogStickModuleParam"},
        {253, &IHidDebugServer::RejectUnsupportedDebugCommand, "ClearStorageForShipment"}, //19.0.0+
        {261, &IHidDebugServer::RejectUnsupportedDebugCommand, "UpdateDesignInfo12"}, //21.0.0+
        {262, &IHidDebugServer::RejectUnsupportedDebugCommand, "GetUniquePadButtonCount"}, //21.0.0+
        {267, &IHidDebugServer::RejectUnsupportedDebugCommand, "SetAnalogStickCalibration"}, //21.0.0+
        {268, &IHidDebugServer::RejectUnsupportedDebugCommand, "ResetAnalogStickCalibration"}, //21.0.0+
        {301, &IHidDebugServer::RejectUnsupportedDebugCommand, "GetAbstractedPadHandles"},
        {302, &IHidDebugServer::RejectUnsupportedDebugCommand, "GetAbstractedPadState"},
        {303, &IHidDebugServer::RejectUnsupportedDebugCommand, "GetAbstractedPadsState"},
        {321, &IHidDebugServer::RejectUnsupportedDebugCommand, "SetAutoPilotVirtualPadState"},
        {322, &IHidDebugServer::RejectUnsupportedDebugCommand, "UnsetAutoPilotVirtualPadState"},
        {323, &IHidDebugServer::RejectUnsupportedDebugCommand, "UnsetAllAutoPilotVirtualPadState"},
        {324, &IHidDebugServer::RejectUnsupportedDebugCommand, "AttachHdlsWorkBuffer"},
        {325, &IHidDebugServer::RejectUnsupportedDebugCommand, "ReleaseHdlsWorkBuffer"},
        {326, &IHidDebugServer::RejectUnsupportedDebugCommand, "DumpHdlsNpadAssignmentState"},
        {327, &IHidDebugServer::RejectUnsupportedDebugCommand, "DumpHdlsStates"},
        {328, &IHidDebugServer::RejectUnsupportedDebugCommand, "ApplyHdlsNpadAssignmentState"},
        {329, &IHidDebugServer::RejectUnsupportedDebugCommand, "ApplyHdlsStateList"},
        {330, &IHidDebugServer::RejectUnsupportedDebugCommand, "AttachHdlsVirtualDevice"},
        {331, &IHidDebugServer::RejectUnsupportedDebugCommand, "DetachHdlsVirtualDevice"},
        {332, &IHidDebugServer::RejectUnsupportedDebugCommand, "SetHdlsState"},
        {350, &IHidDebugServer::RejectUnsupportedDebugCommand, "AddRegisteredDevice"},
        {351, &IHidDebugServer::RejectUnsupportedDebugCommand, "GetRegisteredDevicesCountDebug"}, //17.0.0-18.1.0
        {352, &IHidDebugServer::RejectUnsupportedDebugCommand, "DeleteRegisteredDevicesDebug"}, //17.0.0-18.1.0
        {400, &IHidDebugServer::RejectUnsupportedDebugCommand, "DisableExternalMcuOnNxDevice"},
        {401, &IHidDebugServer::RejectUnsupportedDebugCommand, "DisableRailDeviceFiltering"},
        {402, &IHidDebugServer::RejectUnsupportedDebugCommand, "EnableWiredPairing"},
        {403, &IHidDebugServer::RejectUnsupportedDebugCommand, "EnableShipmentModeAutoClear"},
        {404, &IHidDebugServer::RejectUnsupportedDebugCommand, "SetRailEnabled"},
        {500, &IHidDebugServer::RejectUnsupportedDebugCommand, "SetFactoryInt"},
        {501, &IHidDebugServer::RejectUnsupportedDebugCommand, "IsFactoryBootEnabled"},
        {550, &IHidDebugServer::RejectUnsupportedDebugCommand, "SetAnalogStickModelDataTemporarily"},
        {551, &IHidDebugServer::RejectUnsupportedDebugCommand, "GetAnalogStickModelData"},
        {552, &IHidDebugServer::RejectUnsupportedDebugCommand, "ResetAnalogStickModelData"},
        {600, &IHidDebugServer::RejectUnsupportedDebugCommand, "ConvertPadState"},
        {601, &IHidDebugServer::RejectUnsupportedDebugCommand, "IsButtonConfigSupported"}, //18.0.0+
        {602, &IHidDebugServer::RejectUnsupportedDebugCommand, "IsButtonConfigEmbeddedSupported"}, //18.0.0+
        {603, &IHidDebugServer::RejectUnsupportedDebugCommand, "DeleteButtonConfig"}, //18.0.0+
        {604, &IHidDebugServer::RejectUnsupportedDebugCommand, "DeleteButtonConfigEmbedded"}, //18.0.0+
        {605, &IHidDebugServer::RejectUnsupportedDebugCommand, "SetButtonConfigEnabled"}, //18.0.0+
        {606, &IHidDebugServer::RejectUnsupportedDebugCommand, "SetButtonConfigEmbeddedEnabled"}, //18.0.0+
        {607, &IHidDebugServer::RejectUnsupportedDebugCommand, "IsButtonConfigEnabled"}, //18.0.0+
        {608, &IHidDebugServer::RejectUnsupportedDebugCommand, "IsButtonConfigEmbeddedEnabled"}, //18.0.0+
        {609, &IHidDebugServer::RejectUnsupportedDebugCommand, "SetButtonConfigEmbedded"}, //18.0.0+
        {610, &IHidDebugServer::RejectUnsupportedDebugCommand, "SetButtonConfigFull"}, //18.0.0+
        {611, &IHidDebugServer::RejectUnsupportedDebugCommand, "SetButtonConfigLeft"}, //18.0.0+
        {612, &IHidDebugServer::RejectUnsupportedDebugCommand, "SetButtonConfigRight"}, //18.0.0+
        {613, &IHidDebugServer::RejectUnsupportedDebugCommand, "GetButtonConfigEmbedded"}, //18.0.0+
        {614, &IHidDebugServer::RejectUnsupportedDebugCommand, "GetButtonConfigFull"}, //18.0.0+
        {615, &IHidDebugServer::RejectUnsupportedDebugCommand, "GetButtonConfigLeft"}, //18.0.0+
        {616, &IHidDebugServer::RejectUnsupportedDebugCommand, "GetButtonConfigRight"}, //18.0.0+
        {650, &IHidDebugServer::RejectUnsupportedDebugCommand, "AddButtonPlayData"},
        {651, &IHidDebugServer::RejectUnsupportedDebugCommand, "StartButtonPlayData"},
        {652, &IHidDebugServer::RejectUnsupportedDebugCommand, "StopButtonPlayData"},
        {700, &IHidDebugServer::RejectUnsupportedDebugCommand, "GetRailAttachEventCount"}, //21.0.0+
        {2000, C<&IHidDebugServer::DeactivateDigitizer>, "DeactivateDigitizer"},
        {2001, &IHidDebugServer::RejectUnsupportedDebugCommand, "SetDigitizerAutoPilotState"},
        {2002, &IHidDebugServer::RejectUnsupportedDebugCommand, "UnsetDigitizerAutoPilotState"},
        {3000, C<&IHidDebugServer::ReloadFirmwareDebugSettings>, "ReloadFirmwareDebugSettings"},
    };
    // clang-format on

    RegisterHandlers(functions);
}

IHidDebugServer::~IHidDebugServer() = default;

void IHidDebugServer::RejectUnsupportedDebugCommand(HLERequestContext& ctx) {
    LOG_DEBUG(Service_HID, "Rejecting unsupported HID debug command {}", ctx.GetCommand());

    IPC::ResponseBuilder rb{ctx, 2};
    rb.Push(Kernel::ResultNotImplemented);
}

Result IHidDebugServer::DeactivateDebugPad() {
    LOG_INFO(Service_HID, "called");
    GetResourceManager()->GetDebugPad()->DeactivateController();
    R_SUCCEED();
}

Result IHidDebugServer::DeactivateMouse() {
    LOG_INFO(Service_HID, "called");
    GetResourceManager()->GetMouse()->DeactivateController();
    R_SUCCEED();
}

Result IHidDebugServer::DeactivateKeyboard() {
    LOG_INFO(Service_HID, "called");
    GetResourceManager()->GetKeyboard()->DeactivateController();
    R_SUCCEED();
}

Result IHidDebugServer::ClearNpadSystemCommonPolicy(ClientAppletResourceUserId aruid) {
    LOG_INFO(Service_HID, "called, applet_resource_user_id={}", aruid.pid);
    R_RETURN(GetResourceManager()->GetNpad()->ClearNpadSystemCommonPolicy(system.Kernel(), aruid.pid));
}

Result IHidDebugServer::ReloadFirmwareDebugSettings() {
    LOG_INFO(Service_HID, "called");
    firmware_settings->Reload();
    R_SUCCEED();
}

Result IHidDebugServer::DeactivateHomeButton() {
    LOG_INFO(Service_HID, "called");
    GetResourceManager()->GetHomeButton()->DeactivateController();
    R_SUCCEED();
}

Result IHidDebugServer::DeactivateSleepButton() {
    LOG_INFO(Service_HID, "called");
    GetResourceManager()->GetSleepButton()->DeactivateController();
    R_SUCCEED();
}

Result IHidDebugServer::DeactivateCaptureButton() {
    LOG_INFO(Service_HID, "called");
    GetResourceManager()->GetCaptureButton()->DeactivateController();
    R_SUCCEED();
}

Result IHidDebugServer::DeactivateConsoleSixAxisSensor() {
    LOG_INFO(Service_HID, "called");
    GetResourceManager()->GetConsoleSixAxis()->DeactivateController();
    R_SUCCEED();
}

Result IHidDebugServer::DeactivateSevenSixAxisSensor() {
    LOG_INFO(Service_HID, "called");
    GetResourceManager()->GetSevenSixAxis()->DeactivateController();
    R_SUCCEED();
}

Result IHidDebugServer::DeactivateDigitizer() {
    LOG_INFO(Service_HID, "called");
    GetResourceManager()->GetDigitizer()->DeactivateController();
    R_SUCCEED();
}

Result IHidDebugServer::DeactivateTouchScreen() {
    LOG_INFO(Service_HID, "called");

    if (!firmware_settings->IsDeviceManaged()) {
        R_RETURN(GetResourceManager()->GetTouchScreen()->Deactivate());
    }

    R_SUCCEED();
}

Result IHidDebugServer::SetTouchScreenAutoPilotState(
    InArray<TouchState, BufferAttr_HipcMapAlias> auto_pilot_buffer) {
    AutoPilotState auto_pilot{};

    auto_pilot.count =
        static_cast<u64>((std::min)(auto_pilot_buffer.size(), auto_pilot.state.size()));
    memcpy(auto_pilot.state.data(), auto_pilot_buffer.data(),
           auto_pilot.count * sizeof(TouchState));

    LOG_INFO(Service_HID, "called, auto_pilot_count={}", auto_pilot.count);

    R_RETURN(GetResourceManager()->GetTouchScreen()->SetTouchScreenAutoPilotState(auto_pilot));
}

Result IHidDebugServer::UnsetTouchScreenAutoPilotState() {
    LOG_INFO(Service_HID, "called");
    R_RETURN(GetResourceManager()->GetTouchScreen()->UnsetTouchScreenAutoPilotState());
}

Result IHidDebugServer::GetTouchScreenConfiguration(
    Out<Core::HID::TouchScreenConfigurationForNx> out_touchscreen_config,
    ClientAppletResourceUserId aruid) {
    LOG_INFO(Service_HID, "called, applet_resource_user_id={}", aruid.pid);

    R_TRY(GetResourceManager()->GetTouchScreen()->GetTouchScreenConfiguration(
        *out_touchscreen_config, aruid.pid));

    if (out_touchscreen_config->mode != Core::HID::TouchScreenModeForNx::Heat2 &&
        out_touchscreen_config->mode != Core::HID::TouchScreenModeForNx::Finger) {
        out_touchscreen_config->mode = Core::HID::TouchScreenModeForNx::UseSystemSetting;
    }

    R_SUCCEED();
}

Result IHidDebugServer::ProcessTouchScreenAutoTune() {
    LOG_INFO(Service_HID, "called");
    R_RETURN(GetResourceManager()->GetTouchScreen()->ProcessTouchScreenAutoTune());
}

Result IHidDebugServer::ForceStopTouchScreenManagement() {
    LOG_INFO(Service_HID, "called");

    if (!firmware_settings->IsDeviceManaged()) {
        R_SUCCEED();
    }

    auto touch_screen = GetResourceManager()->GetTouchScreen();
    auto gesture = GetResourceManager()->GetGesture();

    if (firmware_settings->IsTouchI2cManaged()) {
        bool is_touch_active{};
        bool is_gesture_active{};
        R_TRY(touch_screen->IsActive(is_touch_active));
        R_TRY(gesture->IsActive(is_gesture_active));

        if (is_touch_active) {
            R_TRY(touch_screen->Deactivate());
        }
        if (is_gesture_active) {
            R_TRY(gesture->Deactivate());
        }
    }

    R_SUCCEED();
}

Result IHidDebugServer::ForceRestartTouchScreenManagement(u32 basic_gesture_id,
                                                          ClientAppletResourceUserId aruid) {
    LOG_INFO(Service_HID, "called, basic_gesture_id={}, applet_resource_user_id={}",
             basic_gesture_id, aruid.pid);

    auto touch_screen = GetResourceManager()->GetTouchScreen();
    auto gesture = GetResourceManager()->GetGesture();

    if (firmware_settings->IsDeviceManaged() && firmware_settings->IsTouchI2cManaged()) {
        R_TRY(gesture->Activate());
        R_TRY(gesture->Activate(aruid.pid, basic_gesture_id));
        R_TRY(touch_screen->Activate());
        R_TRY(touch_screen->Activate(aruid.pid));
    }

    R_SUCCEED();
}

Result IHidDebugServer::IsTouchScreenManaged(Out<bool> out_is_managed) {
    LOG_INFO(Service_HID, "called");

    bool is_touch_active{};
    bool is_gesture_active{};
    R_TRY(GetResourceManager()->GetTouchScreen()->IsActive(is_touch_active));
    R_TRY(GetResourceManager()->GetGesture()->IsActive(is_gesture_active));

    *out_is_managed = is_touch_active || is_gesture_active;
    R_SUCCEED();
}

Result IHidDebugServer::DeactivateGesture() {
    LOG_INFO(Service_HID, "called");

    if (!firmware_settings->IsDeviceManaged()) {
        R_RETURN(GetResourceManager()->GetGesture()->Deactivate());
    }

    R_SUCCEED();
}

std::shared_ptr<ResourceManager> IHidDebugServer::GetResourceManager() {
    resource_manager->Initialize();
    return resource_manager;
}

} // namespace Service::HID
