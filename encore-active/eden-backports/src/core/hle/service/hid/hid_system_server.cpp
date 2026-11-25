// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

// SPDX-FileCopyrightText: Copyright 2023 yuzu Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#include "core/core.h"
#include "hid_core/hid_core.h"
#include "hid_core/hid_util.h"
#include "hid_core/frontend/emulated_controller.h"
#include "hid_core/frontend/emulated_devices.h"
#include "core/hle/service/hid/hid_system_server.h"
#include "core/hle/service/ipc_helpers.h"
#include "core/hle/service/set/settings_types.h"
#include "hid_core/hid_result.h"
#include "hid_core/resource_manager.h"
#include "hid_core/resources/hid_firmware_settings.h"
#include "hid_core/resources/npad/npad.h"
#include "hid_core/resources/npad/npad_types.h"
#include "hid_core/resources/npad/npad_vibration.h"
#include "hid_core/resources/palma/palma.h"
#include "hid_core/resources/touch_screen/touch_screen.h"

namespace Service::HID {

IHidSystemServer::IHidSystemServer(Core::System& system_, std::shared_ptr<ResourceManager> resource,
                                   std::shared_ptr<HidFirmwareSettings> settings)
    : ServiceFramework{system_, "hid:sys"}, service_context{system_, service_name},
      resource_manager{resource}, firmware_settings{settings} {
    // clang-format off
    static const FunctionInfo functions[] = {
        {31, &IHidSystemServer::SendKeyboardLockKeyEvent, "SendKeyboardLockKeyEvent"},
        {101, &IHidSystemServer::RejectUnsupportedSwitchHardware, "AcquireHomeButtonEventHandle"},
        {111, &IHidSystemServer::RejectUnsupportedSwitchHardware, "ActivateHomeButton"},
        {121, &IHidSystemServer::RejectUnsupportedSwitchHardware, "AcquireSleepButtonEventHandle"},
        {131, &IHidSystemServer::RejectUnsupportedSwitchHardware, "ActivateSleepButton"},
        {141, &IHidSystemServer::RejectUnsupportedSwitchHardware, "AcquireCaptureButtonEventHandle"},
        {151, &IHidSystemServer::RejectUnsupportedSwitchHardware, "ActivateCaptureButton"},
        {161, &IHidSystemServer::GetPlatformConfig, "GetPlatformConfig"},
        {210, &IHidSystemServer::RejectUnsupportedSwitchHardware, "AcquireNfcDeviceUpdateEventHandle"},
        {211, &IHidSystemServer::RejectUnsupportedSwitchHardware, "GetNpadsWithNfc"},
        {212, &IHidSystemServer::RejectUnsupportedSwitchHardware, "AcquireNfcActivateEventHandle"},
        {213, &IHidSystemServer::RejectUnsupportedSwitchHardware, "ActivateNfc"},
        {214, &IHidSystemServer::RejectUnsupportedSwitchHardware, "GetXcdHandleForNpadWithNfc"},
        {215, &IHidSystemServer::RejectUnsupportedSwitchHardware, "IsNfcActivated"},
        {216, &IHidSystemServer::RejectUnsupportedSwitchHardware, "GetAbstractedPadIdForNpadWithNfc"},
        {217, &IHidSystemServer::RejectUnsupportedSwitchHardware, "SetNfcEvent"},
        {218, &IHidSystemServer::RejectUnsupportedSwitchHardware, "GetNfcInfo"},
        {219, &IHidSystemServer::RejectUnsupportedSwitchHardware, "StartNfcDiscovery"},
        {220, &IHidSystemServer::RejectUnsupportedSwitchHardware, "StopNfcDiscovery"},
        {221, &IHidSystemServer::RejectUnsupportedSwitchHardware, "StartNtagRead"},
        {222, &IHidSystemServer::RejectUnsupportedSwitchHardware, "StartNtagWrite"},
        {223, &IHidSystemServer::RejectUnsupportedSwitchHardware, "SendNfcRawData"},
        {224, &IHidSystemServer::RejectUnsupportedSwitchHardware, "RegisterMifareKey"},
        {225, &IHidSystemServer::RejectUnsupportedSwitchHardware, "ClearMifareKey"},
        {226, &IHidSystemServer::RejectUnsupportedSwitchHardware, "StartMifareRead"},
        {227, &IHidSystemServer::RejectUnsupportedSwitchHardware, "StartMifareWrite"},
        {230, &IHidSystemServer::RejectUnsupportedSwitchHardware, "AcquireIrSensorEventHandle" },
        {231, &IHidSystemServer::RejectUnsupportedSwitchHardware, "ActivateIrSensor"},
        {232, &IHidSystemServer::RejectUnsupportedSwitchHardware, "GetIrSensorState"},
        {233, &IHidSystemServer::RejectUnsupportedSwitchHardware, "GetXcdHandleForNpadWihIrSensor" },
        {234, &IHidSystemServer::RejectUnsupportedSwitchHardware, "GetNpadJoyHoldType"},
        {241, &IHidSystemServer::RejectUnsupportedSwitchHardware, "GetDataFormat"},
        {242, &IHidSystemServer::RejectUnsupportedSwitchHardware, "SetDataFormat"},
        {243, &IHidSystemServer::RejectUnsupportedSwitchHardware, "GetMcuState"},
        {244, &IHidSystemServer::RejectUnsupportedSwitchHardware, "SetMcuState"},
        {245, &IHidSystemServer::RejectUnsupportedSwitchHardware, "GetMcuVersionForNfc"},
        {246, &IHidSystemServer::RejectUnsupportedSwitchHardware, "CheckNfcDevicePower"},
        {247, &IHidSystemServer::RejectUnsupportedSwitchHardware, "SetMcuStateImmediate"},
        {301, &IHidSystemServer::RejectUnsupportedSwitchHardware, "ActivateNpadSystem"},
        {303, &IHidSystemServer::ApplyNpadSystemCommonPolicy, "ApplyNpadSystemCommonPolicy"},
        {304, &IHidSystemServer::EnableAssigningSingleOnSlSrPress, "EnableAssigningSingleOnSlSrPress"},
        {305, &IHidSystemServer::DisableAssigningSingleOnSlSrPress, "DisableAssigningSingleOnSlSrPress"},
        {306, &IHidSystemServer::GetLastActiveNpad, "GetLastActiveNpad"},
        {307, &IHidSystemServer::RejectUnsupportedSwitchHardware, "GetNpadSystemExtStyle"},
        {308, &IHidSystemServer::ApplyNpadSystemCommonPolicyFull, "ApplyNpadSystemCommonPolicyFull"},
        {309, &IHidSystemServer::GetNpadFullKeyGripColor, "GetNpadFullKeyGripColor"},
        {310, &IHidSystemServer::GetMaskedSupportedNpadStyleSet, "GetMaskedSupportedNpadStyleSet"},
        {311, &IHidSystemServer::RejectUnsupportedSwitchHardware, "SetNpadPlayerLedBlinkingDevice"},
        {312, &IHidSystemServer::SetSupportedNpadStyleSetAll, "SetSupportedNpadStyleSetAll"},
        {313, &IHidSystemServer::GetNpadCaptureButtonAssignment, "GetNpadCaptureButtonAssignment"},
        {314, &IHidSystemServer::RejectUnsupportedSwitchHardware, "GetAppletFooterUiType"},
        {315, &IHidSystemServer::GetAppletDetailedUiType, "GetAppletDetailedUiType"},
        {316, &IHidSystemServer::GetNpadInterfaceType, "GetNpadInterfaceType"},
        {317, &IHidSystemServer::GetNpadLeftRightInterfaceType, "GetNpadLeftRightInterfaceType"},
        {318, &IHidSystemServer::HasBattery, "HasBattery"},
        {319, &IHidSystemServer::HasLeftRightBattery, "HasLeftRightBattery"},
        {321, &IHidSystemServer::GetUniquePadsFromNpad, "GetUniquePadsFromNpad"},
        {322, &IHidSystemServer::SetNpadSystemExtStateEnabled, "SetNpadSystemExtStateEnabled"},
        {323, &IHidSystemServer::RejectUnsupportedSwitchHardware, "GetLastActiveUniquePad"},
        {324, &IHidSystemServer::RejectUnsupportedSwitchHardware, "GetUniquePadButtonSet"},
        {325, &IHidSystemServer::RejectUnsupportedSwitchHardware, "GetUniquePadColor"},
        {326, &IHidSystemServer::RejectUnsupportedSwitchHardware, "GetUniquePadAppletDetailedUiType"},
        {327, &IHidSystemServer::RejectUnsupportedSwitchHardware, "GetAbstractedPadIdDataFromNpad"},
        {328, &IHidSystemServer::RejectUnsupportedSwitchHardware, "AttachAbstractedPadToNpad"},
        {329, &IHidSystemServer::RejectUnsupportedSwitchHardware, "DetachAbstractedPadAll"},
        {330, &IHidSystemServer::RejectUnsupportedSwitchHardware, "CheckAbstractedPadConnection"},
        {332, &IHidSystemServer::RejectUnsupportedSwitchHardware, "ConvertAppletDetailedUiTypeFromPlayReportType"}, //19.0.0+
        {333, &IHidSystemServer::RejectUnsupportedSwitchHardware, "SetNpadUserSpgApplet"}, //20.0.0+
        {334, &IHidSystemServer::RejectUnsupportedSwitchHardware, "AcquireUniquePadButtonStateChangedEventHandle"}, //20.0.0+
        {501, &IHidSystemServer::RegisterAppletResourceUserId, "RegisterAppletResourceUserId"},
        {502, &IHidSystemServer::UnregisterAppletResourceUserId, "UnregisterAppletResourceUserId"},
        {503, &IHidSystemServer::EnableAppletToGetInput, "EnableAppletToGetInput"},
        {504, &IHidSystemServer::SetAruidValidForVibration, "SetAruidValidForVibration"},
        {505, &IHidSystemServer::EnableAppletToGetSixAxisSensor, "EnableAppletToGetSixAxisSensor"},
        {506, &IHidSystemServer::EnableAppletToGetPadInput, "EnableAppletToGetPadInput"},
        {507, &IHidSystemServer::EnableAppletToGetTouchScreen, "EnableAppletToGetTouchScreen"},
        {510, &IHidSystemServer::SetVibrationMasterVolume, "SetVibrationMasterVolume"},
        {511, &IHidSystemServer::GetVibrationMasterVolume, "GetVibrationMasterVolume"},
        {512, &IHidSystemServer::BeginPermitVibrationSession, "BeginPermitVibrationSession"},
        {513, &IHidSystemServer::EndPermitVibrationSession, "EndPermitVibrationSession"},
        {514, &IHidSystemServer::RejectUnsupportedSwitchHardware, "Unknown514"},
        {520, &IHidSystemServer::RejectUnsupportedSwitchHardware, "EnableHandheldHids"},
        {521, &IHidSystemServer::RejectUnsupportedSwitchHardware, "DisableHandheldHids"},
        {522, &IHidSystemServer::RejectUnsupportedSwitchHardware, "SetJoyConRailEnabled"},
        {523, &IHidSystemServer::IsJoyConRailEnabled, "IsJoyConRailEnabled"},
        {524, &IHidSystemServer::RejectUnsupportedSwitchHardware, "IsHandheldHidsEnabled"},
        {525, &IHidSystemServer::IsJoyConAttachedOnAllRail, "IsJoyConAttachedOnAllRail"},
        {540, &IHidSystemServer::RejectUnsupportedSwitchHardware, "AcquirePlayReportControllerUsageUpdateEvent"},
        {541, &IHidSystemServer::RejectUnsupportedSwitchHardware, "GetPlayReportControllerUsages"},
        {542, &IHidSystemServer::RejectUnsupportedSwitchHardware, "AcquirePlayReportRegisteredDeviceUpdateEvent"},
        {543, &IHidSystemServer::RejectUnsupportedSwitchHardware, "GetRegisteredDevicesOld"},
        {544, &IHidSystemServer::AcquireConnectionTriggerTimeoutEvent, "AcquireConnectionTriggerTimeoutEvent"},
        {545, &IHidSystemServer::RejectUnsupportedSwitchHardware, "SendConnectionTrigger"},
        {546, &IHidSystemServer::AcquireDeviceRegisteredEventForControllerSupport, "AcquireDeviceRegisteredEventForControllerSupport"},
        {547, &IHidSystemServer::RejectUnsupportedSwitchHardware, "GetAllowedBluetoothLinksCount"},
        {548, &IHidSystemServer::GetRegisteredDevices, "GetRegisteredDevices"},
        {549, &IHidSystemServer::RejectUnsupportedSwitchHardware, "GetConnectableRegisteredDevices"},
        {551, &IHidSystemServer::GetRegisteredDevices, "GetRegisteredDevicesForControllerSupport"}, //20.0.0+ //mocked via 548 for Diablo 3 (at least)
        {700, &IHidSystemServer::RejectUnsupportedSwitchHardware, "ActivateUniquePad"},
        {702, &IHidSystemServer::AcquireUniquePadConnectionEventHandle, "AcquireUniquePadConnectionEventHandle"},
        {703, &IHidSystemServer::GetUniquePadIds, "GetUniquePadIds"},
        {711, &IHidSystemServer::RejectUnsupportedSwitchHardware, "AcquireUniquePadConnectionOnHandheldForNsEventHandle"}, //20.0.0+
        {712, &IHidSystemServer::RejectUnsupportedSwitchHardware, "GetUniquePadColor12"}, //20.0.0+
        {751, &IHidSystemServer::AcquireJoyDetachOnBluetoothOffEventHandle, "AcquireJoyDetachOnBluetoothOffEventHandle"},
        {800, &IHidSystemServer::RejectUnsupportedSwitchHardware, "ListSixAxisSensorHandles"},
        {801, &IHidSystemServer::RejectUnsupportedSwitchHardware, "IsSixAxisSensorUserCalibrationSupported"},
        {802, &IHidSystemServer::RejectUnsupportedSwitchHardware, "ResetSixAxisSensorCalibrationValues"},
        {803, &IHidSystemServer::RejectUnsupportedSwitchHardware, "StartSixAxisSensorUserCalibration"},
        {804, &IHidSystemServer::RejectUnsupportedSwitchHardware, "CancelSixAxisSensorUserCalibration"},
        {805, &IHidSystemServer::RejectUnsupportedSwitchHardware, "GetUniquePadBluetoothAddress"},
        {806, &IHidSystemServer::RejectUnsupportedSwitchHardware, "DisconnectUniquePad"},
        {807, &IHidSystemServer::RejectUnsupportedSwitchHardware, "GetUniquePadType"},
        {808, &IHidSystemServer::RejectUnsupportedSwitchHardware, "GetUniquePadInterface"},
        {809, &IHidSystemServer::RejectUnsupportedSwitchHardware, "GetUniquePadSerialNumber"},
        {810, &IHidSystemServer::RejectUnsupportedSwitchHardware, "GetUniquePadControllerNumber"},
        {811, &IHidSystemServer::RejectUnsupportedSwitchHardware, "GetSixAxisSensorUserCalibrationStage"},
        {812, &IHidSystemServer::RejectUnsupportedSwitchHardware, "GetConsoleUniqueSixAxisSensorHandle"},
        {813, &IHidSystemServer::RejectUnsupportedSwitchHardware, "GetDeviceType"},
        {821, &IHidSystemServer::RejectUnsupportedSwitchHardware, "StartAnalogStickManualCalibration"},
        {822, &IHidSystemServer::RejectUnsupportedSwitchHardware, "RetryCurrentAnalogStickManualCalibrationStage"},
        {823, &IHidSystemServer::RejectUnsupportedSwitchHardware, "CancelAnalogStickManualCalibration"},
        {824, &IHidSystemServer::RejectUnsupportedSwitchHardware, "ResetAnalogStickManualCalibration"},
        {825, &IHidSystemServer::RejectUnsupportedSwitchHardware, "GetAnalogStickState"},
        {826, &IHidSystemServer::RejectUnsupportedSwitchHardware, "GetAnalogStickManualCalibrationStage"},
        {827, &IHidSystemServer::RejectUnsupportedSwitchHardware, "IsAnalogStickButtonPressed"},
        {828, &IHidSystemServer::RejectUnsupportedSwitchHardware, "IsAnalogStickInReleasePosition"},
        {829, &IHidSystemServer::RejectUnsupportedSwitchHardware, "IsAnalogStickInCircumference"},
        {830, &IHidSystemServer::RejectUnsupportedSwitchHardware, "SetNotificationLedPattern"},
        {831, &IHidSystemServer::RejectUnsupportedSwitchHardware, "SetNotificationLedPatternWithTimeout"},
        {832, &IHidSystemServer::RejectUnsupportedSwitchHardware, "PrepareHidsForNotificationWake"},
        {850, &IHidSystemServer::IsUsbFullKeyControllerEnabled, "IsUsbFullKeyControllerEnabled"},
        {851, &IHidSystemServer::EnableUsbFullKeyController, "EnableUsbFullKeyController"},
        {852, &IHidSystemServer::RejectUnsupportedSwitchHardware, "IsUsbConnected"},
        {870, &IHidSystemServer::IsHandheldButtonPressedOnConsoleMode, "IsHandheldButtonPressedOnConsoleMode"},
        {900, &IHidSystemServer::RejectUnsupportedSwitchHardware, "ActivateInputDetector"},
        {901, &IHidSystemServer::RejectUnsupportedSwitchHardware, "NotifyInputDetector"},
        {1000, &IHidSystemServer::InitializeFirmwareUpdate, "InitializeFirmwareUpdate"},
        {1001, &IHidSystemServer::RejectUnsupportedSwitchHardware, "GetFirmwareVersion"},
        {1002, &IHidSystemServer::RejectUnsupportedSwitchHardware, "GetAvailableFirmwareVersion"},
        {1003, &IHidSystemServer::RejectUnsupportedSwitchHardware, "IsFirmwareUpdateAvailable"},
        {1004, &IHidSystemServer::CheckFirmwareUpdateRequired, "CheckFirmwareUpdateRequired"},
        {1005, &IHidSystemServer::RejectUnsupportedSwitchHardware, "StartFirmwareUpdate"},
        {1006, &IHidSystemServer::RejectUnsupportedSwitchHardware, "AbortFirmwareUpdate"},
        {1007, &IHidSystemServer::RejectUnsupportedSwitchHardware, "GetFirmwareUpdateState"},
        {1008, &IHidSystemServer::RejectUnsupportedSwitchHardware, "ActivateAudioControl"},
        {1009, &IHidSystemServer::RejectUnsupportedSwitchHardware, "AcquireAudioControlEventHandle"},
        {1010, &IHidSystemServer::RejectUnsupportedSwitchHardware, "GetAudioControlStates"},
        {1011, &IHidSystemServer::RejectUnsupportedSwitchHardware, "DeactivateAudioControl"},
        {1012, &IHidSystemServer::RejectUnsupportedSwitchHardware, "GetFirmwareVersionStringForUserSupportPage"},
        {1050, &IHidSystemServer::RejectUnsupportedSwitchHardware, "IsSixAxisSensorAccurateUserCalibrationSupported"},
        {1051, &IHidSystemServer::RejectUnsupportedSwitchHardware, "StartSixAxisSensorAccurateUserCalibration"},
        {1052, &IHidSystemServer::RejectUnsupportedSwitchHardware, "CancelSixAxisSensorAccurateUserCalibration"},
        {1053, &IHidSystemServer::RejectUnsupportedSwitchHardware, "GetSixAxisSensorAccurateUserCalibrationState"},
        {1100, &IHidSystemServer::RejectUnsupportedSwitchHardware, "GetHidbusSystemServiceObject"},
        {1120, &IHidSystemServer::SetFirmwareHotfixUpdateSkipEnabled, "SetFirmwareHotfixUpdateSkipEnabled"},
        {1130, &IHidSystemServer::InitializeUsbFirmwareUpdate, "InitializeUsbFirmwareUpdate"},
        {1131, &IHidSystemServer::FinalizeUsbFirmwareUpdate, "FinalizeUsbFirmwareUpdate"},
        {1132, &IHidSystemServer::CheckUsbFirmwareUpdateRequired, "CheckUsbFirmwareUpdateRequired"},
        {1133, &IHidSystemServer::RejectUnsupportedSwitchHardware, "StartUsbFirmwareUpdate"},
        {1134, &IHidSystemServer::RejectUnsupportedSwitchHardware, "GetUsbFirmwareUpdateState"},
        {1135, &IHidSystemServer::InitializeUsbFirmwareUpdateWithoutMemory, "InitializeUsbFirmwareUpdateWithoutMemory"},
        {1150, &IHidSystemServer::SetTouchScreenMagnification, "SetTouchScreenMagnification"},
        {1151, &IHidSystemServer::GetTouchScreenFirmwareVersion, "GetTouchScreenFirmwareVersion"},
        {1152, &IHidSystemServer::SetTouchScreenDefaultConfiguration, "SetTouchScreenDefaultConfiguration"},
        {1153, &IHidSystemServer::GetTouchScreenDefaultConfiguration, "GetTouchScreenDefaultConfiguration"},
        {1154, &IHidSystemServer::RejectUnsupportedSwitchHardware, "IsFirmwareAvailableForNotification"},
        {1155, &IHidSystemServer::SetForceHandheldStyleVibration, "SetForceHandheldStyleVibration"},
        {1156, &IHidSystemServer::RejectUnsupportedSwitchHardware, "SendConnectionTriggerWithoutTimeoutEvent"},
        {1157, &IHidSystemServer::RejectUnsupportedSwitchHardware, "CancelConnectionTrigger"},
        {1158, &IHidSystemServer::RejectUnsupportedSwitchHardware, "SetConnectionLimitForSplay"}, //20.1.0+
        {1159, &IHidSystemServer::RejectUnsupportedSwitchHardware, "ClearConnectionLimitForSplay"}, //20.1.0+
        {1200, &IHidSystemServer::RejectUnsupportedSwitchHardware, "IsButtonConfigSupported"},
        {1201, &IHidSystemServer::RejectUnsupportedSwitchHardware, "IsButtonConfigEmbeddedSupported"},
        {1202, &IHidSystemServer::RejectUnsupportedSwitchHardware, "DeleteButtonConfig"},
        {1203, &IHidSystemServer::RejectUnsupportedSwitchHardware, "DeleteButtonConfigEmbedded"},
        {1204, &IHidSystemServer::RejectUnsupportedSwitchHardware, "SetButtonConfigEnabled"},
        {1205, &IHidSystemServer::RejectUnsupportedSwitchHardware, "SetButtonConfigEmbeddedEnabled"},
        {1206, &IHidSystemServer::RejectUnsupportedSwitchHardware, "IsButtonConfigEnabled"},
        {1207, &IHidSystemServer::RejectUnsupportedSwitchHardware, "IsButtonConfigEmbeddedEnabled"},
        {1208, &IHidSystemServer::RejectUnsupportedSwitchHardware, "SetButtonConfigEmbedded"},
        {1209, &IHidSystemServer::RejectUnsupportedSwitchHardware, "SetButtonConfigFull"},
        {1210, &IHidSystemServer::RejectUnsupportedSwitchHardware, "SetButtonConfigLeft"},
        {1211, &IHidSystemServer::RejectUnsupportedSwitchHardware, "SetButtonConfigRight"},
        {1212, &IHidSystemServer::RejectUnsupportedSwitchHardware, "GetButtonConfigEmbedded"},
        {1213, &IHidSystemServer::RejectUnsupportedSwitchHardware, "GetButtonConfigFull"},
        {1214, &IHidSystemServer::RejectUnsupportedSwitchHardware, "GetButtonConfigLeft"},
        {1215, &IHidSystemServer::RejectUnsupportedSwitchHardware, "GetButtonConfigRight"},
        {1250, &IHidSystemServer::RejectUnsupportedSwitchHardware, "IsCustomButtonConfigSupported"},
        {1251, &IHidSystemServer::RejectUnsupportedSwitchHardware, "IsDefaultButtonConfigEmbedded"},
        {1252, &IHidSystemServer::RejectUnsupportedSwitchHardware, "IsDefaultButtonConfigFull"},
        {1253, &IHidSystemServer::RejectUnsupportedSwitchHardware, "IsDefaultButtonConfigLeft"},
        {1254, &IHidSystemServer::RejectUnsupportedSwitchHardware, "IsDefaultButtonConfigRight"},
        {1255, &IHidSystemServer::RejectUnsupportedSwitchHardware, "IsButtonConfigStorageEmbeddedEmpty"},
        {1256, &IHidSystemServer::RejectUnsupportedSwitchHardware, "IsButtonConfigStorageFullEmpty"},
        {1257, &IHidSystemServer::RejectUnsupportedSwitchHardware, "IsButtonConfigStorageLeftEmpty"},
        {1258, &IHidSystemServer::RejectUnsupportedSwitchHardware, "IsButtonConfigStorageRightEmpty"},
        {1259, &IHidSystemServer::RejectUnsupportedSwitchHardware, "GetButtonConfigStorageEmbeddedDeprecated"},
        {1260, &IHidSystemServer::RejectUnsupportedSwitchHardware, "GetButtonConfigStorageFullDeprecated"},
        {1261, &IHidSystemServer::RejectUnsupportedSwitchHardware, "GetButtonConfigStorageLeftDeprecated"},
        {1262, &IHidSystemServer::RejectUnsupportedSwitchHardware, "GetButtonConfigStorageRightDeprecated"},
        {1263, &IHidSystemServer::RejectUnsupportedSwitchHardware, "SetButtonConfigStorageEmbeddedDeprecated"},
        {1264, &IHidSystemServer::RejectUnsupportedSwitchHardware, "SetButtonConfigStorageFullDeprecated"},
        {1265, &IHidSystemServer::RejectUnsupportedSwitchHardware, "SetButtonConfigStorageLeftDeprecated"},
        {1266, &IHidSystemServer::RejectUnsupportedSwitchHardware, "SetButtonConfigStorageRightDeprecated"},
        {1267, &IHidSystemServer::RejectUnsupportedSwitchHardware, "DeleteButtonConfigStorageEmbedded"},
        {1268, &IHidSystemServer::RejectUnsupportedSwitchHardware, "DeleteButtonConfigStorageFull"},
        {1269, &IHidSystemServer::RejectUnsupportedSwitchHardware, "DeleteButtonConfigStorageLeft"},
        {1270, &IHidSystemServer::RejectUnsupportedSwitchHardware, "DeleteButtonConfigStorageRight"},
        {1271, &IHidSystemServer::IsUsingCustomButtonConfig, "IsUsingCustomButtonConfig"},
        {1272, &IHidSystemServer::IsAnyCustomButtonConfigEnabled, "IsAnyCustomButtonConfigEnabled"},
        {1273, &IHidSystemServer::RejectUnsupportedSwitchHardware, "SetAllCustomButtonConfigEnabled"},
        {1274, &IHidSystemServer::RejectUnsupportedSwitchHardware, "SetDefaultButtonConfig"},
        {1275, &IHidSystemServer::RejectUnsupportedSwitchHardware, "SetAllDefaultButtonConfig"},
        {1276, &IHidSystemServer::RejectUnsupportedSwitchHardware, "SetHidButtonConfigEmbedded"},
        {1277, &IHidSystemServer::RejectUnsupportedSwitchHardware, "SetHidButtonConfigFull"},
        {1278, &IHidSystemServer::RejectUnsupportedSwitchHardware, "SetHidButtonConfigLeft"},
        {1279, &IHidSystemServer::RejectUnsupportedSwitchHardware, "SetHidButtonConfigRight"},
        {1280, &IHidSystemServer::RejectUnsupportedSwitchHardware, "GetHidButtonConfigEmbedded"},
        {1281, &IHidSystemServer::RejectUnsupportedSwitchHardware, "GetHidButtonConfigFull"},
        {1282, &IHidSystemServer::RejectUnsupportedSwitchHardware, "GetHidButtonConfigLeft"},
        {1283, &IHidSystemServer::RejectUnsupportedSwitchHardware, "GetHidButtonConfigRight"},
        {1284, &IHidSystemServer::RejectUnsupportedSwitchHardware, "GetButtonConfigStorageEmbedded"},
        {1285, &IHidSystemServer::RejectUnsupportedSwitchHardware, "GetButtonConfigStorageFull"},
        {1286, &IHidSystemServer::RejectUnsupportedSwitchHardware, "GetButtonConfigStorageLeft"},
        {1287, &IHidSystemServer::RejectUnsupportedSwitchHardware, "GetButtonConfigStorageRight"},
        {1288, &IHidSystemServer::RejectUnsupportedSwitchHardware, "SetButtonConfigStorageEmbedded"},
        {1289, &IHidSystemServer::RejectUnsupportedSwitchHardware, "SetButtonConfigStorageFull"},
        {1290, &IHidSystemServer::RejectUnsupportedSwitchHardware, "DeleteButtonConfigStorageRight"},
        {1291, &IHidSystemServer::RejectUnsupportedSwitchHardware, "DeleteButtonConfigStorageRight"},
        {1308, &IHidSystemServer::RejectUnsupportedSwitchHardware, "SetButtonConfigVisible"}, //18.0.0+
        {1309, &IHidSystemServer::RejectUnsupportedSwitchHardware, "IsButtonConfigVisible"}, //18.0.0+
        {1320, &IHidSystemServer::RejectUnsupportedSwitchHardware, "WakeTouchScreenUp"}, //17.0.0+
        {1321, &IHidSystemServer::RejectUnsupportedSwitchHardware, "PutTouchScreenToSleep"}, //17.0.0+
        {1322, &IHidSystemServer::RejectUnsupportedSwitchHardware, "AcquireTouchScreenAsyncWakeCompletedEvent"}, //20.0.0+
        {1323, &IHidSystemServer::RejectUnsupportedSwitchHardware, "StartTouchScreenAutoTuneForSystemSettings"}, //21.0.0+
        {1324, &IHidSystemServer::RejectUnsupportedSwitchHardware, "AcquireTouchScreenAutoTuneCompletedEvent"}, //21.0.0+
        {1325, &IHidSystemServer::RejectUnsupportedSwitchHardware, "IsTouchScreenAutoTuneRequiredForRepairProviderReplacement"}, //21.0.0+
        {1326, &IHidSystemServer::RejectUnsupportedSwitchHardware, "SetTouchScreenOffset"}, //21.0.0+
        {1420, &IHidSystemServer::RejectUnsupportedSwitchHardware, "GetAppletResourceProperty"}, //19.0.0+
        {12010, &IHidSystemServer::RejectUnsupportedSwitchHardware, "SetButtonConfigLeft"} //11.0.0-17.0.1
    };
    // clang-format on

    RegisterHandlers(functions);

    joy_detach_event = service_context.CreateEvent("IHidSystemServer::JoyDetachEvent");
    acquire_device_registered_event =
        service_context.CreateEvent("IHidSystemServer::AcquireDeviceRegisteredEvent");
    acquire_connection_trigger_timeout_event =
        service_context.CreateEvent("IHidSystemServer::AcquireConnectionTriggerTimeoutEvent");
    unique_pad_connection_event =
        service_context.CreateEvent("IHidSystemServer::AcquireUniquePadConnectionEventHandle");
}

IHidSystemServer::~IHidSystemServer() {
    service_context.CloseEvent(joy_detach_event);
    service_context.CloseEvent(acquire_device_registered_event);
    service_context.CloseEvent(acquire_connection_trigger_timeout_event);
    service_context.CloseEvent(unique_pad_connection_event);
};


void IHidSystemServer::RejectUnsupportedSwitchHardware(HLERequestContext& ctx) {
    LOG_WARNING(Service_HID,
                "Rejected hid:sys Switch-specific hardware/maintenance command on PS5 runtime");
    IPC::ResponseBuilder rb{ctx, 2};
    rb.Push(ResultUnknown);
}

void IHidSystemServer::SendKeyboardLockKeyEvent(HLERequestContext& ctx) {
    IPC::RequestParser rp{ctx};
    const auto flags = rp.Pop<u32>();
    system.HIDCore().GetEmulatedDevices()->ApplyKeyboardLockKeyEvents(flags);

    IPC::ResponseBuilder rb{ctx, 2};
    rb.Push(ResultSuccess);
}

void IHidSystemServer::GetPlatformConfig(HLERequestContext& ctx) {
    const auto platform_config = firmware_settings->GetPlatformConfig();

    LOG_INFO(Service_HID, "called, platform_config={}", platform_config.raw);

    IPC::ResponseBuilder rb{ctx, 3};
    rb.Push(ResultSuccess);
    rb.PushRaw(platform_config);
}

void IHidSystemServer::ApplyNpadSystemCommonPolicy(HLERequestContext& ctx) {
    IPC::RequestParser rp{ctx};
    const auto applet_resource_user_id{rp.Pop<u64>()};

    LOG_INFO(Service_HID, "called, applet_resource_user_id={}", applet_resource_user_id);

    GetResourceManager()->GetNpad()->ApplyNpadSystemCommonPolicy(system.Kernel(), applet_resource_user_id);

    IPC::ResponseBuilder rb{ctx, 2};
    rb.Push(ResultSuccess);
}

void IHidSystemServer::EnableAssigningSingleOnSlSrPress(HLERequestContext& ctx) {
    IPC::RequestParser rp{ctx};
    const auto applet_resource_user_id{rp.Pop<u64>()};

    LOG_INFO(Service_HID, "called, applet_resource_user_id={}", applet_resource_user_id);

    GetResourceManager()->GetNpad()->AssigningSingleOnSlSrPress(applet_resource_user_id, true);

    IPC::ResponseBuilder rb{ctx, 2};
    rb.Push(ResultSuccess);
}

void IHidSystemServer::DisableAssigningSingleOnSlSrPress(HLERequestContext& ctx) {
    IPC::RequestParser rp{ctx};
    const auto applet_resource_user_id{rp.Pop<u64>()};

    LOG_INFO(Service_HID, "called, applet_resource_user_id={}", applet_resource_user_id);

    GetResourceManager()->GetNpad()->AssigningSingleOnSlSrPress(applet_resource_user_id, false);

    IPC::ResponseBuilder rb{ctx, 2};
    rb.Push(ResultSuccess);
}

void IHidSystemServer::GetLastActiveNpad(HLERequestContext& ctx) {
    Core::HID::NpadIdType npad_id{};
    const Result result = GetResourceManager()->GetNpad()->GetLastActiveNpad(npad_id);

    LOG_DEBUG(Service_HID, "called, npad_id={}", npad_id);

    IPC::ResponseBuilder rb{ctx, 3};
    rb.Push(result);
    rb.PushEnum(npad_id);
}

void IHidSystemServer::ApplyNpadSystemCommonPolicyFull(HLERequestContext& ctx) {
    IPC::RequestParser rp{ctx};
    const auto applet_resource_user_id{rp.Pop<u64>()};

    LOG_INFO(Service_HID, "called, applet_resource_user_id={}", applet_resource_user_id);

    GetResourceManager()->GetNpad()->ApplyNpadSystemCommonPolicyFull(system.Kernel(), applet_resource_user_id);

    IPC::ResponseBuilder rb{ctx, 2};
    rb.Push(ResultSuccess);
}

void IHidSystemServer::GetNpadFullKeyGripColor(HLERequestContext& ctx) {
    IPC::RequestParser rp{ctx};
    const auto npad_id_type{rp.PopEnum<Core::HID::NpadIdType>()};

    Core::HID::NpadColor main_color{};
    Core::HID::NpadColor sub_color{};
    Result result = ResultInvalidNpadId;

    if (IsNpadIdValid(npad_id_type)) {
        const auto* controller = system.HIDCore().GetEmulatedController(npad_id_type);
        if (controller != nullptr && controller->IsConnected() &&
            controller->GetNpadStyleIndex() == Core::HID::NpadStyleIndex::Fullkey) {
            const auto colors = controller->GetColors();
            main_color = colors.fullkey.body;
            sub_color = colors.fullkey.button;
            result = ResultSuccess;
        } else {
            result = ResultNpadIsNotProController;
        }
    }

    LOG_DEBUG(Service_HID, "called, npad_id_type={}, result={:#x}", npad_id_type,
              result.raw);

    IPC::ResponseBuilder rb{ctx, 4};
    rb.Push(result);
    rb.PushRaw(main_color);
    rb.PushRaw(sub_color);
}

void IHidSystemServer::GetMaskedSupportedNpadStyleSet(HLERequestContext& ctx) {
    IPC::RequestParser rp{ctx};
    const auto applet_resource_user_id{rp.Pop<u64>()};

    LOG_INFO(Service_HID, "called, applet_resource_user_id={}", applet_resource_user_id);

    Core::HID::NpadStyleSet supported_styleset{};
    const auto npad = GetResourceManager()->GetNpad();
    const Result result = npad->GetMaskedSupportedNpadStyleSet(system.Kernel(), applet_resource_user_id, supported_styleset);

    IPC::ResponseBuilder rb{ctx, 3};
    rb.Push(result);
    rb.PushEnum(supported_styleset);
}

void IHidSystemServer::SetSupportedNpadStyleSetAll(HLERequestContext& ctx) {
    IPC::RequestParser rp{ctx};
    const auto applet_resource_user_id{rp.Pop<u64>()};

    LOG_DEBUG(Service_HID, "called, applet_resource_user_id={}", applet_resource_user_id);

    const auto npad = GetResourceManager()->GetNpad();
    const auto result = npad->SetSupportedNpadStyleSet(system.Kernel(), applet_resource_user_id, Core::HID::NpadStyleSet::All);

    IPC::ResponseBuilder rb{ctx, 2};
    rb.Push(result);
}

void IHidSystemServer::GetNpadCaptureButtonAssignment(HLERequestContext& ctx) {
    IPC::RequestParser rp{ctx};
    const auto applet_resource_user_id{rp.Pop<u64>()};
    const auto capture_button_list_size{ctx.GetWriteBufferNumElements<Core::HID::NpadButton>()};

    LOG_DEBUG(Service_HID, "called, applet_resource_user_id={}", applet_resource_user_id);

    std::vector<Core::HID::NpadButton> capture_button_list(capture_button_list_size);
    const auto& npad = GetResourceManager()->GetNpad();
    const u64 list_size =
        npad->GetNpadCaptureButtonAssignment(capture_button_list, applet_resource_user_id);

    if (list_size != 0) {
        ctx.WriteBuffer(capture_button_list);
    }

    IPC::ResponseBuilder rb{ctx, 4};
    rb.Push(ResultSuccess);
    rb.Push(list_size);
}

void IHidSystemServer::GetAppletDetailedUiType(HLERequestContext& ctx) {
    IPC::RequestParser rp{ctx};
    const auto npad_id_type{rp.PopEnum<Core::HID::NpadIdType>()};

    LOG_DEBUG(Service_HID, "called, npad_id_type={}",
              npad_id_type); // Spams a lot when controller applet is running

    const AppletDetailedUiType detailed_ui_type =
        GetResourceManager()->GetNpad()->GetAppletDetailedUiType(npad_id_type);

    IPC::ResponseBuilder rb{ctx, 3};
    rb.Push(ResultSuccess);
    rb.PushRaw(detailed_ui_type);
}

void IHidSystemServer::GetNpadInterfaceType(HLERequestContext& ctx) {
    IPC::RequestParser rp{ctx};
    const auto npad_id_type{rp.PopEnum<Core::HID::NpadIdType>()};

    Core::HID::NpadInterfaceType interface_type{};
    const Result result =
        GetResourceManager()->GetNpad()->GetNpadInterfaceType(npad_id_type, interface_type);

    LOG_DEBUG(Service_HID, "called, npad_id_type={}, interface_type={}, result={:#x}",
              npad_id_type, interface_type, result.raw);

    IPC::ResponseBuilder rb{ctx, 3};
    rb.Push(result);
    rb.PushEnum(interface_type);
}

void IHidSystemServer::GetNpadLeftRightInterfaceType(HLERequestContext& ctx) {
    IPC::RequestParser rp{ctx};
    const auto npad_id_type{rp.PopEnum<Core::HID::NpadIdType>()};

    Core::HID::NpadInterfaceType left_interface_type{};
    Core::HID::NpadInterfaceType right_interface_type{};
    const Result result = GetResourceManager()->GetNpad()->GetNpadLeftRightInterfaceType(
        npad_id_type, left_interface_type, right_interface_type);

    LOG_DEBUG(Service_HID,
              "called, npad_id_type={}, left_interface_type={}, right_interface_type={}, result={:#x}",
              npad_id_type, left_interface_type, right_interface_type, result.raw);

    IPC::ResponseBuilder rb{ctx, 4};
    rb.Push(result);
    rb.PushEnum(left_interface_type);
    rb.PushEnum(right_interface_type);
}

void IHidSystemServer::HasBattery(HLERequestContext& ctx) {
    IPC::RequestParser rp{ctx};
    const auto npad_id_type{rp.PopEnum<Core::HID::NpadIdType>()};

    bool has_battery{};
    const Result result = GetResourceManager()->GetNpad()->HasBattery(npad_id_type, has_battery);

    LOG_DEBUG(Service_HID, "called, npad_id_type={}, has_battery={}, result={:#x}",
              npad_id_type, has_battery, result.raw);

    IPC::ResponseBuilder rb{ctx, 3};
    rb.Push(result);
    rb.Push(has_battery);
}

void IHidSystemServer::HasLeftRightBattery(HLERequestContext& ctx) {
    IPC::RequestParser rp{ctx};
    const auto npad_id_type{rp.PopEnum<Core::HID::NpadIdType>()};

    struct LeftRightBattery {
        bool left;
        bool right;
    };

    LeftRightBattery left_right_battery{};
    const Result result = GetResourceManager()->GetNpad()->HasLeftRightBattery(
        npad_id_type, left_right_battery.left, left_right_battery.right);

    LOG_DEBUG(Service_HID,
              "called, npad_id_type={}, left={}, right={}, result={:#x}",
              npad_id_type, left_right_battery.left, left_right_battery.right, result.raw);

    IPC::ResponseBuilder rb{ctx, 3};
    rb.Push(result);
    rb.PushRaw(left_right_battery);
}

void IHidSystemServer::GetUniquePadsFromNpad(HLERequestContext& ctx) {
    IPC::RequestParser rp{ctx};
    const auto npad_id_type{rp.PopEnum<Core::HID::NpadIdType>()};

    u64 unique_pad_id{};
    switch (npad_id_type) {
    case Core::HID::NpadIdType::Player1: unique_pad_id = 1; break;
    case Core::HID::NpadIdType::Player2: unique_pad_id = 2; break;
    case Core::HID::NpadIdType::Player3: unique_pad_id = 3; break;
    case Core::HID::NpadIdType::Player4: unique_pad_id = 4; break;
    case Core::HID::NpadIdType::Player5: unique_pad_id = 5; break;
    case Core::HID::NpadIdType::Player6: unique_pad_id = 6; break;
    case Core::HID::NpadIdType::Player7: unique_pad_id = 7; break;
    case Core::HID::NpadIdType::Player8: unique_pad_id = 8; break;
    case Core::HID::NpadIdType::Handheld: unique_pad_id = 9; break;
    default: break;
    }

    std::vector<Core::HID::UniquePadId> unique_pads;
    if (unique_pad_id != 0 && ctx.GetWriteBufferSize() >= sizeof(Core::HID::UniquePadId)) {
        const auto* controller = system.HIDCore().GetEmulatedController(npad_id_type);
        if (controller->IsConnected() &&
            controller->GetNpadStyleIndex() != Core::HID::NpadStyleIndex::None) {
            unique_pads.push_back({.id = unique_pad_id});
        }
    }

    if (!unique_pads.empty()) {
        ctx.WriteBuffer(unique_pads);
    }

    LOG_DEBUG(Service_HID, "called, npad_id_type={}, count={}", npad_id_type, unique_pads.size());

    IPC::ResponseBuilder rb{ctx, 4};
    rb.Push(ResultSuccess);
    rb.Push<u64>(unique_pads.size());
}

void IHidSystemServer::SetNpadSystemExtStateEnabled(HLERequestContext& ctx) {
    IPC::RequestParser rp{ctx};
    struct Parameters {
        bool is_enabled;
        INSERT_PADDING_BYTES_NOINIT(7);
        u64 applet_resource_user_id;
    };
    static_assert(sizeof(Parameters) == 0x10, "Parameters has incorrect size.");

    const auto parameters{rp.PopRaw<Parameters>()};

    LOG_INFO(Service_HID, "called, is_enabled={}, applet_resource_user_id={}",
             parameters.is_enabled, parameters.applet_resource_user_id);

    const auto result = GetResourceManager()->GetNpad()->SetNpadSystemExtStateEnabled(
        parameters.applet_resource_user_id, parameters.is_enabled);

    IPC::ResponseBuilder rb{ctx, 2};
    rb.Push(result);
}
void IHidSystemServer::RegisterAppletResourceUserId(HLERequestContext& ctx) {
    IPC::RequestParser rp{ctx};
    struct Parameters {
        bool enable_input;
        INSERT_PADDING_WORDS_NOINIT(1);
        u64 applet_resource_user_id;
    };
    static_assert(sizeof(Parameters) == 0x10, "Parameters has incorrect size.");

    const auto parameters{rp.PopRaw<Parameters>()};

    LOG_INFO(Service_HID, "called, enable_input={}, applet_resource_user_id={}",
             parameters.enable_input, parameters.applet_resource_user_id);

    Result result = GetResourceManager()->RegisterAppletResourceUserId(
        parameters.applet_resource_user_id, parameters.enable_input);

    IPC::ResponseBuilder rb{ctx, 2};
    rb.Push(result);
}

void IHidSystemServer::UnregisterAppletResourceUserId(HLERequestContext& ctx) {
    IPC::RequestParser rp{ctx};
    u64 applet_resource_user_id{rp.Pop<u64>()};

    LOG_INFO(Service_HID, "called, applet_resource_user_id={}", applet_resource_user_id);

    GetResourceManager()->UnregisterAppletResourceUserId(applet_resource_user_id);

    IPC::ResponseBuilder rb{ctx, 2};
    rb.Push(ResultSuccess);
}

void IHidSystemServer::EnableAppletToGetInput(HLERequestContext& ctx) {
    IPC::RequestParser rp{ctx};
    struct Parameters {
        bool is_enabled;
        INSERT_PADDING_WORDS_NOINIT(1);
        u64 applet_resource_user_id;
    };
    static_assert(sizeof(Parameters) == 0x10, "Parameters has incorrect size.");

    const auto parameters{rp.PopRaw<Parameters>()};

    LOG_INFO(Service_HID, "called, is_enabled={}, applet_resource_user_id={}",
             parameters.is_enabled, parameters.applet_resource_user_id);

    GetResourceManager()->EnableInput(parameters.applet_resource_user_id, parameters.is_enabled);
    GetResourceManager()->GetNpad()->EnableAppletToGetInput(parameters.applet_resource_user_id);

    IPC::ResponseBuilder rb{ctx, 2};
    rb.Push(ResultSuccess);
}

void IHidSystemServer::SetAruidValidForVibration(HLERequestContext& ctx) {
    IPC::RequestParser rp{ctx};
    struct Parameters {
        bool is_enabled;
        INSERT_PADDING_WORDS_NOINIT(1);
        u64 applet_resource_user_id;
    };
    static_assert(sizeof(Parameters) == 0x10, "Parameters has incorrect size.");

    const auto parameters{rp.PopRaw<Parameters>()};

    LOG_INFO(Service_HID, "called, is_enabled={}, applet_resource_user_id={}",
             parameters.is_enabled, parameters.applet_resource_user_id);

    GetResourceManager()->SetAruidValidForVibration(parameters.applet_resource_user_id,
                                                    parameters.is_enabled);

    IPC::ResponseBuilder rb{ctx, 2};
    rb.Push(ResultSuccess);
}

void IHidSystemServer::EnableAppletToGetSixAxisSensor(HLERequestContext& ctx) {
    IPC::RequestParser rp{ctx};
    struct Parameters {
        bool is_enabled;
        INSERT_PADDING_WORDS_NOINIT(1);
        u64 applet_resource_user_id;
    };
    static_assert(sizeof(Parameters) == 0x10, "Parameters has incorrect size.");

    const auto parameters{rp.PopRaw<Parameters>()};

    LOG_INFO(Service_HID, "called, is_enabled={}, applet_resource_user_id={}",
             parameters.is_enabled, parameters.applet_resource_user_id);

    GetResourceManager()->EnableTouchScreen(parameters.applet_resource_user_id,
                                            parameters.is_enabled);

    IPC::ResponseBuilder rb{ctx, 2};
    rb.Push(ResultSuccess);
}

void IHidSystemServer::EnableAppletToGetPadInput(HLERequestContext& ctx) {
    IPC::RequestParser rp{ctx};
    struct Parameters {
        bool is_enabled;
        INSERT_PADDING_WORDS_NOINIT(1);
        u64 applet_resource_user_id;
    };
    static_assert(sizeof(Parameters) == 0x10, "Parameters has incorrect size.");

    const auto parameters{rp.PopRaw<Parameters>()};

    LOG_INFO(Service_HID, "called, is_enabled={}, applet_resource_user_id={}",
             parameters.is_enabled, parameters.applet_resource_user_id);

    GetResourceManager()->EnablePadInput(parameters.applet_resource_user_id, parameters.is_enabled);
    GetResourceManager()->GetNpad()->EnableAppletToGetInput(parameters.applet_resource_user_id);

    IPC::ResponseBuilder rb{ctx, 2};
    rb.Push(ResultSuccess);
}

void IHidSystemServer::EnableAppletToGetTouchScreen(HLERequestContext& ctx) {
    IPC::RequestParser rp{ctx};
    struct Parameters {
        bool is_enabled;
        INSERT_PADDING_WORDS_NOINIT(1);
        u64 applet_resource_user_id;
    };
    static_assert(sizeof(Parameters) == 0x10, "Parameters has incorrect size.");

    const auto parameters{rp.PopRaw<Parameters>()};

    LOG_INFO(Service_HID, "called, is_enabled={}, applet_resource_user_id={}",
             parameters.is_enabled, parameters.applet_resource_user_id);

    GetResourceManager()->EnableTouchScreen(parameters.applet_resource_user_id,
                                            parameters.is_enabled);

    IPC::ResponseBuilder rb{ctx, 2};
    rb.Push(ResultSuccess);
}

void IHidSystemServer::SetVibrationMasterVolume(HLERequestContext& ctx) {
    IPC::RequestParser rp{ctx};
    const auto master_volume{rp.Pop<f32>()};

    LOG_INFO(Service_HID, "called, volume={}", master_volume);

    const auto result =
        GetResourceManager()->GetNpad()->GetVibrationHandler()->SetVibrationMasterVolume(
            master_volume);

    IPC::ResponseBuilder rb{ctx, 2};
    rb.Push(result);
}

void IHidSystemServer::GetVibrationMasterVolume(HLERequestContext& ctx) {
    f32 master_volume{};
    const auto result =
        GetResourceManager()->GetNpad()->GetVibrationHandler()->GetVibrationMasterVolume(
            master_volume);

    LOG_INFO(Service_HID, "called, volume={}", master_volume);

    IPC::ResponseBuilder rb{ctx, 3};
    rb.Push(result);
    rb.Push(master_volume);
}

void IHidSystemServer::BeginPermitVibrationSession(HLERequestContext& ctx) {
    IPC::RequestParser rp{ctx};
    const auto applet_resource_user_id{rp.Pop<u64>()};

    LOG_INFO(Service_HID, "called, applet_resource_user_id={}", applet_resource_user_id);

    const auto result =
        GetResourceManager()->GetNpad()->GetVibrationHandler()->BeginPermitVibrationSession(
            applet_resource_user_id);

    IPC::ResponseBuilder rb{ctx, 2};
    rb.Push(result);
}

void IHidSystemServer::EndPermitVibrationSession(HLERequestContext& ctx) {
    LOG_INFO(Service_HID, "called");

    const auto result =
        GetResourceManager()->GetNpad()->GetVibrationHandler()->EndPermitVibrationSession();

    IPC::ResponseBuilder rb{ctx, 2};
    rb.Push(result);
}

void IHidSystemServer::IsJoyConRailEnabled(HLERequestContext& ctx) {
    const bool is_enabled = firmware_settings->IsRailEnabled();

    LOG_DEBUG(Service_HID, "called, is_enabled={}", is_enabled);

    IPC::ResponseBuilder rb{ctx, 3};
    rb.Push(ResultSuccess);
    rb.Push(is_enabled);
}

void IHidSystemServer::IsJoyConAttachedOnAllRail(HLERequestContext& ctx) {
    const auto* handheld =
        system.HIDCore().GetEmulatedController(Core::HID::NpadIdType::Handheld);
    const bool is_attached =
        firmware_settings->IsRailEnabled() && handheld != nullptr && handheld->IsConnected() &&
        handheld->GetNpadStyleIndex() == Core::HID::NpadStyleIndex::Handheld;

    LOG_DEBUG(Service_HID, "called, is_attached={}", is_attached);

    IPC::ResponseBuilder rb{ctx, 3};
    rb.Push(ResultSuccess);
    rb.Push(is_attached);
}

void IHidSystemServer::AcquireConnectionTriggerTimeoutEvent(HLERequestContext& ctx) {
    LOG_INFO(Service_HID, "called");

    IPC::ResponseBuilder rb{ctx, 2, 1};
    rb.Push(ResultSuccess);
    rb.PushCopyObjects(ctx, acquire_connection_trigger_timeout_event->GetReadableEvent());
}

void IHidSystemServer::AcquireDeviceRegisteredEventForControllerSupport(HLERequestContext& ctx) {
    LOG_INFO(Service_HID, "called");

    IPC::ResponseBuilder rb{ctx, 2, 1};
    rb.Push(ResultSuccess);
    rb.PushCopyObjects(ctx, acquire_device_registered_event->GetReadableEvent());
}

void IHidSystemServer::GetRegisteredDevices(HLERequestContext& ctx) {
    // Encore does not expose a persistent Nintendo Bluetooth pairing database on PS5.
    // Connected emulated controllers are reported through the NPad/UniquePad paths instead.
    struct RegisterData {
        std::array<u8, 0x68> data;
    };
    static_assert(sizeof(RegisterData) == 0x68, "RegisterData is an invalid size");
    const std::vector<RegisterData> registered_devices{};

    LOG_DEBUG(Service_HID,
              "called, command={}; no persistent Switch registered-device database",
              ctx.GetCommand()); // 548 or 551

    IPC::ResponseBuilder rb{ctx, 4};
    rb.Push(ResultSuccess);
    rb.Push<u64>(registered_devices.size());
}

void IHidSystemServer::AcquireUniquePadConnectionEventHandle(HLERequestContext& ctx) {
    LOG_INFO(Service_HID, "called");

    IPC::ResponseBuilder rb{ctx, 2, 1};
    rb.PushCopyObjects(ctx, unique_pad_connection_event->GetReadableEvent());
    rb.Push(ResultSuccess);
}

void IHidSystemServer::GetUniquePadIds(HLERequestContext& ctx) {
    std::vector<Core::HID::UniquePadId> unique_pads;
    const auto capacity = ctx.GetWriteBufferSize() / sizeof(Core::HID::UniquePadId);

    for (std::size_t index = 0; index < 8 && unique_pads.size() < capacity; ++index) {
        const auto* controller = system.HIDCore().GetEmulatedControllerByIndex(index);
        if (controller->IsConnected() &&
            controller->GetNpadStyleIndex() != Core::HID::NpadStyleIndex::None) {
            unique_pads.push_back({.id = static_cast<u64>(index + 1)});
        }
    }

    if (unique_pads.size() < capacity) {
        const auto* handheld = system.HIDCore().GetEmulatedController(Core::HID::NpadIdType::Handheld);
        if (handheld->IsConnected() &&
            handheld->GetNpadStyleIndex() != Core::HID::NpadStyleIndex::None) {
            unique_pads.push_back({.id = 9});
        }
    }

    if (!unique_pads.empty()) {
        ctx.WriteBuffer(unique_pads);
    }

    LOG_DEBUG(Service_HID, "called, count={}", unique_pads.size());

    IPC::ResponseBuilder rb{ctx, 4};
    rb.Push(ResultSuccess);
    rb.Push<u64>(unique_pads.size());
}

void IHidSystemServer::AcquireJoyDetachOnBluetoothOffEventHandle(HLERequestContext& ctx) {
    LOG_INFO(Service_AM, "called");

    IPC::ResponseBuilder rb{ctx, 2, 1};
    rb.Push(ResultSuccess);
    rb.PushCopyObjects(ctx, joy_detach_event->GetReadableEvent());
}

void IHidSystemServer::IsUsbFullKeyControllerEnabled(HLERequestContext& ctx) {
    const bool is_enabled = false;

    LOG_DEBUG(Service_HID, "called, is_enabled={}; USB full-key backend is not modeled", is_enabled);

    IPC::ResponseBuilder rb{ctx, 3};
    rb.Push(ResultSuccess);
    rb.Push(is_enabled);
}

void IHidSystemServer::EnableUsbFullKeyController(HLERequestContext& ctx) {
    IPC::RequestParser rp{ctx};
    const auto is_enabled{rp.Pop<bool>()};

    LOG_DEBUG(Service_HID, "called, is_enabled={}; USB full-key backend is not modeled", is_enabled);

    IPC::ResponseBuilder rb{ctx, 2};
    rb.Push(ResultSuccess);
}

void IHidSystemServer::IsHandheldButtonPressedOnConsoleMode(HLERequestContext& ctx) {
    const bool button_pressed = false;

    LOG_DEBUG(Service_HID,
              "called, button_pressed={}; no physical handheld-console button is exposed",
              button_pressed);

    IPC::ResponseBuilder rb{ctx, 3};
    rb.Push(ResultSuccess);
    rb.Push(button_pressed);
}

void IHidSystemServer::InitializeFirmwareUpdate(HLERequestContext& ctx) {
    LOG_DEBUG(Service_HID, "called; controller firmware update is not modeled");

    IPC::ResponseBuilder rb{ctx, 2};
    rb.Push(ResultSuccess);
}

void IHidSystemServer::CheckFirmwareUpdateRequired(HLERequestContext& ctx) {
    LOG_DEBUG(Service_HID, "called; controller firmware update is not modeled");

    IPC::ResponseBuilder rb{ctx, 2};
    rb.Push(ResultSuccess);
}

void IHidSystemServer::SetFirmwareHotfixUpdateSkipEnabled(HLERequestContext& ctx) {
    LOG_DEBUG(Service_HID, "called; controller firmware update is not modeled");

    IPC::ResponseBuilder rb{ctx, 2};
    rb.Push(ResultSuccess);
}

void IHidSystemServer::InitializeUsbFirmwareUpdate(HLERequestContext& ctx) {
    LOG_DEBUG(Service_HID, "called; controller firmware update is not modeled");

    IPC::ResponseBuilder rb{ctx, 2};
    rb.Push(ResultSuccess);
}

void IHidSystemServer::FinalizeUsbFirmwareUpdate(HLERequestContext& ctx) {
    LOG_DEBUG(Service_HID, "called; controller firmware update is not modeled");

    IPC::ResponseBuilder rb{ctx, 2};
    rb.Push(ResultSuccess);
}

void IHidSystemServer::CheckUsbFirmwareUpdateRequired(HLERequestContext& ctx) {
    LOG_DEBUG(Service_HID, "called; controller firmware update is not modeled");

    IPC::ResponseBuilder rb{ctx, 2};
    rb.Push(ResultSuccess);
}

void IHidSystemServer::InitializeUsbFirmwareUpdateWithoutMemory(HLERequestContext& ctx) {
    // The PS5 host has no Nintendo-controller USB firmware-update backend. This is a
    // successful no-op paired with CheckUsbFirmwareUpdateRequired reporting no work.
    LOG_DEBUG(Service_HID, "called; Nintendo USB firmware-update backend is not present");

    IPC::ResponseBuilder rb{ctx, 2};
    rb.Push(ResultSuccess);
}

void IHidSystemServer::SetTouchScreenMagnification(HLERequestContext& ctx) {
    IPC::RequestParser rp{ctx};
    const auto point1x{rp.Pop<f32>()};
    const auto point1y{rp.Pop<f32>()};
    const auto point2x{rp.Pop<f32>()};
    const auto point2y{rp.Pop<f32>()};

    LOG_INFO(Service_HID, "called, point1=-({},{}), point2=({},{})", point1x, point1y, point2x,
             point2y);

    const Result result = GetResourceManager()->GetTouchScreen()->SetTouchScreenMagnification(
        point1x, point1y, point2x, point2y);

    IPC::ResponseBuilder rb{ctx, 2};
    rb.Push(result);
}

void IHidSystemServer::GetTouchScreenFirmwareVersion(HLERequestContext& ctx) {
    LOG_INFO(Service_HID, "called");

    Core::HID::FirmwareVersion firmware{};
    const auto result = GetResourceManager()->GetTouchScreenFirmwareVersion(firmware);

    IPC::ResponseBuilder rb{ctx, 6};
    rb.Push(result);
    rb.PushRaw(firmware);
}

void IHidSystemServer::SetTouchScreenDefaultConfiguration(HLERequestContext& ctx) {
    IPC::RequestParser rp{ctx};
    auto touchscreen_config{rp.PopRaw<Core::HID::TouchScreenConfigurationForNx>()};

    LOG_INFO(Service_HID, "called, touchscreen_config={}", touchscreen_config.mode);

    if (touchscreen_config.mode != Core::HID::TouchScreenModeForNx::Heat2 &&
        touchscreen_config.mode != Core::HID::TouchScreenModeForNx::Finger) {
        touchscreen_config.mode = Core::HID::TouchScreenModeForNx::UseSystemSetting;
    }

    const Result result =
        GetResourceManager()->GetTouchScreen()->SetTouchScreenDefaultConfiguration(
            touchscreen_config);

    IPC::ResponseBuilder rb{ctx, 2};
    rb.Push(result);
}

void IHidSystemServer::GetTouchScreenDefaultConfiguration(HLERequestContext& ctx) {
    LOG_INFO(Service_HID, "called");

    Core::HID::TouchScreenConfigurationForNx touchscreen_config{};
    const Result result =
        GetResourceManager()->GetTouchScreen()->GetTouchScreenDefaultConfiguration(
            touchscreen_config);

    if (touchscreen_config.mode != Core::HID::TouchScreenModeForNx::Heat2 &&
        touchscreen_config.mode != Core::HID::TouchScreenModeForNx::Finger) {
        touchscreen_config.mode = Core::HID::TouchScreenModeForNx::UseSystemSetting;
    }

    IPC::ResponseBuilder rb{ctx, 6};
    rb.Push(result);
    rb.PushRaw(touchscreen_config);
}

void IHidSystemServer::SetForceHandheldStyleVibration(HLERequestContext& ctx) {
    IPC::RequestParser rp{ctx};
    const auto is_forced{rp.Pop<bool>()};

    LOG_INFO(Service_HID, "called, is_forced={}", is_forced);

    GetResourceManager()->SetForceHandheldStyleVibration(is_forced);
    GetResourceManager()->GetNpad()->UpdateHandheldAbstractState();

    IPC::ResponseBuilder rb{ctx, 2};
    rb.Push(ResultSuccess);
}

void IHidSystemServer::IsUsingCustomButtonConfig(HLERequestContext& ctx) {
    // Guest-side Nintendo button-remap storage is not exposed by Encore; host input
    // mappings are an emulator concern and must not be reported as a guest custom config.
    constexpr bool is_enabled = false;

    LOG_DEBUG(Service_HID, "called, is_enabled={}", is_enabled);

    IPC::ResponseBuilder rb{ctx, 3};
    rb.Push(ResultSuccess);
    rb.Push(is_enabled);
}

void IHidSystemServer::IsAnyCustomButtonConfigEnabled(HLERequestContext& ctx) {
    // See IsUsingCustomButtonConfig: Encore has no guest Nintendo remap database.
    constexpr bool is_enabled = false;

    LOG_DEBUG(Service_HID, "called, is_enabled={}", is_enabled);

    IPC::ResponseBuilder rb{ctx, 3};
    rb.Push(ResultSuccess);
    rb.Push(is_enabled);
}

std::shared_ptr<ResourceManager> IHidSystemServer::GetResourceManager() {
    resource_manager->Initialize();
    return resource_manager;
}

} // namespace Service::HID
