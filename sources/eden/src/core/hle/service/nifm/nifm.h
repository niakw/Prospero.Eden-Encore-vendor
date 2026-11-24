// SPDX-FileCopyrightText: Copyright 2025 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

// SPDX-FileCopyrightText: Copyright 2018 yuzu Emulator
// Project// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <atomic>

#include "core/hle/service/service.h"

namespace Core {
class System;
}

namespace Service::NIFM {

class IScanRequest;
class IRequest;

void LoopProcess(Core::System& system);

class IGeneralService final : public ServiceFramework<IGeneralService> {
public:
    explicit IGeneralService(Core::System& system_);
    ~IGeneralService() override;

private:
    void GetClientId(HLERequestContext& ctx);
    void CreateScanRequest(HLERequestContext& ctx);
    void CreateRequest(HLERequestContext& ctx);
    void GetCurrentNetworkProfile(HLERequestContext& ctx);
    void EnumerateNetworkInterfaces(HLERequestContext& ctx);
    void EnumerateNetworkProfiles(HLERequestContext& ctx);
    void GetNetworkProfile(HLERequestContext& ctx);
    void SetNetworkProfile(HLERequestContext& ctx);
    void RemoveNetworkProfile(HLERequestContext& ctx);
    void GetScanData(HLERequestContext& ctx);
    void GetCurrentIpAddress(HLERequestContext& ctx);
    void CreateTemporaryNetworkProfile(HLERequestContext& ctx);
    void GetCurrentIpConfigInfo(HLERequestContext& ctx);
    void SetWirelessCommunicationEnabled(HLERequestContext& ctx);
    void IsWirelessCommunicationEnabled(HLERequestContext& ctx);
    void GetInternetConnectionStatus(HLERequestContext& ctx);
    void SetEthernetCommunicationEnabled(HLERequestContext& ctx);
    void IsEthernetCommunicationEnabled(HLERequestContext& ctx);
    void IsWiredConnectionAvailable(HLERequestContext& ctx);
    void IsNetworkEmulationFeatureEnabled(HLERequestContext& ctx);
    void SelectActiveNetworkEmulationProfileIdForDebug(HLERequestContext& ctx);
    void IsRewriteFeatureEnabled(HLERequestContext& ctx);
    void CreateRewriteRule(HLERequestContext& ctx);
    void DestroyRewriteRule(HLERequestContext& ctx);
    void IsAnyInternetRequestAccepted(HLERequestContext& ctx);
    void IsAnyForegroundRequestAccepted(HLERequestContext& ctx);
    void PutToSleep(HLERequestContext& ctx);
    void WakeUp(HLERequestContext& ctx);
    void GetSsidListVersion(HLERequestContext& ctx);
    void SetExclusiveClient(HLERequestContext& ctx);
    void GetDefaultIpSetting(HLERequestContext& ctx);
    void SetDefaultIpSetting(HLERequestContext& ctx);
    void GetScanDataV2(HLERequestContext& ctx);
    void ConfirmSystemAvailability(HLERequestContext& ctx);
    void SetBackgroundRequestEnabled(HLERequestContext& ctx);
    void GetCurrentAccessPointOld(HLERequestContext& ctx);
    void GetCurrentAccessPoint(HLERequestContext& ctx);
    void Shutdown(HLERequestContext& ctx);
    void SetAcceptableNetworkTypeFlag(HLERequestContext& ctx);
    void GetAcceptableNetworkTypeFlag(HLERequestContext& ctx);
    void NotifyConnectionStateChanged(HLERequestContext& ctx);
    void SetWowlDelayedWakeTime(HLERequestContext& ctx);
    void GetScanDataV3(HLERequestContext& ctx);

    u32 client_id{};
    std::atomic<u32> exclusive_client_id{0};
    std::atomic<u32> acceptable_network_type_flag{0};
    std::atomic<s32> wowl_delayed_wake_time{0};
};

} // namespace Service::NIFM
