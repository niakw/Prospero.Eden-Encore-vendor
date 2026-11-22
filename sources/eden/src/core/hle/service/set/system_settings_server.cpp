// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

// SPDX-FileCopyrightText: Copyright 2018 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <fstream>

#include "common/assert.h"
#include "common/fs/file.h"
#include "common/fs/fs.h"
#include "common/fs/path_util.h"
#include "common/logging.h"
#include "common/settings.h"
#include "common/string_util.h"
#include "core/core.h"
#include "core/file_sys/content_archive.h"
#include "core/file_sys/errors.h"
#include "core/file_sys/nca_metadata.h"
#include "core/file_sys/registered_cache.h"
#include "core/file_sys/romfs.h"
#include "core/file_sys/system_archive/system_archive.h"
#include "core/hle/service/cmif_serialization.h"
#include "core/hle/service/filesystem/filesystem.h"
#include "core/hle/service/ipc_helpers.h"
#include "core/hle/service/set/settings_server.h"
#include "core/hle/service/set/system_settings_server.h"

namespace Service::Set {

namespace {
constexpr u32 SETTINGS_VERSION{4u};
constexpr auto SETTINGS_MAGIC = Common::MakeMagic('y', 'u', 'z', 'u', '_', 's', 'e', 't');
struct SettingsHeader {
    u64 magic;
    u32 version;
    u32 reserved;
};

void SyncGlobalLanguageFromCode(LanguageCode language_code) {
    const auto it = std::find_if(available_language_codes.begin(), available_language_codes.end(),
                                 [language_code](LanguageCode code) { return code == language_code; });
    if (it == available_language_codes.end()) {
        return;
    }

    const std::size_t index = static_cast<std::size_t>(std::distance(available_language_codes.begin(), it));
    if (index >= static_cast<std::size_t>(Settings::values.language_index.GetValue())) {
        Settings::values.language_index.SetValue(static_cast<Settings::Language>(index));
    }
}

void SyncGlobalRegionFromCode(SystemRegionCode region_code) {
    const auto region_index = static_cast<std::size_t>(region_code);
    if (region_index > static_cast<std::size_t>(Settings::Region::Taiwan)) {
        return;
    }

    Settings::values.region_index.SetValue(static_cast<Settings::Region>(region_index));
}

} // Anonymous namespace

Result GetFirmwareVersionImpl(FirmwareVersionFormat& out_firmware, Core::System& system,
                              GetFirmwareVersionType type) {
    constexpr u64 FirmwareVersionSystemDataId = 0x0100000000000809;
    auto& fsc = system.GetFileSystemController();

    // Attempt to load version data from disk
    const FileSys::RegisteredCache* bis_system{};
    std::unique_ptr<FileSys::NCA> nca{};
    FileSys::VirtualDir romfs{};

    bis_system = fsc.GetSystemNANDContents();
    if (bis_system) {
        nca = bis_system->GetEntry(FirmwareVersionSystemDataId, FileSys::ContentRecordType::Data);
    }
    if (nca) {
        if (auto nca_romfs = nca->GetRomFS(); nca_romfs) {
            romfs = FileSys::ExtractRomFS(nca_romfs);
        }
    }
    if (!romfs) {
        romfs = FileSys::ExtractRomFS(
            FileSys::SystemArchive::SynthesizeSystemArchive(FirmwareVersionSystemDataId));
    }

    const auto early_exit_failure = [](std::string_view desc, Result code) {
        LOG_ERROR(Service_SET, "General failure while attempting to resolve firmware version ({}).",
                  desc);
        return code;
    };

    const auto ver_file = romfs->GetFile("file");
    if (ver_file == nullptr) {
        return early_exit_failure("The system version archive didn't contain the file 'file'.",
                                  FileSys::ResultInvalidArgument);
    }

    auto data = ver_file->ReadAllBytes();
    if (data.size() != sizeof(FirmwareVersionFormat)) {
        return early_exit_failure("The system version file 'file' was not the correct size.",
                                  FileSys::ResultOutOfRange);
    }

    std::memcpy(&out_firmware, data.data(), sizeof(FirmwareVersionFormat));

    // If the command is GetFirmwareVersion (as opposed to GetFirmwareVersion2), hardware will
    // zero out the REVISION_MINOR field.
    if (type == GetFirmwareVersionType::Version1) {
        out_firmware.revision_minor = 0;
    }

    return ResultSuccess;
}

ISystemSettingsServer::ISystemSettingsServer(Core::System& system_)
    : ServiceFramework{system_, "set:sys"}, m_system{system} {
    // clang-format off
    static const FunctionInfo functions[] = {
        {0, C<&ISystemSettingsServer::SetLanguageCode>, "SetLanguageCode"},
        {1, nullptr, "SetNetworkSettings"},
        {2, nullptr, "GetNetworkSettings"},
        {3, C<&ISystemSettingsServer::GetFirmwareVersion>, "GetFirmwareVersion"},
        {4, C<&ISystemSettingsServer::GetFirmwareVersion2>, "GetFirmwareVersion2"},
        {5, nullptr, "GetFirmwareVersionDigest"},
        {7, C<&ISystemSettingsServer::GetLockScreenFlag>, "GetLockScreenFlag"},
        {8, C<&ISystemSettingsServer::SetLockScreenFlag>, "SetLockScreenFlag"},
        {9, nullptr, "GetBacklightSettings"},
        {10, nullptr, "SetBacklightSettings"},
        {11, nullptr, "SetBluetoothDevicesSettings"},
        {12, nullptr, "GetBluetoothDevicesSettings"},
        {13, C<&ISystemSettingsServer::GetExternalSteadyClockSourceId>, "GetExternalSteadyClockSourceId"},
        {14, C<&ISystemSettingsServer::SetExternalSteadyClockSourceId>, "SetExternalSteadyClockSourceId"},
        {15, C<&ISystemSettingsServer::GetUserSystemClockContext>, "GetUserSystemClockContext"},
        {16, C<&ISystemSettingsServer::SetUserSystemClockContext>, "SetUserSystemClockContext"},
        {17, C<&ISystemSettingsServer::GetAccountSettings>, "GetAccountSettings"},
        {18, C<&ISystemSettingsServer::SetAccountSettings>, "SetAccountSettings"},
        {19, nullptr, "GetAudioVolume"},
        {20, nullptr, "SetAudioVolume"},
        {21, C<&ISystemSettingsServer::GetEulaVersions>, "GetEulaVersions"},
        {22, C<&ISystemSettingsServer::SetEulaVersions>, "SetEulaVersions"},
        {23, C<&ISystemSettingsServer::GetColorSetId>, "GetColorSetId"},
        {24, C<&ISystemSettingsServer::SetColorSetId>, "SetColorSetId"},
        {25, C<&ISystemSettingsServer::GetConsoleInformationUploadFlag>, "GetConsoleInformationUploadFlag"},
        {26, C<&ISystemSettingsServer::SetConsoleInformationUploadFlag>, "SetConsoleInformationUploadFlag"},
        {27, C<&ISystemSettingsServer::GetAutomaticApplicationDownloadFlag>, "GetAutomaticApplicationDownloadFlag"},
        {28, C<&ISystemSettingsServer::SetAutomaticApplicationDownloadFlag>, "SetAutomaticApplicationDownloadFlag"},
        {29, C<&ISystemSettingsServer::GetNotificationSettings>, "GetNotificationSettings"},
        {30, C<&ISystemSettingsServer::SetNotificationSettings>, "SetNotificationSettings"},
        {31, C<&ISystemSettingsServer::GetAccountNotificationSettings>, "GetAccountNotificationSettings"},
        {32, C<&ISystemSettingsServer::SetAccountNotificationSettings>, "SetAccountNotificationSettings"},
        {35, C<&ISystemSettingsServer::GetVibrationMasterVolume>, "GetVibrationMasterVolume"},
        {36, C<&ISystemSettingsServer::SetVibrationMasterVolume>, "SetVibrationMasterVolume"},
        {37, C<&ISystemSettingsServer::GetSettingsItemValueSize>, "GetSettingsItemValueSize"},
        {38, C<&ISystemSettingsServer::GetSettingsItemValue>, "GetSettingsItemValue"},
        {39, C<&ISystemSettingsServer::GetTvSettings>, "GetTvSettings"},
        {40, C<&ISystemSettingsServer::SetTvSettings>, "SetTvSettings"},
        {41, nullptr, "GetEdid"},
        {42, nullptr, "SetEdid"},
        {43, C<&ISystemSettingsServer::GetAudioOutputMode>, "GetAudioOutputMode"},
        {44, C<&ISystemSettingsServer::SetAudioOutputMode>, "SetAudioOutputMode"},
        {45, C<&ISystemSettingsServer::GetSpeakerAutoMuteFlag>, "GetSpeakerAutoMuteFlag"},
        {46, C<&ISystemSettingsServer::SetSpeakerAutoMuteFlag>, "SetSpeakerAutoMuteFlag"},
        {47, C<&ISystemSettingsServer::GetQuestFlag>, "GetQuestFlag"},
        {48, C<&ISystemSettingsServer::SetQuestFlag>, "SetQuestFlag"},
        {49, C<&ISystemSettingsServer::GetDataDeletionSettings>, "GetDataDeletionSettings"},
        {50, C<&ISystemSettingsServer::SetDataDeletionSettings>, "SetDataDeletionSettings"},
        {51, C<&ISystemSettingsServer::GetInitialSystemAppletProgramId>, "GetInitialSystemAppletProgramId"},
        {52, C<&ISystemSettingsServer::GetOverlayDispProgramId>, "GetOverlayDispProgramId"},
        {53, C<&ISystemSettingsServer::GetDeviceTimeZoneLocationName>, "GetDeviceTimeZoneLocationName"},
        {54, C<&ISystemSettingsServer::SetDeviceTimeZoneLocationName>, "SetDeviceTimeZoneLocationName"},
        {55, nullptr, "GetWirelessCertificationFileSize"},
        {56, nullptr, "GetWirelessCertificationFile"},
        {57, C<&ISystemSettingsServer::SetRegionCode>, "SetRegionCode"},
        {58, C<&ISystemSettingsServer::GetNetworkSystemClockContext>, "GetNetworkSystemClockContext"},
        {59, C<&ISystemSettingsServer::SetNetworkSystemClockContext>, "SetNetworkSystemClockContext"},
        {60, C<&ISystemSettingsServer::IsUserSystemClockAutomaticCorrectionEnabled>, "IsUserSystemClockAutomaticCorrectionEnabled"},
        {61, C<&ISystemSettingsServer::SetUserSystemClockAutomaticCorrectionEnabled>, "SetUserSystemClockAutomaticCorrectionEnabled"},
        {62, C<&ISystemSettingsServer::GetDebugModeFlag>, "GetDebugModeFlag"},
        {63, C<&ISystemSettingsServer::GetPrimaryAlbumStorage>, "GetPrimaryAlbumStorage"},
        {64, C<&ISystemSettingsServer::SetPrimaryAlbumStorage>, "SetPrimaryAlbumStorage"},
        {65, C<&ISystemSettingsServer::GetUsb30EnableFlag>, "GetUsb30EnableFlag"},
        {66, C<&ISystemSettingsServer::SetUsb30EnableFlag>, "SetUsb30EnableFlag"},
        {67, C<&ISystemSettingsServer::GetBatteryLot>, "GetBatteryLot"},
        {68, C<&ISystemSettingsServer::GetSerialNumber>, "GetSerialNumber"},
        {69, C<&ISystemSettingsServer::GetNfcEnableFlag>, "GetNfcEnableFlag"},
        {70, C<&ISystemSettingsServer::SetNfcEnableFlag>, "SetNfcEnableFlag"},
        {71, C<&ISystemSettingsServer::GetSleepSettings>, "GetSleepSettings"},
        {72, C<&ISystemSettingsServer::SetSleepSettings>, "SetSleepSettings"},
        {73, C<&ISystemSettingsServer::GetWirelessLanEnableFlag>, "GetWirelessLanEnableFlag"},
        {74, C<&ISystemSettingsServer::SetWirelessLanEnableFlag>, "SetWirelessLanEnableFlag"},
        {75, C<&ISystemSettingsServer::GetInitialLaunchSettings>, "GetInitialLaunchSettings"},
        {76, C<&ISystemSettingsServer::SetInitialLaunchSettings>, "SetInitialLaunchSettings"},
        {77, C<&ISystemSettingsServer::GetDeviceNickName>, "GetDeviceNickName"},
        {78, C<&ISystemSettingsServer::SetDeviceNickName>, "SetDeviceNickName"},
        {79, C<&ISystemSettingsServer::GetProductModel>, "GetProductModel"},
        {80, C<&ISystemSettingsServer::GetLdnChannel>, "GetLdnChannel"},
        {81, C<&ISystemSettingsServer::SetLdnChannel>, "SetLdnChannel"},
        {82, nullptr, "AcquireTelemetryDirtyFlagEventHandle"},
        {83, nullptr, "GetTelemetryDirtyFlags"},
        {84, C<&ISystemSettingsServer::GetPtmBatteryLot>, "GetPtmBatteryLot"},
        {85, C<&ISystemSettingsServer::SetPtmBatteryLot>, "SetPtmBatteryLot"},
        {86, C<&ISystemSettingsServer::GetPtmFuelGaugeParameter>, "GetPtmFuelGaugeParameter"},
        {87, C<&ISystemSettingsServer::SetPtmFuelGaugeParameter>, "SetPtmFuelGaugeParameter"},
        {88, C<&ISystemSettingsServer::GetBluetoothEnableFlag>, "GetBluetoothEnableFlag"},
        {89, C<&ISystemSettingsServer::SetBluetoothEnableFlag>, "SetBluetoothEnableFlag"},
        {90, C<&ISystemSettingsServer::GetMiiAuthorId>, "GetMiiAuthorId"},
        {91, C<&ISystemSettingsServer::SetShutdownRtcValue>, "SetShutdownRtcValue"},
        {92, C<&ISystemSettingsServer::GetShutdownRtcValue>, "GetShutdownRtcValue"},
        {93, nullptr, "AcquireFatalDirtyFlagEventHandle"},
        {94, nullptr, "GetFatalDirtyFlags"},
        {95, C<&ISystemSettingsServer::GetAutoUpdateEnableFlag>, "GetAutoUpdateEnableFlag"},
        {96, C<&ISystemSettingsServer::SetAutoUpdateEnableFlag>, "SetAutoUpdateEnableFlag"},
        {97, nullptr, "GetNxControllerSettings"},
        {98, nullptr, "SetNxControllerSettings"},
        {99, C<&ISystemSettingsServer::GetBatteryPercentageFlag>, "GetBatteryPercentageFlag"},
        {100, C<&ISystemSettingsServer::SetBatteryPercentageFlag>, "SetBatteryPercentageFlag"},
        {101, C<&ISystemSettingsServer::GetExternalRtcResetFlag>, "GetExternalRtcResetFlag"},
        {102, C<&ISystemSettingsServer::SetExternalRtcResetFlag>, "SetExternalRtcResetFlag"},
        {103, C<&ISystemSettingsServer::GetUsbFullKeyEnableFlag>, "GetUsbFullKeyEnableFlag"},
        {104, C<&ISystemSettingsServer::SetUsbFullKeyEnableFlag>, "SetUsbFullKeyEnableFlag"},
        {105, C<&ISystemSettingsServer::SetExternalSteadyClockInternalOffset>, "SetExternalSteadyClockInternalOffset"},
        {106, C<&ISystemSettingsServer::GetExternalSteadyClockInternalOffset>, "GetExternalSteadyClockInternalOffset"},
        {107, nullptr, "GetBacklightSettingsEx"},
        {108, nullptr, "SetBacklightSettingsEx"},
        {109, C<&ISystemSettingsServer::GetHeadphoneVolumeWarningCount>, "GetHeadphoneVolumeWarningCount"},
        {110, C<&ISystemSettingsServer::SetHeadphoneVolumeWarningCount>, "SetHeadphoneVolumeWarningCount"},
        {111, C<&ISystemSettingsServer::GetBluetoothAfhEnableFlag>, "GetBluetoothAfhEnableFlag"},
        {112, C<&ISystemSettingsServer::SetBluetoothAfhEnableFlag>, "SetBluetoothAfhEnableFlag"},
        {113, C<&ISystemSettingsServer::GetBluetoothBoostEnableFlag>, "GetBluetoothBoostEnableFlag"},
        {114, C<&ISystemSettingsServer::SetBluetoothBoostEnableFlag>, "SetBluetoothBoostEnableFlag"},
        {115, C<&ISystemSettingsServer::GetInRepairProcessEnableFlag>, "GetInRepairProcessEnableFlag"},
        {116, C<&ISystemSettingsServer::SetInRepairProcessEnableFlag>, "SetInRepairProcessEnableFlag"},
        {117, C<&ISystemSettingsServer::GetHeadphoneVolumeUpdateFlag>, "GetHeadphoneVolumeUpdateFlag"},
        {118, C<&ISystemSettingsServer::SetHeadphoneVolumeUpdateFlag>, "SetHeadphoneVolumeUpdateFlag"},
        {119, nullptr, "NeedsToUpdateHeadphoneVolume"},
        {120, C<&ISystemSettingsServer::GetPushNotificationActivityModeOnSleep>, "GetPushNotificationActivityModeOnSleep"},
        {121, C<&ISystemSettingsServer::SetPushNotificationActivityModeOnSleep>, "SetPushNotificationActivityModeOnSleep"},
        {122, C<&ISystemSettingsServer::GetServiceDiscoveryControlSettings>, "GetServiceDiscoveryControlSettings"},
        {123, C<&ISystemSettingsServer::SetServiceDiscoveryControlSettings>, "SetServiceDiscoveryControlSettings"},
        {124, C<&ISystemSettingsServer::GetErrorReportSharePermission>, "GetErrorReportSharePermission"},
        {125, C<&ISystemSettingsServer::SetErrorReportSharePermission>, "SetErrorReportSharePermission"},
        {126, C<&ISystemSettingsServer::GetAppletLaunchFlags>, "GetAppletLaunchFlags"},
        {127, C<&ISystemSettingsServer::SetAppletLaunchFlags>, "SetAppletLaunchFlags"},
        {128, C<&ISystemSettingsServer::GetConsoleSixAxisSensorAccelerationBias>, "GetConsoleSixAxisSensorAccelerationBias"},
        {129, C<&ISystemSettingsServer::SetConsoleSixAxisSensorAccelerationBias>, "SetConsoleSixAxisSensorAccelerationBias"},
        {130, C<&ISystemSettingsServer::GetConsoleSixAxisSensorAngularVelocityBias>, "GetConsoleSixAxisSensorAngularVelocityBias"},
        {131, C<&ISystemSettingsServer::SetConsoleSixAxisSensorAngularVelocityBias>, "SetConsoleSixAxisSensorAngularVelocityBias"},
        {132, C<&ISystemSettingsServer::GetConsoleSixAxisSensorAccelerationGain>, "GetConsoleSixAxisSensorAccelerationGain"},
        {133, C<&ISystemSettingsServer::SetConsoleSixAxisSensorAccelerationGain>, "SetConsoleSixAxisSensorAccelerationGain"},
        {134, C<&ISystemSettingsServer::GetConsoleSixAxisSensorAngularVelocityGain>, "GetConsoleSixAxisSensorAngularVelocityGain"},
        {135, C<&ISystemSettingsServer::SetConsoleSixAxisSensorAngularVelocityGain>, "SetConsoleSixAxisSensorAngularVelocityGain"},
        {136, C<&ISystemSettingsServer::GetKeyboardLayout>, "GetKeyboardLayout"},
        {137, C<&ISystemSettingsServer::SetKeyboardLayout>, "SetKeyboardLayout"},
        {138, C<&ISystemSettingsServer::GetWebInspectorFlag>, "GetWebInspectorFlag"},
        {139, nullptr, "GetAllowedSslHosts"},
        {140, nullptr, "GetHostFsMountPoint"},
        {141, C<&ISystemSettingsServer::GetRequiresRunRepairTimeReviser>, "GetRequiresRunRepairTimeReviser"},
        {142, C<&ISystemSettingsServer::SetRequiresRunRepairTimeReviser>, "SetRequiresRunRepairTimeReviser"},
        {143, C<&ISystemSettingsServer::SetBlePairingSettings>, "SetBlePairingSettings"},
        {144, C<&ISystemSettingsServer::GetBlePairingSettings>, "GetBlePairingSettings"},
        {145, C<&ISystemSettingsServer::GetConsoleSixAxisSensorAngularVelocityTimeBias>, "GetConsoleSixAxisSensorAngularVelocityTimeBias"},
        {146, C<&ISystemSettingsServer::SetConsoleSixAxisSensorAngularVelocityTimeBias>, "SetConsoleSixAxisSensorAngularVelocityTimeBias"},
        {147, C<&ISystemSettingsServer::GetConsoleSixAxisSensorAngularAcceleration>, "GetConsoleSixAxisSensorAngularAcceleration"},
        {148, C<&ISystemSettingsServer::SetConsoleSixAxisSensorAngularAcceleration>, "SetConsoleSixAxisSensorAngularAcceleration"},
        {149, C<&ISystemSettingsServer::GetRebootlessSystemUpdateVersion>, "GetRebootlessSystemUpdateVersion"},
        {150, C<&ISystemSettingsServer::GetDeviceTimeZoneLocationUpdatedTime>, "GetDeviceTimeZoneLocationUpdatedTime"},
        {151, C<&ISystemSettingsServer::SetDeviceTimeZoneLocationUpdatedTime>, "SetDeviceTimeZoneLocationUpdatedTime"},
        {152, C<&ISystemSettingsServer::GetUserSystemClockAutomaticCorrectionUpdatedTime>, "GetUserSystemClockAutomaticCorrectionUpdatedTime"},
        {153, C<&ISystemSettingsServer::SetUserSystemClockAutomaticCorrectionUpdatedTime>, "SetUserSystemClockAutomaticCorrectionUpdatedTime"},
        {154, C<&ISystemSettingsServer::GetAccountOnlineStorageSettings>, "GetAccountOnlineStorageSettings"},
        {155, C<&ISystemSettingsServer::SetAccountOnlineStorageSettings>, "SetAccountOnlineStorageSettings"},
        {156, C<&ISystemSettingsServer::GetPctlReadyFlag>, "GetPctlReadyFlag"},
        {157, C<&ISystemSettingsServer::SetPctlReadyFlag>, "SetPctlReadyFlag"},
        {158, C<&ISystemSettingsServer::GetAnalogStickUserCalibrationL>, "GetAnalogStickUserCalibrationL"},
        {159, C<&ISystemSettingsServer::SetAnalogStickUserCalibrationL>, "SetAnalogStickUserCalibrationL"},
        {160, C<&ISystemSettingsServer::GetAnalogStickUserCalibrationR>, "GetAnalogStickUserCalibrationR"},
        {161, C<&ISystemSettingsServer::SetAnalogStickUserCalibrationR>, "SetAnalogStickUserCalibrationR"},
        {162, C<&ISystemSettingsServer::GetPtmBatteryVersion>, "GetPtmBatteryVersion"},
        {163, C<&ISystemSettingsServer::SetPtmBatteryVersion>, "SetPtmBatteryVersion"},
        {164, C<&ISystemSettingsServer::GetUsb30HostEnableFlag>, "GetUsb30HostEnableFlag"},
        {165, C<&ISystemSettingsServer::SetUsb30HostEnableFlag>, "SetUsb30HostEnableFlag"},
        {166, C<&ISystemSettingsServer::GetUsb30DeviceEnableFlag>, "GetUsb30DeviceEnableFlag"},
        {167, C<&ISystemSettingsServer::SetUsb30DeviceEnableFlag>, "SetUsb30DeviceEnableFlag"},
        {168, C<&ISystemSettingsServer::GetThemeId>, "GetThemeId"},
        {169, C<&ISystemSettingsServer::SetThemeId>, "SetThemeId"},
        {170, C<&ISystemSettingsServer::GetChineseTraditionalInputMethod>, "GetChineseTraditionalInputMethod"},
        {171, C<&ISystemSettingsServer::SetChineseTraditionalInputMethod>, "SetChineseTraditionalInputMethod"},
        {172, C<&ISystemSettingsServer::GetPtmCycleCountReliability>, "GetPtmCycleCountReliability"},
        {173, C<&ISystemSettingsServer::SetPtmCycleCountReliability>, "SetPtmCycleCountReliability"},
        {174, C<&ISystemSettingsServer::GetHomeMenuScheme>, "GetHomeMenuScheme"},
        {175, C<&ISystemSettingsServer::GetThemeSettings>, "GetThemeSettings"},
        {176, C<&ISystemSettingsServer::SetThemeSettings>, "SetThemeSettings"},
        {177, C<&ISystemSettingsServer::GetThemeKey>, "GetThemeKey"},
        {178, C<&ISystemSettingsServer::SetThemeKey>, "SetThemeKey"},
        {179, C<&ISystemSettingsServer::GetZoomFlag>, "GetZoomFlag"},
        {180, C<&ISystemSettingsServer::SetZoomFlag>, "SetZoomFlag"},
        {181, nullptr, "GetT"},
        {182, nullptr, "SetT"},
        {183, C<&ISystemSettingsServer::GetPlatformRegion>, "GetPlatformRegion"},
        {184, C<&ISystemSettingsServer::SetPlatformRegion>, "SetPlatformRegion"},
        {185, C<&ISystemSettingsServer::GetHomeMenuSchemeModel>, "GetHomeMenuSchemeModel"},
        {186, C<&ISystemSettingsServer::GetMemoryUsageRateFlag>, "GetMemoryUsageRateFlag"},
        {187, C<&ISystemSettingsServer::GetTouchScreenMode>, "GetTouchScreenMode"},
        {188, C<&ISystemSettingsServer::SetTouchScreenMode>, "SetTouchScreenMode"},
        {189, nullptr, "GetButtonConfigSettingsFull"},
        {190, nullptr, "SetButtonConfigSettingsFull"},
        {191, nullptr, "GetButtonConfigSettingsEmbedded"},
        {192, nullptr, "SetButtonConfigSettingsEmbedded"},
        {193, nullptr, "GetButtonConfigSettingsLeft"},
        {194, nullptr, "SetButtonConfigSettingsLeft"},
        {195, nullptr, "GetButtonConfigSettingsRight"},
        {196, nullptr, "SetButtonConfigSettingsRight"},
        {197, nullptr, "GetButtonConfigRegisteredSettingsEmbedded"},
        {198, nullptr, "SetButtonConfigRegisteredSettingsEmbedded"},
        {199, nullptr, "GetButtonConfigRegisteredSettings"},
        {200, nullptr, "SetButtonConfigRegisteredSettings"},
        {201, C<&ISystemSettingsServer::GetFieldTestingFlag>, "GetFieldTestingFlag"},
        {202, C<&ISystemSettingsServer::SetFieldTestingFlag>, "SetFieldTestingFlag"},
        {203, C<&ISystemSettingsServer::GetPanelCrcMode>, "GetPanelCrcMode"},
        {204, C<&ISystemSettingsServer::SetPanelCrcMode>, "SetPanelCrcMode"},
        {205, nullptr, "GetNxControllerSettingsEx"},
        {206, nullptr, "SetNxControllerSettingsEx"},
        {207, C<&ISystemSettingsServer::GetHearingProtectionSafeguardFlag>, "GetHearingProtectionSafeguardFlag"},
        {208, C<&ISystemSettingsServer::SetHearingProtectionSafeguardFlag>, "SetHearingProtectionSafeguardFlag"},
        {209, C<&ISystemSettingsServer::GetHearingProtectionSafeguardRemainingTime>, "GetHearingProtectionSafeguardRemainingTime"},
        {210, C<&ISystemSettingsServer::SetHearingProtectionSafeguardRemainingTime>, "SetHearingProtectionSafeguardRemainingTime"},
        {221, nullptr, "GetForceMonauralOutputFlag"}, //17.0.0+
        {222, nullptr, "SetForceMonauralOutputFlag"}, //17.0.0+
        {251, nullptr, "GetAccountIdentificationSettings"}, //18.0.0+
        {252, nullptr, "SetAccountIdentificationSettings"}, //18.0.0+
        {263, nullptr, "AcquireVphymDirtyFlagEventHandle"}, //20.0.0+
        {264, nullptr, "GetVphymDirtyFlags"}, //20.0.0+
        {282, nullptr, "ConvertToProductModel"}, //20.0.0+
        {283, nullptr, "ConvertToProductModelName"}, //20.0.0+
        {289, nullptr, "GetDefaultAccountIdentificationFlagSet"}, //20.0.0+
        {300, nullptr, "AcquirePushNotificationDirtyFlagEventHandle"}, //20.0.0+
        {301, nullptr, "GetPushNotificationDirtyFlags"}, //20.0.0+
        {306, nullptr, "GetPinCodeReregistrationGuideAccounts"}, //20.0.0+
        {307, nullptr, "SetPinCodeReregistrationGuideAccounts"}, //20.0.0+
        {315, C<&ISystemSettingsServer::GetHttpAuthConfigs>, "GetHttpAuthConfigs"}, //21.0.0+
        {319, C<&ISystemSettingsServer::GetAccountUserSettings>, "GetAccountUserSettings"}, //21.0.0+
        {320, nullptr, "SetAccountUserSettings"}, //21.0.0+
        {321, C<&ISystemSettingsServer::GetDefaultAccountUserSettings>, "GetDefaultAccountUserSettings"}, //21.0.0+
    };
    // clang-format on

    RegisterHandlers(functions);

    SetupSettings();

    m_system_settings.region_code =
        static_cast<SystemRegionCode>(::Settings::values.region_index.GetValue());

    // TODO: Remove this when starter applet is fully functional
    EulaVersion eula_version{
        .version = 0x10000,
        .region_code = m_system_settings.region_code,
        .clock_type = EulaVersionClockType::SteadyClock,
        .system_clock_context = m_system_settings.user_system_clock_context,
    };
    m_system_settings.eula_versions[0] = eula_version;
    m_system_settings.eula_version_count = 1;
}

ISystemSettingsServer::~ISystemSettingsServer() {
    SetSaveNeeded();
}

bool ISystemSettingsServer::LoadSettingsFile(std::filesystem::path& path, auto&& default_func) {
    using settings_type = decltype(default_func());

    if (!Common::FS::CreateDirs(path)) {
        return false;
    }

    auto settings_file = path / "settings.dat";
    auto exists = std::filesystem::exists(settings_file);
    auto file_size_ok = exists && std::filesystem::file_size(settings_file) ==
                                      sizeof(SettingsHeader) + sizeof(settings_type);

    auto ResetToDefault = [&]() {
        auto default_settings{default_func()};

        SettingsHeader hdr{
            .magic = SETTINGS_MAGIC,
            .version = SETTINGS_VERSION,
            .reserved = 0u,
        };

        std::ofstream out_settings_file(settings_file, std::ios::out | std::ios::binary);
        out_settings_file.write(reinterpret_cast<const char*>(&hdr), sizeof(hdr));
        out_settings_file.write(reinterpret_cast<const char*>(&default_settings),
                                sizeof(settings_type));
        out_settings_file.flush();
        out_settings_file.close();
    };

    constexpr auto IsHeaderValid = [](std::ifstream& file) -> bool {
        if (!file.is_open()) {
            return false;
        }
        SettingsHeader hdr{};
        file.read(reinterpret_cast<char*>(&hdr), sizeof(hdr));
        return hdr.magic == SETTINGS_MAGIC && hdr.version >= SETTINGS_VERSION;
    };

    if (!exists || !file_size_ok) {
        ResetToDefault();
    }

    std::ifstream file(settings_file, std::ios::binary | std::ios::in);
    if (!IsHeaderValid(file)) {
        file.close();
        ResetToDefault();
        file = std::ifstream(settings_file, std::ios::binary | std::ios::in);
        if (!IsHeaderValid(file)) {
            return false;
        }
    }

    if constexpr (std::is_same_v<settings_type, PrivateSettings>) {
        file.read(reinterpret_cast<char*>(&m_private_settings), sizeof(settings_type));
    } else if constexpr (std::is_same_v<settings_type, DeviceSettings>) {
        file.read(reinterpret_cast<char*>(&m_device_settings), sizeof(settings_type));
    } else if constexpr (std::is_same_v<settings_type, ApplnSettings>) {
        file.read(reinterpret_cast<char*>(&m_appln_settings), sizeof(settings_type));
    } else if constexpr (std::is_same_v<settings_type, SystemSettings>) {
        file.read(reinterpret_cast<char*>(&m_system_settings), sizeof(settings_type));
    } else {
        UNREACHABLE();
    }
    file.close();

    return true;
}

bool ISystemSettingsServer::StoreSettingsFile(std::filesystem::path& path, auto& settings) {
    using settings_type = std::decay_t<decltype(settings)>;

    if (!Common::FS::IsDir(path)) {
        return false;
    }

    auto settings_base = path / "settings";
    std::filesystem::path settings_tmp_file = settings_base;
    settings_tmp_file = settings_tmp_file.replace_extension("tmp");
    std::ofstream file(settings_tmp_file, std::ios::binary | std::ios::out);
    if (!file.is_open()) {
        return false;
    }

    SettingsHeader hdr{
        .magic = SETTINGS_MAGIC,
        .version = SETTINGS_VERSION,
        .reserved = 0u,
    };
    file.write(reinterpret_cast<const char*>(&hdr), sizeof(hdr));

    if constexpr (std::is_same_v<settings_type, PrivateSettings>) {
        file.write(reinterpret_cast<const char*>(&m_private_settings), sizeof(settings_type));
    } else if constexpr (std::is_same_v<settings_type, DeviceSettings>) {
        file.write(reinterpret_cast<const char*>(&m_device_settings), sizeof(settings_type));
    } else if constexpr (std::is_same_v<settings_type, ApplnSettings>) {
        file.write(reinterpret_cast<const char*>(&m_appln_settings), sizeof(settings_type));
    } else if constexpr (std::is_same_v<settings_type, SystemSettings>) {
        file.write(reinterpret_cast<const char*>(&m_system_settings), sizeof(settings_type));
    } else {
        UNREACHABLE();
    }
    file.close();

    std::filesystem::rename(settings_tmp_file, settings_base.replace_extension("dat"));

    return true;
}

Result ISystemSettingsServer::SetLanguageCode(LanguageCode language_code) {
    LOG_INFO(Service_SET, "called, language_code={}", language_code);

    m_system_settings.language_code = language_code;
    SyncGlobalLanguageFromCode(language_code);
    SetSaveNeeded();
    R_SUCCEED();
}

Result ISystemSettingsServer::GetFirmwareVersion(
    OutLargeData<FirmwareVersionFormat, BufferAttr_HipcPointer> out_firmware_data) {
    LOG_DEBUG(Service_SET, "called");

    R_RETURN(GetFirmwareVersionImpl(*out_firmware_data, system, GetFirmwareVersionType::Version1));
}

Result ISystemSettingsServer::GetFirmwareVersion2(
    OutLargeData<FirmwareVersionFormat, BufferAttr_HipcPointer> out_firmware_data) {
    LOG_DEBUG(Service_SET, "called");

    R_RETURN(GetFirmwareVersionImpl(*out_firmware_data, system, GetFirmwareVersionType::Version2));
}

Result ISystemSettingsServer::GetLockScreenFlag(Out<bool> out_lock_screen_flag) {
    LOG_INFO(Service_SET, "called, lock_screen_flag={}", m_system_settings.lock_screen_flag);

    *out_lock_screen_flag = m_system_settings.lock_screen_flag;
    R_SUCCEED();
}

Result ISystemSettingsServer::SetLockScreenFlag(bool lock_screen_flag) {
    LOG_INFO(Service_SET, "called, lock_screen_flag={}", lock_screen_flag);

    m_system_settings.lock_screen_flag = lock_screen_flag;
    SetSaveNeeded();
    R_SUCCEED();
}

Result ISystemSettingsServer::GetExternalSteadyClockSourceId(
    Out<Common::UUID> out_clock_source_id) {
    LOG_INFO(Service_SET, "called, clock_source_id={}",
             m_private_settings.external_clock_source_id.FormattedString());

    *out_clock_source_id = m_private_settings.external_clock_source_id;
    R_SUCCEED();
}

Result ISystemSettingsServer::SetExternalSteadyClockSourceId(const Common::UUID& clock_source_id) {
    LOG_INFO(Service_SET, "called, clock_source_id={}", clock_source_id.FormattedString());

    m_private_settings.external_clock_source_id = clock_source_id;
    SetSaveNeeded();
    R_SUCCEED();
}

Result ISystemSettingsServer::GetUserSystemClockContext(
    Out<Service::PSC::Time::SystemClockContext> out_clock_context) {
    LOG_INFO(Service_SET, "called");

    *out_clock_context = m_system_settings.user_system_clock_context;
    R_SUCCEED();
}

Result ISystemSettingsServer::SetUserSystemClockContext(
    const Service::PSC::Time::SystemClockContext& clock_context) {
    LOG_INFO(Service_SET, "called");

    m_system_settings.user_system_clock_context = clock_context;
    SetSaveNeeded();
    R_SUCCEED();
}

Result ISystemSettingsServer::GetAccountSettings(Out<AccountSettings> out_account_settings) {
    LOG_INFO(Service_SET, "called, account_settings_flags={}",
             m_system_settings.account_settings.flags);

    *out_account_settings = m_system_settings.account_settings;
    R_SUCCEED();
}

Result ISystemSettingsServer::SetAccountSettings(AccountSettings account_settings) {
    LOG_INFO(Service_SET, "called, account_settings_flags={}", account_settings.flags);

    m_system_settings.account_settings = account_settings;
    SetSaveNeeded();
    R_SUCCEED();
}

Result ISystemSettingsServer::GetEulaVersions(
    Out<s32> out_count, OutArray<EulaVersion, BufferAttr_HipcMapAlias> out_eula_versions) {
    LOG_INFO(Service_SET, "called, elements={}", m_system_settings.eula_version_count);

    *out_count =
        (std::min)(m_system_settings.eula_version_count, static_cast<s32>(out_eula_versions.size()));
    memcpy(out_eula_versions.data(), m_system_settings.eula_versions.data(),
           static_cast<std::size_t>(*out_count) * sizeof(EulaVersion));
    R_SUCCEED();
}

Result ISystemSettingsServer::SetEulaVersions(
    InArray<EulaVersion, BufferAttr_HipcMapAlias> eula_versions) {
    LOG_INFO(Service_SET, "called, elements={}", eula_versions.size());

    ASSERT(eula_versions.size() <= m_system_settings.eula_versions.size());

    m_system_settings.eula_version_count = static_cast<s32>(eula_versions.size());
    std::memcpy(m_system_settings.eula_versions.data(), eula_versions.data(),
                eula_versions.size() * sizeof(EulaVersion));
    SetSaveNeeded();
    R_SUCCEED();
}

Result ISystemSettingsServer::GetColorSetId(Out<ColorSet> out_color_set_id) {
    LOG_DEBUG(Service_SET, "called, color_set=", m_system_settings.color_set_id);

    *out_color_set_id = m_system_settings.color_set_id;
    R_SUCCEED();
}

Result ISystemSettingsServer::SetColorSetId(ColorSet color_set_id) {
    LOG_DEBUG(Service_SET, "called, color_set={}", color_set_id);

    m_system_settings.color_set_id = color_set_id;
    SetSaveNeeded();
    R_SUCCEED();
}

Result ISystemSettingsServer::GetNotificationSettings(
    Out<NotificationSettings> out_notification_settings) {
    LOG_INFO(Service_SET, "called, flags={}, volume={}, head_time={}:{}, tailt_time={}:{}",
             m_system_settings.notification_settings.flags.raw,
             m_system_settings.notification_settings.volume,
             m_system_settings.notification_settings.start_time.hour,
             m_system_settings.notification_settings.start_time.minute,
             m_system_settings.notification_settings.stop_time.hour,
             m_system_settings.notification_settings.stop_time.minute);

    *out_notification_settings = m_system_settings.notification_settings;
    R_SUCCEED();
}

Result ISystemSettingsServer::SetNotificationSettings(
    const NotificationSettings& notification_settings) {
    LOG_INFO(Service_SET, "called, flags={}, volume={}, head_time={}:{}, tailt_time={}:{}",
             notification_settings.flags.raw, notification_settings.volume,
             notification_settings.start_time.hour, notification_settings.start_time.minute,
             notification_settings.stop_time.hour, notification_settings.stop_time.minute);

    m_system_settings.notification_settings = notification_settings;
    SetSaveNeeded();
    R_SUCCEED();
}

Result ISystemSettingsServer::GetAccountNotificationSettings(
    Out<s32> out_count, OutArray<AccountNotificationSettings, BufferAttr_HipcMapAlias>
                            out_account_notification_settings) {
    LOG_INFO(Service_SET, "called, elements={}",
             m_system_settings.account_notification_settings_count);

    *out_count = (std::min)(m_system_settings.account_notification_settings_count,
                          static_cast<s32>(out_account_notification_settings.size()));
    memcpy(out_account_notification_settings.data(),
           m_system_settings.account_notification_settings.data(),
           static_cast<std::size_t>(*out_count) * sizeof(AccountNotificationSettings));

    R_SUCCEED();
}

Result ISystemSettingsServer::SetAccountNotificationSettings(
    InArray<AccountNotificationSettings, BufferAttr_HipcMapAlias> account_notification_settings) {
    LOG_INFO(Service_SET, "called, elements={}", account_notification_settings.size());

    ASSERT(account_notification_settings.size() <=
           m_system_settings.account_notification_settings.size());

    m_system_settings.account_notification_settings_count =
        static_cast<s32>(account_notification_settings.size());
    std::memcpy(m_system_settings.account_notification_settings.data(),
                account_notification_settings.data(),
                account_notification_settings.size() * sizeof(AccountNotificationSettings));
    SetSaveNeeded();
    R_SUCCEED();
}

Result ISystemSettingsServer::GetVibrationMasterVolume(Out<f32> vibration_master_volume) {
    LOG_INFO(Service_SET, "called, vibration_master_volume={}",
             m_system_settings.vibration_master_volume);

    *vibration_master_volume = m_system_settings.vibration_master_volume;
    R_SUCCEED();
}

Result ISystemSettingsServer::SetVibrationMasterVolume(f32 vibration_master_volume) {
    LOG_INFO(Service_SET, "called, vibration_master_volume={}", vibration_master_volume);

    m_system_settings.vibration_master_volume = vibration_master_volume;
    SetSaveNeeded();
    R_SUCCEED();
}

// FIXME: implement support for the real system_settings.ini

template <typename T>
static std::vector<u8> ToBytes(const T& value) {
    static_assert(std::is_trivially_copyable_v<T>);

    const auto* begin = reinterpret_cast<const u8*>(&value);
    const auto* end = begin + sizeof(T);

    return std::vector<u8>(begin, end);
}

using Settings =
    std::map<std::string, std::map<std::string, std::vector<u8>, std::less<>>, std::less<>>;

static Settings GetSettings() {
    Settings ret;

    // AM
    ret["hbloader"]["applet_heap_size"] = ToBytes(u64{0x0});
    ret["hbloader"]["applet_heap_reservation_size"] = ToBytes(u64{0x8600000});

    // Time
    ret["time"]["notify_time_to_fs_interval_seconds"] = ToBytes(s32{600});
    ret["time"]["standard_network_clock_sufficient_accuracy_minutes"] =
        ToBytes(s32{43200}); // 30 days
    ret["time"]["standard_steady_clock_rtc_update_interval_minutes"] = ToBytes(s32{5});
    ret["time"]["standard_steady_clock_test_offset_minutes"] = ToBytes(s32{0});
    ret["time"]["standard_user_clock_initial_year"] = ToBytes(s32{2023});

    // HID
    ret["hid"]["has_rail_interface"] = ToBytes(bool{true});
    ret["hid"]["has_sio_mcu"] = ToBytes(bool{true});
    ret["hid_debug"]["enables_debugpad"] = ToBytes(bool{true});
    ret["hid_debug"]["manages_devices"] = ToBytes(bool{true});
    ret["hid_debug"]["manages_touch_ic_i2c"] = ToBytes(bool{true});
    ret["hid_debug"]["emulate_future_device"] = ToBytes(bool{false});
    ret["hid_debug"]["emulate_mcu_hardware_error"] = ToBytes(bool{false});
    ret["hid_debug"]["enables_rail"] = ToBytes(bool{true});
    ret["hid_debug"]["emulate_firmware_update_failure"] = ToBytes(bool{false});
    ret["hid_debug"]["failure_firmware_update"] = ToBytes(s32{0});
    ret["hid_debug"]["ble_disabled"] = ToBytes(bool{false});
    ret["hid_debug"]["dscale_disabled"] = ToBytes(bool{false});
    ret["hid_debug"]["force_handheld"] = ToBytes(bool{true});
    ret["hid_debug"]["disabled_features_per_id"] = std::vector<u8>(0xa8);
    ret["hid_debug"]["touch_firmware_auto_update_disabled"] = ToBytes(bool{false});

    // Mii
    ret["mii"]["is_db_test_mode_enabled"] = ToBytes(bool{false});

    // Settings
    ret["settings_debug"]["is_debug_mode_enabled"] = ToBytes(bool{false});

    // Error
    ret["err"]["applet_auto_close"] = ToBytes(bool{false});

    return ret;
}

Result ISystemSettingsServer::GetSettingsItemValueSize(
    Out<u64> out_size, InLargeData<SettingItemName, BufferAttr_HipcPointer> setting_category_buffer,
    InLargeData<SettingItemName, BufferAttr_HipcPointer> setting_name_buffer) {
    const std::string setting_category{Common::StringFromBuffer(*setting_category_buffer)};
    const std::string setting_name{Common::StringFromBuffer(*setting_name_buffer)};

    LOG_DEBUG(Service_SET, "called, category={}, name={}", setting_category, setting_name);

    *out_size = 0;

    auto settings{GetSettings()};
    if (settings.contains(setting_category) && settings[setting_category].contains(setting_name)) {
        *out_size = settings[setting_category][setting_name].size();
    }

    R_UNLESS(*out_size != 0, ResultUnknown);
    R_SUCCEED();
}

Result ISystemSettingsServer::GetSettingsItemValue(
    Out<u64> out_size, OutBuffer<BufferAttr_HipcMapAlias> out_data,
    InLargeData<SettingItemName, BufferAttr_HipcPointer> setting_category_buffer,
    InLargeData<SettingItemName, BufferAttr_HipcPointer> setting_name_buffer) {
    const std::string setting_category{Common::StringFromBuffer(*setting_category_buffer)};
    const std::string setting_name{Common::StringFromBuffer(*setting_name_buffer)};

    LOG_INFO(Service_SET, "called, category={}, name={}", setting_category, setting_name);

    R_RETURN(GetSettingsItemValueImpl(out_data, *out_size, setting_category, setting_name));
}

Result ISystemSettingsServer::GetTvSettings(Out<TvSettings> out_tv_settings) {
    LOG_INFO(Service_SET,
             "called, flags={}, cmu_mode={}, contrast_ratio={}, hdmi_content_type={}, "
             "rgb_range={}, tv_gama={}, tv_resolution={}, tv_underscan={}",
             m_system_settings.tv_settings.flags.raw, m_system_settings.tv_settings.cmu_mode,
             m_system_settings.tv_settings.contrast_ratio,
             m_system_settings.tv_settings.hdmi_content_type,
             m_system_settings.tv_settings.rgb_range, m_system_settings.tv_settings.tv_gama,
             m_system_settings.tv_settings.tv_resolution,
             m_system_settings.tv_settings.tv_underscan);

    *out_tv_settings = m_system_settings.tv_settings;
    R_SUCCEED();
}

Result ISystemSettingsServer::SetTvSettings(TvSettings tv_settings) {

    LOG_INFO(Service_SET,
             "called, flags={}, cmu_mode={}, contrast_ratio={}, hdmi_content_type={}, "
             "rgb_range={}, tv_gama={}, tv_resolution={}, tv_underscan={}",
             tv_settings.flags.raw, tv_settings.cmu_mode, tv_settings.contrast_ratio,
             tv_settings.hdmi_content_type, tv_settings.rgb_range, tv_settings.tv_gama,
             tv_settings.tv_resolution, tv_settings.tv_underscan);

    m_system_settings.tv_settings = tv_settings;
    SetSaveNeeded();
    R_SUCCEED();
}

Result ISystemSettingsServer::GetAudioOutputMode(Out<AudioOutputMode> out_output_mode,
                                                 AudioOutputModeTarget target) {
    switch (target) {
    case AudioOutputModeTarget::Hdmi:
        *out_output_mode = m_system_settings.audio_output_mode_hdmi;
        break;
    case AudioOutputModeTarget::Speaker:
        *out_output_mode = m_system_settings.audio_output_mode_speaker;
        break;
    case AudioOutputModeTarget::Headphone:
        *out_output_mode = m_system_settings.audio_output_mode_headphone;
        break;
    case AudioOutputModeTarget::Type3:
        *out_output_mode = m_system_settings.audio_output_mode_type3;
        break;
    case AudioOutputModeTarget::Type4:
        *out_output_mode = m_system_settings.audio_output_mode_type4;
        break;
    default:
        LOG_ERROR(Service_SET, "Invalid audio output mode target {}", target);
    }

    LOG_INFO(Service_SET, "called, target={}, output_mode={}", target, *out_output_mode);
    R_SUCCEED();
}

Result ISystemSettingsServer::SetAudioOutputMode(AudioOutputModeTarget target,
                                                 AudioOutputMode output_mode) {
    LOG_INFO(Service_SET, "called, target={}, output_mode={}", target, output_mode);

    switch (target) {
    case AudioOutputModeTarget::Hdmi:
        m_system_settings.audio_output_mode_hdmi = output_mode;
        break;
    case AudioOutputModeTarget::Speaker:
        m_system_settings.audio_output_mode_speaker = output_mode;
        break;
    case AudioOutputModeTarget::Headphone:
        m_system_settings.audio_output_mode_headphone = output_mode;
        break;
    case AudioOutputModeTarget::Type3:
        m_system_settings.audio_output_mode_type3 = output_mode;
        break;
    case AudioOutputModeTarget::Type4:
        m_system_settings.audio_output_mode_type4 = output_mode;
        break;
    default:
        LOG_ERROR(Service_SET, "Invalid audio output mode target {}", target);
    }

    SetSaveNeeded();
    R_SUCCEED();
}

Result ISystemSettingsServer::GetSpeakerAutoMuteFlag(
    Out<bool> out_force_mute_on_headphone_removed) {
    LOG_INFO(Service_SET, "called, force_mute_on_headphone_removed={}",
             m_system_settings.force_mute_on_headphone_removed);

    *out_force_mute_on_headphone_removed = m_system_settings.force_mute_on_headphone_removed;
    R_SUCCEED();
}

Result ISystemSettingsServer::SetSpeakerAutoMuteFlag(bool force_mute_on_headphone_removed) {
    LOG_INFO(Service_SET, "called, force_mute_on_headphone_removed={}",
             force_mute_on_headphone_removed);

    m_system_settings.force_mute_on_headphone_removed = force_mute_on_headphone_removed;
    SetSaveNeeded();
    R_SUCCEED();
}

Result ISystemSettingsServer::GetQuestFlag(Out<QuestFlag> out_quest_flag) {
    LOG_INFO(Service_SET, "called, quest_flag={}", m_system_settings.quest_flag);

    *out_quest_flag = m_system_settings.quest_flag;
    R_SUCCEED();
}

Result ISystemSettingsServer::SetQuestFlag(QuestFlag quest_flag) {
    LOG_INFO(Service_SET, "called, quest_flag={}", quest_flag);

    m_system_settings.quest_flag = quest_flag;
    SetSaveNeeded();
    R_SUCCEED();
}

Result ISystemSettingsServer::GetRebootlessSystemUpdateVersion(
    Out<RebootlessSystemUpdateVersion> out_rebootless_system_update) {
    // Encore does not install Horizon rebootless-update packages. Report the explicit empty state.
    *out_rebootless_system_update = {};
    strcpy(out_rebootless_system_update->display_version, "0.0.0");
    LOG_DEBUG(Service_SET, "No rebootless system update installed");
    R_SUCCEED();
}

Result ISystemSettingsServer::GetDeviceTimeZoneLocationName(
    Out<Service::PSC::Time::LocationName> out_name) {
    LOG_INFO(Service_SET, "called");

    *out_name = m_system_settings.device_time_zone_location_name;
    R_SUCCEED();
}

Result ISystemSettingsServer::SetDeviceTimeZoneLocationName(
    const Service::PSC::Time::LocationName& name) {
    LOG_INFO(Service_SET, "called");

    m_system_settings.device_time_zone_location_name = name;
    SetSaveNeeded();
    R_SUCCEED();
}

Result ISystemSettingsServer::SetRegionCode(SystemRegionCode region_code) {
    LOG_INFO(Service_SET, "called, region_code={}", region_code);

    m_system_settings.region_code = region_code;
    SyncGlobalRegionFromCode(region_code);
    SetSaveNeeded();
    R_SUCCEED();
}

Result ISystemSettingsServer::GetNetworkSystemClockContext(
    Out<Service::PSC::Time::SystemClockContext> out_context) {
    LOG_INFO(Service_SET, "called");

    *out_context = m_system_settings.network_system_clock_context;
    R_SUCCEED();
}

Result ISystemSettingsServer::SetNetworkSystemClockContext(
    const Service::PSC::Time::SystemClockContext& context) {
    LOG_INFO(Service_SET, "called");

    m_system_settings.network_system_clock_context = context;
    SetSaveNeeded();
    R_SUCCEED();
}

Result ISystemSettingsServer::IsUserSystemClockAutomaticCorrectionEnabled(
    Out<bool> out_automatic_correction_enabled) {
    LOG_INFO(Service_SET, "called, out_automatic_correction_enabled={}",
             m_system_settings.user_system_clock_automatic_correction_enabled);

    *out_automatic_correction_enabled =
        m_system_settings.user_system_clock_automatic_correction_enabled;
    R_SUCCEED();
}

Result ISystemSettingsServer::SetUserSystemClockAutomaticCorrectionEnabled(
    bool automatic_correction_enabled) {
    LOG_INFO(Service_SET, "called, out_automatic_correction_enabled={}",
             automatic_correction_enabled);

    m_system_settings.user_system_clock_automatic_correction_enabled = automatic_correction_enabled;
    SetSaveNeeded();
    R_SUCCEED();
}

Result ISystemSettingsServer::GetDebugModeFlag(Out<bool> is_debug_mode_enabled) {
    const auto result = GetSettingsItemValueImpl<bool>(*is_debug_mode_enabled, "settings_debug",
                                                       "is_debug_mode_enabled");

    LOG_DEBUG(Service_SET, "called, is_debug_mode_enabled={}", *is_debug_mode_enabled);
    R_RETURN(result);
}

Result ISystemSettingsServer::GetPrimaryAlbumStorage(
    Out<PrimaryAlbumStorage> out_primary_album_storage) {
    LOG_INFO(Service_SET, "called, primary_album_storage={}",
             m_system_settings.primary_album_storage);

    *out_primary_album_storage = m_system_settings.primary_album_storage;
    R_SUCCEED();
}

Result ISystemSettingsServer::SetPrimaryAlbumStorage(PrimaryAlbumStorage primary_album_storage) {
    LOG_INFO(Service_SET, "called, primary_album_storage={}", primary_album_storage);

    m_system_settings.primary_album_storage = primary_album_storage;
    SetSaveNeeded();
    R_SUCCEED();
}

static void Fill3DS_CRC(u32 d, char* data) {
    std::array<u8, 10> digits = {
        u8((d / 1000000000) % 100),
        u8((d / 100000000) % 10),
        u8((d / 10000000) % 10),
        u8((d / 1000000) % 10),
        u8((d / 100000) % 10),
        u8((d / 10000) % 10),
        u8((d / 1000) % 10),
        u8((d / 100) % 10),
        u8((d / 10) % 10),
        u8(d % 10),
    };
    // Normalize to retail values
    std::array<u8, 4> retail_digits = { 1, 4, 5, 7 };
    digits[0] = retail_digits[(d % 10) % 4];
    digits[1] = 0;
    //
    for (size_t i = 0; i < sizeof(digits); ++i)
        data[i] = char(digits[i] + '0');
    u8 sum_odd = 0, sum_even = 0;
    for (size_t i = 0; i < sizeof(digits); i += 2) {
        sum_odd += digits[i + 0];
        sum_even += digits[i + 1];
    }
    u8 sum_digit = u8(((sum_even * 3) + sum_odd) % 10);
    if (sum_digit != 0)
        sum_digit = 10 - sum_digit;
    data[sizeof(digits)] = char(sum_digit + '0');
}

Result ISystemSettingsServer::GetBatteryLot(Out<BatteryLot> out_battery_lot) {
    LOG_INFO(Service_SET, "called");
    *out_battery_lot = []{
        u32 d = ::Settings::values.serial_battery.GetValue();
        BatteryLot c{};
        c.lot_number[0] = 'B';
        c.lot_number[1] = 'H';
        c.lot_number[2] = 'A';
        c.lot_number[3] = 'C';
        // TODO: what do the letters mean?
        c.lot_number[4] = 'H';
        c.lot_number[5] = 'Z';
        c.lot_number[6] = 'Z';
        c.lot_number[7] = 'A';
        c.lot_number[8] = 'D';
        c.lot_number[9] = char(((d / 100000) % 26) + 'A');
        Fill3DS_CRC(d, c.lot_number.data() + 10);
        return c;
    }();
    R_SUCCEED();
}

Result ISystemSettingsServer::GetSerialNumber(Out<SerialNumber> out_console_serial) {
    LOG_INFO(Service_SET, "called");
    *out_console_serial = []{
        u32 d = ::Settings::values.serial_unit.GetValue();
        SerialNumber c{};
        c.serial_number[0] = 'X';
        c.serial_number[1] = 'A';
        c.serial_number[2] = [] {
            // Adding another setting would be tedious so... let's just reuse region_index :)
            switch (::Settings::values.region_index.GetValue()) {
            case ::Settings::Region::Japan: return 'J';
            case ::Settings::Region::Usa: return 'W';
            case ::Settings::Region::Europe: return 'E';
            case ::Settings::Region::Australia: return 'M'; //pretend its Malaysia
            case ::Settings::Region::China:
            case ::Settings::Region::Taiwan: return 'C';
            case ::Settings::Region::Korea: return 'K';
            default: return 'W';
            }
        }();
        Fill3DS_CRC(d, c.serial_number.data() + 3);
        return c;
    }();
    R_SUCCEED();
}

Result ISystemSettingsServer::GetNfcEnableFlag(Out<bool> out_nfc_enable_flag) {
    LOG_INFO(Service_SET, "called, nfc_enable_flag={}", m_system_settings.nfc_enable_flag);

    *out_nfc_enable_flag = m_system_settings.nfc_enable_flag;
    R_SUCCEED();
}

Result ISystemSettingsServer::SetNfcEnableFlag(bool nfc_enable_flag) {
    LOG_INFO(Service_SET, "called, nfc_enable_flag={}", nfc_enable_flag);

    m_system_settings.nfc_enable_flag = nfc_enable_flag;
    SetSaveNeeded();
    R_SUCCEED();
}

Result ISystemSettingsServer::GetConsoleInformationUploadFlag(Out<bool> out_flag) {
    LOG_INFO(Service_SET, "called {}", m_system_settings.console_information_upload_flag);
    *out_flag = m_system_settings.console_information_upload_flag;
    R_SUCCEED();
}

Result ISystemSettingsServer::SetConsoleInformationUploadFlag(bool flag) {
    LOG_INFO(Service_SET, "called {}", flag);
    m_system_settings.usb_30_enable_flag = flag;
    SetSaveNeeded();
    R_SUCCEED();
}

Result ISystemSettingsServer::GetAutomaticApplicationDownloadFlag(Out<bool> out_flag) {
    LOG_INFO(Service_SET, "called {}", m_system_settings.usb_30_enable_flag);
    *out_flag = m_system_settings.automatic_application_download_flag;
    R_SUCCEED();
}

Result ISystemSettingsServer::SetAutomaticApplicationDownloadFlag(bool flag) {
    LOG_INFO(Service_SET, "called {}", flag);
    m_system_settings.automatic_application_download_flag = flag;
    SetSaveNeeded();
    R_SUCCEED();
}

Result ISystemSettingsServer::GetLdnChannel(Out<s32> out_value) {
    LOG_DEBUG(Service_SET, "GetLdnChannel called");
    *out_value = m_system_settings.ldn_channel;
    R_SUCCEED();
}

Result ISystemSettingsServer::SetLdnChannel(s32 value) {
    LOG_DEBUG(Service_SET, "SetLdnChannel called");
    m_system_settings.ldn_channel = value;
    SetSaveNeeded();
    R_SUCCEED();
}

Result ISystemSettingsServer::GetExternalRtcResetFlag(Out<bool> out_value) {
    LOG_DEBUG(Service_SET, "GetExternalRtcResetFlag called");
    *out_value = m_system_settings.external_rtc_reset_flag;
    R_SUCCEED();
}

Result ISystemSettingsServer::SetExternalRtcResetFlag(bool value) {
    LOG_DEBUG(Service_SET, "SetExternalRtcResetFlag called");
    m_system_settings.external_rtc_reset_flag = value;
    SetSaveNeeded();
    R_SUCCEED();
}

Result ISystemSettingsServer::GetUsbFullKeyEnableFlag(Out<bool> out_value) {
    LOG_DEBUG(Service_SET, "GetUsbFullKeyEnableFlag called");
    *out_value = m_system_settings.usb_full_key_enable_flag;
    R_SUCCEED();
}

Result ISystemSettingsServer::SetUsbFullKeyEnableFlag(bool value) {
    LOG_DEBUG(Service_SET, "SetUsbFullKeyEnableFlag called");
    m_system_settings.usb_full_key_enable_flag = value;
    SetSaveNeeded();
    R_SUCCEED();
}

Result ISystemSettingsServer::GetHeadphoneVolumeWarningCount(Out<s32> out_value) {
    LOG_DEBUG(Service_SET, "GetHeadphoneVolumeWarningCount called");
    *out_value = m_system_settings.headphone_volume_warning_count;
    R_SUCCEED();
}

Result ISystemSettingsServer::SetHeadphoneVolumeWarningCount(s32 value) {
    LOG_DEBUG(Service_SET, "SetHeadphoneVolumeWarningCount called");
    m_system_settings.headphone_volume_warning_count = value;
    SetSaveNeeded();
    R_SUCCEED();
}

Result ISystemSettingsServer::GetBluetoothAfhEnableFlag(Out<bool> out_value) {
    LOG_DEBUG(Service_SET, "GetBluetoothAfhEnableFlag called");
    *out_value = m_system_settings.bluetooth_afh_enable_flag;
    R_SUCCEED();
}

Result ISystemSettingsServer::SetBluetoothAfhEnableFlag(bool value) {
    LOG_DEBUG(Service_SET, "SetBluetoothAfhEnableFlag called");
    m_system_settings.bluetooth_afh_enable_flag = value;
    SetSaveNeeded();
    R_SUCCEED();
}

Result ISystemSettingsServer::GetBluetoothBoostEnableFlag(Out<bool> out_value) {
    LOG_DEBUG(Service_SET, "GetBluetoothBoostEnableFlag called");
    *out_value = m_system_settings.bluetooth_boost_enable_flag;
    R_SUCCEED();
}

Result ISystemSettingsServer::SetBluetoothBoostEnableFlag(bool value) {
    LOG_DEBUG(Service_SET, "SetBluetoothBoostEnableFlag called");
    m_system_settings.bluetooth_boost_enable_flag = value;
    SetSaveNeeded();
    R_SUCCEED();
}

Result ISystemSettingsServer::GetHeadphoneVolumeUpdateFlag(Out<bool> out_value) {
    LOG_DEBUG(Service_SET, "GetHeadphoneVolumeUpdateFlag called");
    *out_value = m_system_settings.heaphone_volume_update_flag;
    R_SUCCEED();
}

Result ISystemSettingsServer::SetHeadphoneVolumeUpdateFlag(bool value) {
    LOG_DEBUG(Service_SET, "SetHeadphoneVolumeUpdateFlag called");
    m_system_settings.heaphone_volume_update_flag = value;
    SetSaveNeeded();
    R_SUCCEED();
}

Result ISystemSettingsServer::GetPctlReadyFlag(Out<bool> out_value) {
    LOG_DEBUG(Service_SET, "GetPctlReadyFlag called");
    *out_value = m_system_settings.pctl_ready_flag;
    R_SUCCEED();
}

Result ISystemSettingsServer::SetPctlReadyFlag(bool value) {
    LOG_DEBUG(Service_SET, "SetPctlReadyFlag called");
    m_system_settings.pctl_ready_flag = value;
    SetSaveNeeded();
    R_SUCCEED();
}

Result ISystemSettingsServer::GetDataDeletionSettings(
    Out<std::array<u8, 0x8>> out_settings) {
    *out_settings = m_system_settings.data_deletion_settings;
    R_SUCCEED();
}

Result ISystemSettingsServer::SetDataDeletionSettings(
    const std::array<u8, 0x8>& settings) {
    m_system_settings.data_deletion_settings = settings;
    SetSaveNeeded();
    R_SUCCEED();
}

Result ISystemSettingsServer::GetInitialSystemAppletProgramId(Out<u64> out_program_id) {
    u64 id{};
    std::memcpy(&id, m_system_settings.initial_system_applet_program_id.data(), sizeof(id));
    *out_program_id = id;
    R_SUCCEED();
}

Result ISystemSettingsServer::GetOverlayDispProgramId(Out<u64> out_program_id) {
    u64 id{};
    std::memcpy(&id, m_system_settings.overlay_disp_program_id.data(), sizeof(id));
    *out_program_id = id;
    R_SUCCEED();
}

Result ISystemSettingsServer::GetPtmBatteryLot(Out<BatteryLot> out_lot) {
    std::memcpy(out_lot->lot_number.data(), m_device_settings.ptm_battery_lot.data(),
                m_device_settings.ptm_battery_lot.size());
    R_SUCCEED();
}

Result ISystemSettingsServer::SetPtmBatteryLot(const BatteryLot& lot) {
    std::memcpy(m_device_settings.ptm_battery_lot.data(), lot.lot_number.data(),
                m_device_settings.ptm_battery_lot.size());
    SetSaveNeeded();
    R_SUCCEED();
}

Result ISystemSettingsServer::GetPtmFuelGaugeParameter(
    Out<std::array<u8, 0x18>> out_parameter) {
    *out_parameter = m_device_settings.ptm_fuel_gauge_parameter;
    R_SUCCEED();
}

Result ISystemSettingsServer::SetPtmFuelGaugeParameter(
    const std::array<u8, 0x18>& parameter) {
    m_device_settings.ptm_fuel_gauge_parameter = parameter;
    SetSaveNeeded();
    R_SUCCEED();
}

Result ISystemSettingsServer::SetShutdownRtcValue(s64 value) {
    m_private_settings.shutdown_rtc_value = value;
    SetSaveNeeded();
    R_SUCCEED();
}

Result ISystemSettingsServer::GetShutdownRtcValue(Out<s64> out_value) {
    *out_value = m_private_settings.shutdown_rtc_value;
    R_SUCCEED();
}

Result ISystemSettingsServer::GetAnalogStickUserCalibrationL(Out<std::array<u8, 0x10>> out_value) {
    *out_value = m_system_settings.analog_stick_user_calibration_left;
    R_SUCCEED();
}

Result ISystemSettingsServer::SetAnalogStickUserCalibrationL(const std::array<u8, 0x10>& value) {
    m_system_settings.analog_stick_user_calibration_left = value;
    SetSaveNeeded();
    R_SUCCEED();
}

Result ISystemSettingsServer::GetAnalogStickUserCalibrationR(Out<std::array<u8, 0x10>> out_value) {
    *out_value = m_system_settings.analog_stick_user_calibration_right;
    R_SUCCEED();
}

Result ISystemSettingsServer::SetAnalogStickUserCalibrationR(const std::array<u8, 0x10>& value) {
    m_system_settings.analog_stick_user_calibration_right = value;
    SetSaveNeeded();
    R_SUCCEED();
}

Result ISystemSettingsServer::GetPtmBatteryVersion(Out<u8> out_value) {
    *out_value = m_device_settings.ptm_battery_version;
    R_SUCCEED();
}

Result ISystemSettingsServer::SetPtmBatteryVersion(u8 value) {
    m_device_settings.ptm_battery_version = value;
    SetSaveNeeded();
    R_SUCCEED();
}

Result ISystemSettingsServer::GetUsb30HostEnableFlag(Out<bool> out_value) {
    *out_value = m_system_settings.usb_30_host_enable_flag;
    R_SUCCEED();
}

Result ISystemSettingsServer::SetUsb30HostEnableFlag(bool value) {
    m_system_settings.usb_30_host_enable_flag = value;
    SetSaveNeeded();
    R_SUCCEED();
}

Result ISystemSettingsServer::GetUsb30DeviceEnableFlag(Out<bool> out_value) {
    *out_value = m_system_settings.usb_30_device_enable_flag;
    R_SUCCEED();
}

Result ISystemSettingsServer::SetUsb30DeviceEnableFlag(bool value) {
    m_system_settings.usb_30_device_enable_flag = value;
    SetSaveNeeded();
    R_SUCCEED();
}

Result ISystemSettingsServer::GetConsoleSixAxisSensorAccelerationBias(Out<Common::Vec<f32, 3>> out_value) {
    *out_value = m_system_settings.console_six_axis_sensor_acceleration_bias;
    R_SUCCEED();
}

Result ISystemSettingsServer::SetConsoleSixAxisSensorAccelerationBias(const Common::Vec<f32, 3>& value) {
    m_system_settings.console_six_axis_sensor_acceleration_bias = value;
    SetSaveNeeded();
    R_SUCCEED();
}

Result ISystemSettingsServer::GetConsoleSixAxisSensorAngularVelocityBias(Out<Common::Vec<f32, 3>> out_value) {
    *out_value = m_system_settings.console_six_axis_sensor_angular_velocity_bias;
    R_SUCCEED();
}

Result ISystemSettingsServer::SetConsoleSixAxisSensorAngularVelocityBias(const Common::Vec<f32, 3>& value) {
    m_system_settings.console_six_axis_sensor_angular_velocity_bias = value;
    SetSaveNeeded();
    R_SUCCEED();
}

Result ISystemSettingsServer::GetConsoleSixAxisSensorAccelerationGain(Out<std::array<u8, 0x24>> out_value) {
    *out_value = m_system_settings.console_six_axis_sensor_acceleration_gain;
    R_SUCCEED();
}

Result ISystemSettingsServer::SetConsoleSixAxisSensorAccelerationGain(const std::array<u8, 0x24>& value) {
    m_system_settings.console_six_axis_sensor_acceleration_gain = value;
    SetSaveNeeded();
    R_SUCCEED();
}

Result ISystemSettingsServer::GetConsoleSixAxisSensorAngularVelocityGain(Out<std::array<u8, 0x24>> out_value) {
    *out_value = m_system_settings.console_six_axis_sensor_angular_velocity_gain;
    R_SUCCEED();
}

Result ISystemSettingsServer::SetConsoleSixAxisSensorAngularVelocityGain(const std::array<u8, 0x24>& value) {
    m_system_settings.console_six_axis_sensor_angular_velocity_gain = value;
    SetSaveNeeded();
    R_SUCCEED();
}

Result ISystemSettingsServer::GetConsoleSixAxisSensorAngularVelocityTimeBias(Out<Common::Vec<f32, 3>> out_value) {
    *out_value = m_system_settings.console_six_axis_sensor_angular_velocity_time_bias;
    R_SUCCEED();
}

Result ISystemSettingsServer::SetConsoleSixAxisSensorAngularVelocityTimeBias(const Common::Vec<f32, 3>& value) {
    m_system_settings.console_six_axis_sensor_angular_velocity_time_bias = value;
    SetSaveNeeded();
    R_SUCCEED();
}

Result ISystemSettingsServer::GetConsoleSixAxisSensorAngularAcceleration(Out<std::array<u8, 0x24>> out_value) {
    *out_value = m_system_settings.console_six_axis_sensor_angular_velocity_acceleration;
    R_SUCCEED();
}

Result ISystemSettingsServer::SetConsoleSixAxisSensorAngularAcceleration(const std::array<u8, 0x24>& value) {
    m_system_settings.console_six_axis_sensor_angular_velocity_acceleration = value;
    SetSaveNeeded();
    R_SUCCEED();
}

Result ISystemSettingsServer::SetBlePairingSettings(
    InArray<std::array<u8, 0x80>, BufferAttr_HipcMapAlias> settings) {
    R_UNLESS(settings.size() <= m_system_settings.ble_pairing_settings.size(), ResultUnknown);
    m_system_settings.ble_pairing_settings_count = static_cast<u32>(settings.size());
    std::memcpy(m_system_settings.ble_pairing_settings.data(), settings.data(),
                settings.size() * sizeof(settings[0]));
    SetSaveNeeded();
    R_SUCCEED();
}

Result ISystemSettingsServer::GetBlePairingSettings(
    Out<s32> out_count,
    OutArray<std::array<u8, 0x80>, BufferAttr_HipcMapAlias> out_settings) {
    const auto count = (std::min)(
        static_cast<std::size_t>(m_system_settings.ble_pairing_settings_count),
        (std::min)(m_system_settings.ble_pairing_settings.size(), out_settings.size()));
    *out_count = static_cast<s32>(count);
    std::memcpy(out_settings.data(), m_system_settings.ble_pairing_settings.data(),
                count * sizeof(out_settings[0]));
    R_SUCCEED();
}

Result ISystemSettingsServer::GetAccountOnlineStorageSettings(
    Out<s32> out_count,
    OutArray<std::array<u8, 0x40>, BufferAttr_HipcMapAlias> out_settings) {
    const auto count = (std::min)(
        static_cast<std::size_t>(m_system_settings.account_online_storage_settings_count),
        (std::min)(m_system_settings.account_online_storage_settings.size(), out_settings.size()));
    *out_count = static_cast<s32>(count);
    std::memcpy(out_settings.data(), m_system_settings.account_online_storage_settings.data(),
                count * sizeof(out_settings[0]));
    R_SUCCEED();
}

Result ISystemSettingsServer::SetAccountOnlineStorageSettings(
    InArray<std::array<u8, 0x40>, BufferAttr_HipcMapAlias> settings) {
    R_UNLESS(settings.size() <= m_system_settings.account_online_storage_settings.size(),
             ResultUnknown);
    m_system_settings.account_online_storage_settings_count = static_cast<u32>(settings.size());
    std::memcpy(m_system_settings.account_online_storage_settings.data(), settings.data(),
                settings.size() * sizeof(settings[0]));
    SetSaveNeeded();
    R_SUCCEED();
}

Result ISystemSettingsServer::GetInRepairProcessEnableFlag(Out<bool> out_value) {
    *out_value = m_appln_settings.in_repair_process_enable_flag;
    R_SUCCEED();
}

Result ISystemSettingsServer::SetInRepairProcessEnableFlag(bool value) {
    m_appln_settings.in_repair_process_enable_flag = value;
    SetSaveNeeded();
    R_SUCCEED();
}

Result ISystemSettingsServer::GetServiceDiscoveryControlSettings(Out<u32> out_value) {
    *out_value = m_appln_settings.service_discovery_control_settings;
    R_SUCCEED();
}

Result ISystemSettingsServer::SetServiceDiscoveryControlSettings(u32 value) {
    m_appln_settings.service_discovery_control_settings = value;
    SetSaveNeeded();
    R_SUCCEED();
}

Result ISystemSettingsServer::GetThemeKey(Out<std::array<u8, 0x10>> out_value) {
    *out_value = m_system_settings.theme_key;
    R_SUCCEED();
}

Result ISystemSettingsServer::SetThemeKey(const std::array<u8, 0x10>& value) {
    m_system_settings.theme_key = value;
    SetSaveNeeded();
    R_SUCCEED();
}

Result ISystemSettingsServer::GetThemeId(Out<std::array<u64, 0x10>> out_value, s32 type) {
    // The HOS ThemeId carries 16 u64s, not a byte-aligned IPC array.
    R_UNLESS(type == 0 || type == 1, ResultUnknown);
    const auto& source = type == 0 ? m_system_settings.theme_id_type0
                                    : m_system_settings.theme_id_type1;
    std::memcpy(out_value->data(), source.data(), source.size());
    R_SUCCEED();
}

Result ISystemSettingsServer::SetThemeId(s32 type, const std::array<u64, 0x10>& value) {
    // Following the s32 selector, this aligned type starts at HIPC raw offset 8.
    R_UNLESS(type == 0 || type == 1, ResultUnknown);
    auto& target = type == 0 ? m_system_settings.theme_id_type0
                             : m_system_settings.theme_id_type1;
    std::memcpy(target.data(), value.data(), target.size());
    SetSaveNeeded();
    R_SUCCEED();
}

Result ISystemSettingsServer::GetRequiresRunRepairTimeReviser(Out<bool> out_value) {
    *out_value = m_system_settings.requires_run_repair_time_reviser;
    R_SUCCEED();
}

Result ISystemSettingsServer::SetRequiresRunRepairTimeReviser(bool value) {
    m_system_settings.requires_run_repair_time_reviser = value;
    SetSaveNeeded();
    R_SUCCEED();
}

Result ISystemSettingsServer::GetHearingProtectionSafeguardFlag(Out<bool> out_value) {
    *out_value = m_system_settings.hearing_protection_safeguard_flag;
    R_SUCCEED();
}

Result ISystemSettingsServer::SetHearingProtectionSafeguardFlag(bool value) {
    m_system_settings.hearing_protection_safeguard_flag = value;
    SetSaveNeeded();
    R_SUCCEED();
}

Result ISystemSettingsServer::GetHearingProtectionSafeguardRemainingTime(Out<s64> out_value) {
    *out_value = m_system_settings.hearing_protection_safeguard_remaining_time;
    R_SUCCEED();
}

Result ISystemSettingsServer::SetHearingProtectionSafeguardRemainingTime(s64 value) {
    m_system_settings.hearing_protection_safeguard_remaining_time = value;
    SetSaveNeeded();
    R_SUCCEED();
}

Result ISystemSettingsServer::GetWebInspectorFlag(Out<bool> out_value) {
    *out_value = m_system_settings.web_inspector_flag;
    R_SUCCEED();
}

Result ISystemSettingsServer::SetChineseTraditionalInputMethod(ChineseTraditionalInputMethod value) {
    m_system_settings.chinese_traditional_input_method = value;
    SetSaveNeeded();
    R_SUCCEED();
}

Result ISystemSettingsServer::GetPtmCycleCountReliability(Out<u32> out_value) {
    *out_value = m_device_settings.ptm_cycle_count_reliability;
    R_SUCCEED();
}

Result ISystemSettingsServer::SetPtmCycleCountReliability(u32 value) {
    m_device_settings.ptm_cycle_count_reliability = value;
    SetSaveNeeded();
    R_SUCCEED();
}

Result ISystemSettingsServer::GetThemeSettings(Out<std::array<u8, 0x8>> out_value) {
    *out_value = m_system_settings.theme_settings;
    R_SUCCEED();
}

Result ISystemSettingsServer::SetThemeSettings(const std::array<u8, 0x8>& value) {
    m_system_settings.theme_settings = value;
    SetSaveNeeded();
    R_SUCCEED();
}

Result ISystemSettingsServer::GetZoomFlag(Out<bool> out_value) {
    *out_value = m_system_settings.zoom_flag;
    R_SUCCEED();
}

Result ISystemSettingsServer::SetZoomFlag(bool value) {
    m_system_settings.zoom_flag = value;
    SetSaveNeeded();
    R_SUCCEED();
}

Result ISystemSettingsServer::GetMemoryUsageRateFlag(Out<bool> out_value) {
    *out_value = m_system_settings.memory_usage_rate_flag;
    R_SUCCEED();
}

Result ISystemSettingsServer::SetFieldTestingFlag(bool value) {
    m_system_settings.field_testing_flag = value;
    SetSaveNeeded();
    R_SUCCEED();
}

Result ISystemSettingsServer::GetUsb30EnableFlag(Out<bool> out_usb30_enable_flag) {
    LOG_INFO(Service_SET, "called, usb30_enable_flag={}", m_system_settings.usb_30_enable_flag);
    *out_usb30_enable_flag = m_system_settings.usb_30_enable_flag;
    R_SUCCEED();
}

Result ISystemSettingsServer::SetUsb30EnableFlag(bool usb30_enable_flag) {
    LOG_INFO(Service_SET, "called, usb30_enable_flag={}", usb30_enable_flag);
    m_system_settings.usb_30_enable_flag = usb30_enable_flag;
    SetSaveNeeded();
    R_SUCCEED();
}

Result ISystemSettingsServer::GetSleepSettings(Out<SleepSettings> out_sleep_settings) {
    LOG_INFO(Service_SET, "called, flags={}, handheld_sleep_plan={}, console_sleep_plan={}",
             m_system_settings.sleep_settings.flags.raw,
             m_system_settings.sleep_settings.handheld_sleep_plan,
             m_system_settings.sleep_settings.console_sleep_plan);

    *out_sleep_settings = m_system_settings.sleep_settings;
    R_SUCCEED();
}

Result ISystemSettingsServer::SetSleepSettings(SleepSettings sleep_settings) {
    LOG_INFO(Service_SET, "called, flags={}, handheld_sleep_plan={}, console_sleep_plan={}",
             sleep_settings.flags.raw, sleep_settings.handheld_sleep_plan,
             sleep_settings.console_sleep_plan);

    m_system_settings.sleep_settings = sleep_settings;
    SetSaveNeeded();
    R_SUCCEED();
}

Result ISystemSettingsServer::GetWirelessLanEnableFlag(Out<bool> out_wireless_lan_enable_flag) {
    LOG_INFO(Service_SET, "called, wireless_lan_enable_flag={}",
             m_system_settings.wireless_lan_enable_flag);

    *out_wireless_lan_enable_flag = m_system_settings.wireless_lan_enable_flag;
    R_SUCCEED();
}

Result ISystemSettingsServer::SetWirelessLanEnableFlag(bool wireless_lan_enable_flag) {
    LOG_INFO(Service_SET, "called, wireless_lan_enable_flag={}", wireless_lan_enable_flag);

    m_system_settings.wireless_lan_enable_flag = wireless_lan_enable_flag;
    SetSaveNeeded();
    R_SUCCEED();
}

Result ISystemSettingsServer::GetInitialLaunchSettings(
    Out<InitialLaunchSettings> out_initial_launch_settings) {
    LOG_INFO(Service_SET, "called, flags={}, timestamp={}",
             m_system_settings.initial_launch_settings_packed.flags.raw,
             m_system_settings.initial_launch_settings_packed.timestamp.time_point);

    *out_initial_launch_settings = {
        .flags = m_system_settings.initial_launch_settings_packed.flags,
        .timestamp = m_system_settings.initial_launch_settings_packed.timestamp,
    };
    R_SUCCEED();
}

Result ISystemSettingsServer::SetInitialLaunchSettings(
    InitialLaunchSettings initial_launch_settings) {
    LOG_INFO(Service_SET, "called, flags={}, timestamp={}", initial_launch_settings.flags.raw,
             initial_launch_settings.timestamp.time_point);

    m_system_settings.initial_launch_settings_packed.flags = initial_launch_settings.flags;
    m_system_settings.initial_launch_settings_packed.timestamp = initial_launch_settings.timestamp;
    SetSaveNeeded();
    R_SUCCEED();
}

Result ISystemSettingsServer::GetDeviceNickName(
    OutLargeData<std::array<u8, 0x80>, BufferAttr_HipcMapAlias> out_device_name) {
    LOG_DEBUG(Service_SET, "called");

    *out_device_name = {};
    const auto device_name_buffer = ::Settings::values.device_name.GetValue().c_str();
    memcpy(out_device_name->data(), device_name_buffer,
           ::Settings::values.device_name.GetValue().size());

    R_SUCCEED();
}

Result ISystemSettingsServer::SetDeviceNickName(
    InLargeData<std::array<u8, 0x80>, BufferAttr_HipcMapAlias> device_name_buffer) {
    const std::string device_name = Common::StringFromBuffer(*device_name_buffer);

    LOG_INFO(Service_SET, "called, device_name={}", device_name);

    ::Settings::values.device_name = device_name;
    R_SUCCEED();
}

Result ISystemSettingsServer::GetProductModel(Out<u32> out_product_model) {
    // Encore presents the guest with a stable retail Switch-compatible product model.
    constexpr u32 product_model = 1;
    LOG_DEBUG(Service_SET, "Returning synthetic product_model={}", product_model);
    *out_product_model = product_model;
    R_SUCCEED();
}

Result ISystemSettingsServer::GetBluetoothEnableFlag(Out<bool> out_bluetooth_enable_flag) {
    LOG_INFO(Service_SET, "called, bluetooth_enable_flag={}",
             m_system_settings.bluetooth_enable_flag);

    *out_bluetooth_enable_flag = m_system_settings.bluetooth_enable_flag;
    R_SUCCEED();
}

Result ISystemSettingsServer::SetBluetoothEnableFlag(bool bluetooth_enable_flag) {
    LOG_INFO(Service_SET, "called, bluetooth_enable_flag={}", bluetooth_enable_flag);

    m_system_settings.bluetooth_enable_flag = bluetooth_enable_flag;
    SetSaveNeeded();
    R_SUCCEED();
}

Result ISystemSettingsServer::GetMiiAuthorId(Out<Common::UUID> out_mii_author_id) {
    if (m_system_settings.mii_author_id.IsInvalid()) {
        m_system_settings.mii_author_id = Common::UUID::MakeDefault();
        SetSaveNeeded();
    }

    LOG_INFO(Service_SET, "called, author_id={}",
             m_system_settings.mii_author_id.FormattedString());

    *out_mii_author_id = m_system_settings.mii_author_id;
    R_SUCCEED();
}

Result ISystemSettingsServer::GetAutoUpdateEnableFlag(Out<bool> out_auto_update_enable_flag) {
    LOG_INFO(Service_SET, "called, auto_update_flag={}", m_system_settings.auto_update_enable_flag);

    *out_auto_update_enable_flag = m_system_settings.auto_update_enable_flag;
    R_SUCCEED();
}

Result ISystemSettingsServer::SetAutoUpdateEnableFlag(bool auto_update_enable_flag) {
    LOG_INFO(Service_SET, "called, auto_update_flag={}", auto_update_enable_flag);

    m_system_settings.auto_update_enable_flag = auto_update_enable_flag;
    SetSaveNeeded();
    R_SUCCEED();
}

Result ISystemSettingsServer::GetBatteryPercentageFlag(Out<bool> out_battery_percentage_flag) {
    LOG_DEBUG(Service_SET, "called, battery_percentage_flag={}",
              m_system_settings.battery_percentage_flag);

    *out_battery_percentage_flag = m_system_settings.battery_percentage_flag;
    R_SUCCEED();
}

Result ISystemSettingsServer::SetBatteryPercentageFlag(bool battery_percentage_flag) {
    LOG_INFO(Service_SET, "called, battery_percentage_flag={}", battery_percentage_flag);

    m_system_settings.battery_percentage_flag = battery_percentage_flag;
    SetSaveNeeded();
    R_SUCCEED();
}

Result ISystemSettingsServer::SetExternalSteadyClockInternalOffset(s64 offset) {
    LOG_DEBUG(Service_SET, "called, external_steady_clock_internal_offset={}", offset);

    m_private_settings.external_steady_clock_internal_offset = offset;
    SetSaveNeeded();
    R_SUCCEED();
}

Result ISystemSettingsServer::GetExternalSteadyClockInternalOffset(Out<s64> out_offset) {
    LOG_DEBUG(Service_SET, "called, external_steady_clock_internal_offset={}",
              m_private_settings.external_steady_clock_internal_offset);

    *out_offset = m_private_settings.external_steady_clock_internal_offset;
    R_SUCCEED();
}

Result ISystemSettingsServer::GetPushNotificationActivityModeOnSleep(
    Out<s32> out_push_notification_activity_mode_on_sleep) {
    LOG_INFO(Service_SET, "called, push_notification_activity_mode_on_sleep={}",
             m_system_settings.push_notification_activity_mode_on_sleep);

    *out_push_notification_activity_mode_on_sleep =
        m_system_settings.push_notification_activity_mode_on_sleep;
    R_SUCCEED();
}

Result ISystemSettingsServer::SetPushNotificationActivityModeOnSleep(
    s32 push_notification_activity_mode_on_sleep) {
    LOG_INFO(Service_SET, "called, push_notification_activity_mode_on_sleep={}",
             push_notification_activity_mode_on_sleep);

    m_system_settings.push_notification_activity_mode_on_sleep =
        push_notification_activity_mode_on_sleep;
    SetSaveNeeded();
    R_SUCCEED();
}

Result ISystemSettingsServer::GetErrorReportSharePermission(
    Out<ErrorReportSharePermission> out_error_report_share_permission) {
    LOG_INFO(Service_SET, "called, error_report_share_permission={}",
             m_system_settings.error_report_share_permission);

    *out_error_report_share_permission = m_system_settings.error_report_share_permission;
    R_SUCCEED();
}

Result ISystemSettingsServer::SetErrorReportSharePermission(
    ErrorReportSharePermission error_report_share_permission) {
    LOG_INFO(Service_SET, "called, error_report_share_permission={}",
             error_report_share_permission);

    m_system_settings.error_report_share_permission = error_report_share_permission;
    SetSaveNeeded();
    R_SUCCEED();
}

Result ISystemSettingsServer::GetAppletLaunchFlags(Out<u32> out_applet_launch_flag) {
    LOG_INFO(Service_SET, "called, applet_launch_flag={}", m_system_settings.applet_launch_flag);

    *out_applet_launch_flag = m_system_settings.applet_launch_flag;
    R_SUCCEED();
}

Result ISystemSettingsServer::SetAppletLaunchFlags(u32 applet_launch_flag) {
    LOG_INFO(Service_SET, "called, applet_launch_flag={}", applet_launch_flag);

    m_system_settings.applet_launch_flag = applet_launch_flag;
    SetSaveNeeded();
    R_SUCCEED();
}

Result ISystemSettingsServer::GetKeyboardLayout(Out<KeyboardLayout> out_keyboard_layout) {
    LOG_INFO(Service_SET, "called, keyboard_layout={}", m_system_settings.keyboard_layout);

    *out_keyboard_layout = m_system_settings.keyboard_layout;
    R_SUCCEED();
}

Result ISystemSettingsServer::SetKeyboardLayout(KeyboardLayout keyboard_layout) {
    LOG_INFO(Service_SET, "called, keyboard_layout={}", keyboard_layout);

    m_system_settings.keyboard_layout = keyboard_layout;
    SetSaveNeeded();
    R_SUCCEED();
}

Result ISystemSettingsServer::GetDeviceTimeZoneLocationUpdatedTime(
    Out<Service::PSC::Time::SteadyClockTimePoint> out_time_point) {
    LOG_INFO(Service_SET, "called");

    *out_time_point = m_system_settings.device_time_zone_location_updated_time;
    R_SUCCEED();
}

Result ISystemSettingsServer::SetDeviceTimeZoneLocationUpdatedTime(
    const Service::PSC::Time::SteadyClockTimePoint& time_point) {
    LOG_INFO(Service_SET, "called");

    m_system_settings.device_time_zone_location_updated_time = time_point;
    SetSaveNeeded();
    R_SUCCEED();
}

Result ISystemSettingsServer::GetUserSystemClockAutomaticCorrectionUpdatedTime(
    Out<Service::PSC::Time::SteadyClockTimePoint> out_time_point) {
    LOG_INFO(Service_SET, "called");

    *out_time_point = m_system_settings.user_system_clock_automatic_correction_updated_time_point;
    R_SUCCEED();
}

Result ISystemSettingsServer::SetUserSystemClockAutomaticCorrectionUpdatedTime(
    const Service::PSC::Time::SteadyClockTimePoint& out_time_point) {
    LOG_INFO(Service_SET, "called");

    m_system_settings.user_system_clock_automatic_correction_updated_time_point = out_time_point;
    SetSaveNeeded();
    R_SUCCEED();
}

Result ISystemSettingsServer::GetChineseTraditionalInputMethod(
    Out<ChineseTraditionalInputMethod> out_chinese_traditional_input_method) {
    LOG_INFO(Service_SET, "called, chinese_traditional_input_method={}",
             m_system_settings.chinese_traditional_input_method);

    *out_chinese_traditional_input_method = m_system_settings.chinese_traditional_input_method;
    R_SUCCEED();
}

Result ISystemSettingsServer::GetHomeMenuScheme(Out<HomeMenuScheme> out_home_menu_scheme) {
    // No Horizon HOME-menu theme database is emulated; provide one stable default palette.
    *out_home_menu_scheme = {
        .main = 0xFF323232,
        .back = 0xFF323232,
        .sub = 0xFFFFFFFF,
        .bezel = 0xFFFFFFFF,
        .extra = 0xFF000000,
    };
    LOG_DEBUG(Service_SET, "Returning synthetic default HOME-menu scheme");
    R_SUCCEED();
}

Result ISystemSettingsServer::GetPlatformRegion(Out<PlatformRegion> out_platform_region) {
    const auto stored = static_cast<PlatformRegion>(m_private_settings.platform_region);
    if (stored != PlatformRegion::Global && stored != PlatformRegion::Terra) {
        m_private_settings.platform_region = static_cast<s32>(PlatformRegion::Global);
        SetSaveNeeded();
    }
    *out_platform_region = static_cast<PlatformRegion>(m_private_settings.platform_region);
    LOG_DEBUG(Service_SET, "called, platform_region={}", *out_platform_region);
    R_SUCCEED();
}

Result ISystemSettingsServer::SetPlatformRegion(PlatformRegion platform_region) {
    if (platform_region != PlatformRegion::Global && platform_region != PlatformRegion::Terra) {
        platform_region = PlatformRegion::Global;
    }
    m_private_settings.platform_region = static_cast<s32>(platform_region);
    SetSaveNeeded();
    LOG_DEBUG(Service_SET, "called, platform_region={}", platform_region);
    R_SUCCEED();
}

Result ISystemSettingsServer::GetHomeMenuSchemeModel(Out<u32> out_home_menu_scheme_model) {
    // Mirrors the synthetic default scheme above; no model-specific HOME theme is exposed.
    *out_home_menu_scheme_model = 0;
    LOG_DEBUG(Service_SET, "Returning default HOME-menu scheme model=0");
    R_SUCCEED();
}

Result ISystemSettingsServer::GetTouchScreenMode(Out<TouchScreenMode> out_touch_screen_mode) {
    LOG_INFO(Service_SET, "called, touch_screen_mode={}", m_system_settings.touch_screen_mode);

    *out_touch_screen_mode = m_system_settings.touch_screen_mode;
    R_SUCCEED();
}

Result ISystemSettingsServer::SetTouchScreenMode(TouchScreenMode touch_screen_mode) {
    LOG_INFO(Service_SET, "called, touch_screen_mode={}", touch_screen_mode);

    m_system_settings.touch_screen_mode = touch_screen_mode;
    SetSaveNeeded();
    R_SUCCEED();
}

Result ISystemSettingsServer::GetFieldTestingFlag(Out<bool> out_field_testing_flag) {
    LOG_INFO(Service_SET, "called, field_testing_flag={}", m_system_settings.field_testing_flag);

    *out_field_testing_flag = m_system_settings.field_testing_flag;
    R_SUCCEED();
}

Result ISystemSettingsServer::GetPanelCrcMode(Out<s32> out_panel_crc_mode) {
    LOG_INFO(Service_SET, "called, panel_crc_mode={}", m_system_settings.panel_crc_mode);

    *out_panel_crc_mode = m_system_settings.panel_crc_mode;
    R_SUCCEED();
}

Result ISystemSettingsServer::SetPanelCrcMode(s32 panel_crc_mode) {
    LOG_INFO(Service_SET, "called, panel_crc_mode={}", panel_crc_mode);

    m_system_settings.panel_crc_mode = panel_crc_mode;
    SetSaveNeeded();
    R_SUCCEED();
}

Result ISystemSettingsServer::GetHttpAuthConfigs(Out<s32> out_count, OutBuffer<BufferAttr_HipcMapAlias> out_configs) {
    LOG_WARNING(Service_SET, "(STUBBED) called, buffer_size={}", out_configs.size());
    *out_count = 0;
    R_SUCCEED();
}

Result ISystemSettingsServer::GetAccountUserSettings(
    Out<u32> out_count,
    OutLargeData<AccountUserSettings, BufferAttr_HipcMapAlias> out_settings) {
    LOG_WARNING(Service_SET, "(STUBBED) called");

    *out_count = 0;
    *out_settings = {};
    R_SUCCEED();
}

Result ISystemSettingsServer::GetDefaultAccountUserSettings(Out<AccountUserSettings> out_settings) {
    LOG_WARNING(Service_SET, "(STUBBED) called");

    *out_settings = {};
    R_SUCCEED();
}

void ISystemSettingsServer::SetupSettings() {
    auto system_dir = Common::FS::GetEdenPath(Common::FS::EdenPath::NANDDir) / "system/save/8000000000000050";
    if (!LoadSettingsFile(system_dir, []() { return DefaultSystemSettings(); })) {
        ASSERT(false);
    }

    auto private_dir = Common::FS::GetEdenPath(Common::FS::EdenPath::NANDDir) / "system/save/8000000000000052";
    if (!LoadSettingsFile(private_dir, []() { return DefaultPrivateSettings(); })) {
        ASSERT(false);
    }

    auto device_dir = Common::FS::GetEdenPath(Common::FS::EdenPath::NANDDir) / "system/save/8000000000000053";
    if (!LoadSettingsFile(device_dir, []() { return DefaultDeviceSettings(); })) {
        ASSERT(false);
    }

    auto appln_dir = Common::FS::GetEdenPath(Common::FS::EdenPath::NANDDir) / "system/save/8000000000000054";
    if (!LoadSettingsFile(appln_dir, []() { return DefaultApplnSettings(); })) {
        ASSERT(false);
    }
}

void ISystemSettingsServer::StoreSettings() {
    auto system_dir = Common::FS::GetEdenPath(Common::FS::EdenPath::NANDDir) / "system/save/8000000000000050";
    if (!StoreSettingsFile(system_dir, m_system_settings)) {
        LOG_ERROR(Service_SET, "Failed to store System settings");
    }

    auto private_dir = Common::FS::GetEdenPath(Common::FS::EdenPath::NANDDir) / "system/save/8000000000000052";
    if (!StoreSettingsFile(private_dir, m_private_settings)) {
        LOG_ERROR(Service_SET, "Failed to store Private settings");
    }

    auto device_dir = Common::FS::GetEdenPath(Common::FS::EdenPath::NANDDir) / "system/save/8000000000000053";
    if (!StoreSettingsFile(device_dir, m_device_settings)) {
        LOG_ERROR(Service_SET, "Failed to store Device settings");
    }

    auto appln_dir = Common::FS::GetEdenPath(Common::FS::EdenPath::NANDDir) / "system/save/8000000000000054";
    if (!StoreSettingsFile(appln_dir, m_appln_settings)) {
        LOG_ERROR(Service_SET, "Failed to store ApplLn settings");
    }
}

void ISystemSettingsServer::SetSaveNeeded() {
    std::scoped_lock l{m_save_needed_mutex};
    StoreSettings();
}

Result ISystemSettingsServer::GetSettingsItemValueImpl(std::span<u8> out_value, u64& out_size,
                                                       const std::string& category,
                                                       const std::string& name) {
    auto settings{GetSettings()};
    R_UNLESS(settings.contains(category) && settings[category].contains(name), ResultUnknown);

    ASSERT_MSG(out_value.size() >= settings[category][name].size(),
               "Stored type is bigger than requested type");
    out_size = std::min<u64>(settings[category][name].size(), out_value.size());
    std::memcpy(out_value.data(), settings[category][name].data(), out_size);
    R_SUCCEED();
}

} // namespace Service::Set
