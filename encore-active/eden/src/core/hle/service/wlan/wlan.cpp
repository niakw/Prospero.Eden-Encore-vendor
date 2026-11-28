// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#include "core/core.h"
#include "core/hle/result.h"
#include "core/hle/service/wlan/wlan.h"
#include "core/hle/service/ipc_helpers.h"
#include "core/hle/service/cmif_serialization.h"
#include "core/hle/service/cmif_types.h"
#include "core/hle/service/service.h"
#include "core/hle/service/server_manager.h"
#include "frontend_common/firmware_manager.h"

namespace Service::WLAN {

class ILocalManager final : public ServiceFramework<ILocalManager> {
public:
    explicit ILocalManager(Core::System& system_)
        : ServiceFramework{system_, "wlan:lcl"}
    {
        static const FunctionInfo functions[] = {
            { 0, &ILocalManager::RejectLegacyWlanCommand, "OpenMasterMode" },
            { 0, &ILocalManager::RejectLegacyWlanCommand, "OpenMode_2" },
            { 1, &ILocalManager::RejectLegacyWlanCommand, "CloseMasterMode" },
            { 1, &ILocalManager::RejectLegacyWlanCommand, "CloseMode_2" },
            { 2, &ILocalManager::RejectLegacyWlanCommand, "OpenClientMode" },
            { 2, &ILocalManager::RejectLegacyWlanCommand, "GetMacAddress_2" },
            { 3, &ILocalManager::RejectLegacyWlanCommand, "CloseClientMode" },
            { 3, &ILocalManager::RejectLegacyWlanCommand, "CreateBss" },
            { 4, &ILocalManager::RejectLegacyWlanCommand, "OpenSpectatorMode" },
            { 4, &ILocalManager::RejectLegacyWlanCommand, "DestroyBss" },
            { 5, &ILocalManager::RejectLegacyWlanCommand, "CloseSpectatorMode" },
            { 5, &ILocalManager::RejectLegacyWlanCommand, "StartScan_2" },
            { 6, &ILocalManager::RejectLegacyWlanCommand, "GetMacAddress_2" },
            { 6, &ILocalManager::RejectLegacyWlanCommand, "StopScan_2" },
            { 7, &ILocalManager::RejectLegacyWlanCommand, "CreateBss" },
            { 7, &ILocalManager::RejectLegacyWlanCommand, "Connect_2" },
            { 8, &ILocalManager::RejectLegacyWlanCommand, "DestroyBss" },
            { 8, &ILocalManager::RejectLegacyWlanCommand, "CancelConnect_2" },
            { 9, &ILocalManager::RejectLegacyWlanCommand, "StartScan_2" },
            { 9, &ILocalManager::RejectLegacyWlanCommand, "Join" },
            { 10, &ILocalManager::RejectLegacyWlanCommand, "StopScan_2" },
            { 10, &ILocalManager::RejectLegacyWlanCommand, "CancelJoin" },
            { 11, &ILocalManager::RejectLegacyWlanCommand, "Connect_2" },
            { 11, &ILocalManager::RejectLegacyWlanCommand, "Disconnect_2" },
            { 12, &ILocalManager::RejectLegacyWlanCommand, "CancelConnect_2" },
            { 12, &ILocalManager::RejectLegacyWlanCommand, "SetBeaconLostCount" },
            { 13, &ILocalManager::RejectLegacyWlanCommand, "Join" },
            { 13, &ILocalManager::RejectLegacyWlanCommand, "GetSystemEvent_2" },
            { 14, &ILocalManager::RejectLegacyWlanCommand, "CancelJoin" },
            { 14, &ILocalManager::RejectLegacyWlanCommand, "GetConnectionStatus_2" },
            { 15, &ILocalManager::RejectLegacyWlanCommand, "Disconnect_2" },
            { 15, &ILocalManager::RejectLegacyWlanCommand, "GetClientStatus" },
            { 16, &ILocalManager::RejectLegacyWlanCommand, "SetBeaconLostCount" },
            { 16, &ILocalManager::RejectLegacyWlanCommand, "GetBssIndicationEvent" },
            { 17, &ILocalManager::RejectLegacyWlanCommand, "GetSystemEvent_2" },
            { 17, &ILocalManager::RejectLegacyWlanCommand, "GetBssIndicationInfo" },
            { 18, &ILocalManager::RejectLegacyWlanCommand, "GetConnectionStatus_2" },
            { 18, &ILocalManager::RejectLegacyWlanCommand, "GetState_2" },
            { 19, &ILocalManager::RejectLegacyWlanCommand, "GetClientStatus" },
            { 19, &ILocalManager::RejectLegacyWlanCommand, "GetAllowedChannels" },
            { 20, &ILocalManager::RejectLegacyWlanCommand, "GetBssIndicationEvent" },
            { 20, &ILocalManager::RejectLegacyWlanCommand, "AddIe" },
            { 21, &ILocalManager::RejectLegacyWlanCommand, "GetBssIndicationInfo" },
            { 21, &ILocalManager::RejectLegacyWlanCommand, "DeleteIe" },
            { 22, &ILocalManager::RejectLegacyWlanCommand, "GetState_2" },
            { 22, &ILocalManager::RejectLegacyWlanCommand, "PutFrameRaw" },
            { 23, &ILocalManager::RejectLegacyWlanCommand, "GetAllowedChannels" },
            { 23, &ILocalManager::RejectLegacyWlanCommand, "CancelGetFrame" },
            { 24, &ILocalManager::RejectLegacyWlanCommand, "AddIe" },
            { 24, &ILocalManager::RejectLegacyWlanCommand, "CreateRxEntry" },
            { 25, &ILocalManager::RejectLegacyWlanCommand, "DeleteIe" },
            { 25, &ILocalManager::RejectLegacyWlanCommand, "DeleteRxEntry" },
            { 26, &ILocalManager::RejectLegacyWlanCommand, "PutFrameRaw" },
            { 26, &ILocalManager::RejectLegacyWlanCommand, "AddEthertypeToRxEntry" },
            { 27, &ILocalManager::RejectLegacyWlanCommand, "CancelGetFrame" },
            { 27, &ILocalManager::RejectLegacyWlanCommand, "DeleteEthertypeFromRxEntry" },
            { 28, &ILocalManager::RejectLegacyWlanCommand, "CreateRxEntry" },
            { 28, &ILocalManager::RejectLegacyWlanCommand, "AddMatchingDataToRxEntry" },
            { 29, &ILocalManager::RejectLegacyWlanCommand, "DeleteRxEntry" },
            { 29, &ILocalManager::RejectLegacyWlanCommand, "RemoveMatchingDataFromRxEntry" },
            { 30, &ILocalManager::RejectLegacyWlanCommand, "AddEthertypeToRxEntry" },
            { 30, &ILocalManager::RejectLegacyWlanCommand, "GetScanResult_2" },
            { 31, &ILocalManager::RejectLegacyWlanCommand, "DeleteEthertypeFromRxEntry" },
            { 31, &ILocalManager::RejectLegacyWlanCommand, "PutActionFrameOneShot" },
            { 32, &ILocalManager::RejectLegacyWlanCommand, "AddMatchingDataToRxEntry" },
            { 32, &ILocalManager::RejectLegacyWlanCommand, "SetActionFrameWithBeacon" },
            { 33, &ILocalManager::RejectLegacyWlanCommand, "RemoveMatchingDataFromRxEntry" },
            { 33, &ILocalManager::RejectLegacyWlanCommand, "CancelActionFrameWithBeacon" },
            { 34, &ILocalManager::RejectLegacyWlanCommand, "GetScanResult_2" },
            { 34, &ILocalManager::RejectLegacyWlanCommand, "CreateRxEntryForActionFrame" },
            { 35, &ILocalManager::RejectLegacyWlanCommand, "PutActionFrameOneShot" },
            { 35, &ILocalManager::RejectLegacyWlanCommand, "DeleteRxEntryForActionFrame" },
            { 36, &ILocalManager::RejectLegacyWlanCommand, "SetActionFrameWithBeacon" },
            { 36, &ILocalManager::RejectLegacyWlanCommand, "AddSubtypeToRxEntryForActionFrame" },
            { 37, &ILocalManager::RejectLegacyWlanCommand, "CancelActionFrameWithBeacon" },
            { 37, &ILocalManager::RejectLegacyWlanCommand, "DeleteSubtypeFromRxEntryForActionFrame" },
            { 38, &ILocalManager::RejectLegacyWlanCommand, "CreateRxEntryForActionFrame" },
            { 38, &ILocalManager::RejectLegacyWlanCommand, "CancelGetActionFrame" },
            { 39, &ILocalManager::RejectLegacyWlanCommand, "DeleteRxEntryForActionFrame" },
            { 39, &ILocalManager::RejectLegacyWlanCommand, "GetRssi_2" },
            { 40, &ILocalManager::RejectLegacyWlanCommand, "AddSubtypeToRxEntryForActionFrame" },
            { 40, &ILocalManager::RejectLegacyWlanCommand, "SetMaxAssociationNumber" },
            { 41, &ILocalManager::RejectLegacyWlanCommand, "DeleteSubtypeFromRxEntryForActionFrame" },
            { 41, &ILocalManager::RejectLegacyWlanCommand, "Cmd41" },
            { 42, &ILocalManager::RejectLegacyWlanCommand, "CancelGetActionFrame" },
            { 42, &ILocalManager::RejectLegacyWlanCommand, "Cmd42" },
            { 43, &ILocalManager::RejectLegacyWlanCommand, "GetRssi_2" },
            { 43, &ILocalManager::RejectLegacyWlanCommand, "Cmd43" },
            { 44, &ILocalManager::RejectLegacyWlanCommand, "SetMaxAssociationNumber" },
            { 45, &ILocalManager::RejectLegacyWlanCommand, "OpenLcsMasterMode" },
            { 46, &ILocalManager::RejectLegacyWlanCommand, "CloseLcsMasterMode" },
            { 47, &ILocalManager::RejectLegacyWlanCommand, "OpenLcsClientMode" },
            { 48, &ILocalManager::RejectLegacyWlanCommand, "CloseLcsClientMode" },
            { 49, &ILocalManager::RejectLegacyWlanCommand, "GetChannelStats" },
            { 50, &ILocalManager::RejectLegacyWlanCommand, "Cmd50" },
            { 51, &ILocalManager::RejectLegacyWlanCommand, "Cmd51" },
            { 52, &ILocalManager::RejectLegacyWlanCommand, "Cmd52" },
        };
        RegisterHandlers(functions);
    }
private:
    void RejectLegacyWlanCommand(HLERequestContext& ctx) {
        // These interfaces are registered only before HOS 15. Modern HOS uses
        // the wlan creator/service path below; no legacy Switch radio backend exists.
        IPC::ResponseBuilder rb{ctx, 2};
        rb.Push(IPC::ResultNotSupported);
    }

};

class ILocalGetFrame final : public ServiceFramework<ILocalGetFrame> {
public:
    explicit ILocalGetFrame(Core::System& system_)
        : ServiceFramework{system_, "wlan:lg"}
    {
        static const FunctionInfo functions[] = {
            { 0, &ILocalGetFrame::RejectLegacyWlanCommand, "GetFrameRaw" },
        };
        RegisterHandlers(functions);
    }
private:
    void RejectLegacyWlanCommand(HLERequestContext& ctx) {
        // These interfaces are registered only before HOS 15. Modern HOS uses
        // the wlan creator/service path below; no legacy Switch radio backend exists.
        IPC::ResponseBuilder rb{ctx, 2};
        rb.Push(IPC::ResultNotSupported);
    }

};

class ILocalGetActionFrame final : public ServiceFramework<ILocalGetActionFrame> {
public:
    explicit ILocalGetActionFrame(Core::System& system_)
        : ServiceFramework{system_, "wlan:lga"}
    {
        static const FunctionInfo functions[] = {
            { 0, &ILocalGetActionFrame::RejectLegacyWlanCommand, "GetActionFrame" },
        };
        RegisterHandlers(functions);
    }
private:
    void RejectLegacyWlanCommand(HLERequestContext& ctx) {
        // These interfaces are registered only before HOS 15. Modern HOS uses
        // the wlan creator/service path below; no legacy Switch radio backend exists.
        IPC::ResponseBuilder rb{ctx, 2};
        rb.Push(IPC::ResultNotSupported);
    }

};

class ISocketGetFrame final : public ServiceFramework<ISocketGetFrame> {
public:
    explicit ISocketGetFrame(Core::System& system_)
        : ServiceFramework{system_, "wlan:sg"}
    {
        static const FunctionInfo functions[] = {
            { 0, &ISocketGetFrame::RejectLegacyWlanCommand, "GetFrameRaw" },
        };
        RegisterHandlers(functions);
    }
private:
    void RejectLegacyWlanCommand(HLERequestContext& ctx) {
        // These interfaces are registered only before HOS 15. Modern HOS uses
        // the wlan creator/service path below; no legacy Switch radio backend exists.
        IPC::ResponseBuilder rb{ctx, 2};
        rb.Push(IPC::ResultNotSupported);
    }

};

class ISocketManager final : public ServiceFramework<ISocketManager> {
public:
    explicit ISocketManager(Core::System& system_)
        : ServiceFramework{system_, "wlan:soc"}
    {
        static const FunctionInfo functions[] = {
            { 0, &ISocketManager::RejectLegacyWlanCommand, "PutFrameRaw_2" },
            { 1, &ISocketManager::RejectLegacyWlanCommand, "CancelGetFrame_2" },
            { 2, &ISocketManager::RejectLegacyWlanCommand, "CreateRxEntry_2" },
            { 3, &ISocketManager::RejectLegacyWlanCommand, "DeleteRxEntry_2" },
            { 4, &ISocketManager::RejectLegacyWlanCommand, "AddEthertypeToRxEntry_2" },
            { 5, &ISocketManager::RejectLegacyWlanCommand, "DeleteEthertypeFromRxEntry_2" },
            { 6, &ISocketManager::RejectLegacyWlanCommand, "GetMacAddress_3" },
            { 7, &ISocketManager::RejectLegacyWlanCommand, "SwitchTsfTimerFunction" },
            { 8, &ISocketManager::RejectLegacyWlanCommand, "GetDeltaTimeBetweenSystemAndTsf" },
            { 9, &ISocketManager::RejectLegacyWlanCommand, "RegisterSharedMemory" },
            { 10, &ISocketManager::RejectLegacyWlanCommand, "UnregisterSharedMemory" },
            { 11, &ISocketManager::RejectLegacyWlanCommand, "EnableSharedMemory" },
            { 12, &ISocketManager::RejectLegacyWlanCommand, "SetMulticastFilter" },
        };
        RegisterHandlers(functions);
    }
private:
    void RejectLegacyWlanCommand(HLERequestContext& ctx) {
        // These interfaces are registered only before HOS 15. Modern HOS uses
        // the wlan creator/service path below; no legacy Switch radio backend exists.
        IPC::ResponseBuilder rb{ctx, 2};
        rb.Push(IPC::ResultNotSupported);
    }

};

class IDetectManager final : public ServiceFramework<IDetectManager> {
public:
    explicit IDetectManager(Core::System& system_)
        : ServiceFramework{system_, "wlan:dtc"}
    {
        static const FunctionInfo functions[] = {
            { 0, &IDetectManager::RejectLegacyWlanCommand, "Cmd0" },
            { 1, &IDetectManager::RejectLegacyWlanCommand, "Cmd1" },
            { 2, &IDetectManager::RejectLegacyWlanCommand, "Cmd2" },
            { 3, &IDetectManager::RejectLegacyWlanCommand, "Cmd3" },
            { 4, &IDetectManager::RejectLegacyWlanCommand, "Cmd4" },
            { 5, &IDetectManager::RejectLegacyWlanCommand, "Cmd5" },
            { 6, &IDetectManager::RejectLegacyWlanCommand, "Cmd6" },
            { 7, &IDetectManager::RejectLegacyWlanCommand, "Cmd7" },
            { 8, &IDetectManager::RejectLegacyWlanCommand, "Cmd8" },
            { 9, &IDetectManager::RejectLegacyWlanCommand, "Cmd9" },
            { 10, &IDetectManager::RejectLegacyWlanCommand, "Cmd10" },
            { 11, &IDetectManager::RejectLegacyWlanCommand, "Cmd11" },
            { 12, &IDetectManager::RejectLegacyWlanCommand, "Cmd12" },
            { 13, &IDetectManager::RejectLegacyWlanCommand, "Cmd13" },
            { 14, &IDetectManager::RejectLegacyWlanCommand, "Cmd14" },
            { 15, &IDetectManager::RejectLegacyWlanCommand, "Cmd15" },
            { 16, &IDetectManager::RejectLegacyWlanCommand, "Cmd16" },
            { 17, &IDetectManager::RejectLegacyWlanCommand, "Cmd17" },
            { 18, &IDetectManager::RejectLegacyWlanCommand, "Cmd18" },
            { 19, &IDetectManager::RejectLegacyWlanCommand, "Cmd19" },
            { 20, &IDetectManager::RejectLegacyWlanCommand, "Cmd20" },
            { 21, &IDetectManager::RejectLegacyWlanCommand, "Cmd21" },
            { 22, &IDetectManager::RejectLegacyWlanCommand, "Cmd22" },
            { 23, &IDetectManager::RejectLegacyWlanCommand, "Cmd23" },
            { 24, &IDetectManager::RejectLegacyWlanCommand, "Cmd24" },
            { 25, &IDetectManager::RejectLegacyWlanCommand, "Cmd25" },
            { 26, &IDetectManager::RejectLegacyWlanCommand, "Cmd26" },
            { 27, &IDetectManager::RejectLegacyWlanCommand, "Cmd27" },
        };
        RegisterHandlers(functions);
    }
private:
    void RejectLegacyWlanCommand(HLERequestContext& ctx) {
        // These interfaces are registered only before HOS 15. Modern HOS uses
        // the wlan creator/service path below; no legacy Switch radio backend exists.
        IPC::ResponseBuilder rb{ctx, 2};
        rb.Push(IPC::ResultNotSupported);
    }

};

class IPrivateServiceCreator final : public ServiceFramework<IPrivateServiceCreator> {
public:
    explicit IPrivateServiceCreator(Core::System& system_)
        : ServiceFramework{system_, "wlan:p"}
    {
        static const FunctionInfo functions[] = {
            { 0, &IPrivateServiceCreator::RejectUnavailableCreator, "CreateWirelessCommunicationService" },
            { 1, &IPrivateServiceCreator::RejectUnavailableCreator, "CreatePrivateWirelessCommunicationService" },
        };
        RegisterHandlers(functions);
    }
private:
    void RejectUnavailableCreator(HLERequestContext& ctx) {
        IPC::ResponseBuilder rb{ctx, 2};
        rb.Push(ResultUnknown);
    }

};

class ISfDriverServiceCreator final : public ServiceFramework<ISfDriverServiceCreator> {
public:
    explicit ISfDriverServiceCreator(Core::System& system_)
        : ServiceFramework{system_, "wlan:nd"}
    {
        static const FunctionInfo functions[] = {
            { 0, &ISfDriverServiceCreator::RejectUnavailableCreator, "CreateDriverService" },
        };
        RegisterHandlers(functions);
    }
private:
    void RejectUnavailableCreator(HLERequestContext& ctx) {
        IPC::ResponseBuilder rb{ctx, 2};
        rb.Push(ResultUnknown);
    }

};


class IWirelessCommunicationService final : public ServiceFramework<IWirelessCommunicationService> {
public:
    explicit IWirelessCommunicationService(Core::System& system_)
        : ServiceFramework{system_, "IWirelessCommunicationService"} {
        // Horizon 15+ uses the unified wlan service; these low-level radio
        // commands cannot be backed by the PS5 guest network socket backend.
        static const FunctionInfo functions[] = {
            {0, &IWirelessCommunicationService::RejectUnavailableRadio, "OpenCommunication"},
            {1, &IWirelessCommunicationService::RejectUnavailableRadio, "CloseCommunication"},
            {10, &IWirelessCommunicationService::RejectUnavailableRadio, "PutActionFrame"},
            {11, &IWirelessCommunicationService::RejectUnavailableRadio, "GetActionFrame"},
            {12, &IWirelessCommunicationService::RejectUnavailableRadio, "CancelGetActionFrame"},
            {13, &IWirelessCommunicationService::RejectUnavailableRadio, "CreateRxEntryForActionFrame"},
            {14, &IWirelessCommunicationService::RejectUnavailableRadio, "DeleteRxEntryForActionFrame"},
            {20, &IWirelessCommunicationService::RejectUnavailableRadio, "SetActionFrameBssidFilter"},
            {21, &IWirelessCommunicationService::RejectUnavailableRadio, "DeleteActionFrameBssidFilter"},
            {30, &IWirelessCommunicationService::RejectUnavailableRadio, "GetState"},
            {31, &IWirelessCommunicationService::RejectUnavailableRadio, "GetCommunicationSetting"},
            {32, &IWirelessCommunicationService::RejectUnavailableRadio, "GetStationStatus"},
            {33, &IWirelessCommunicationService::RejectUnavailableRadio, "GetMacAddress"},
            {40, &IWirelessCommunicationService::RejectUnavailableRadio, "PutFrame"},
            {41, &IWirelessCommunicationService::RejectUnavailableRadio, "GetFrame"},
            {42, &IWirelessCommunicationService::RejectUnavailableRadio, "CancelGetFrame"},
            {43, &IWirelessCommunicationService::RejectUnavailableRadio, "CreateRxEntry"},
            {44, &IWirelessCommunicationService::RejectUnavailableRadio, "DeleteRxEntry"},
            {45, &IWirelessCommunicationService::RejectUnavailableRadio, "AddEthertypeToRxEntry"},
            {46, &IWirelessCommunicationService::RejectUnavailableRadio, "DeleteEthertypeFromRxEntry"},
            {47, &IWirelessCommunicationService::RejectUnavailableRadio, "AddMatchingDataToRxEntry"},
            {48, &IWirelessCommunicationService::RejectUnavailableRadio, "DeleteMatchingDataFromRxEntry"},
            {50, &IWirelessCommunicationService::RejectUnavailableRadio, "AddVendorSpecificIe"},
            {51, &IWirelessCommunicationService::RejectUnavailableRadio, "DeleteVendorSpecificIe"},
            {60, &IWirelessCommunicationService::RejectUnavailableRadio, "Scan"},
            {61, &IWirelessCommunicationService::RejectUnavailableRadio, "StopScan"},
            {62, &IWirelessCommunicationService::RejectUnavailableRadio, "GetScanCompleteEvent"},
            {63, &IWirelessCommunicationService::RejectUnavailableRadio, "GetScanResults"},
            {80, &IWirelessCommunicationService::RejectUnavailableRadio, "RequestSleep"},
            {81, &IWirelessCommunicationService::RejectUnavailableRadio, "RequestWakeUp"},
            {90, &IWirelessCommunicationService::RejectUnavailableRadio, "CreateBss"},
            {91, &IWirelessCommunicationService::RejectUnavailableRadio, "DestroyBss"},
            {92, &IWirelessCommunicationService::RejectUnavailableRadio, "Deauthenticate"},
            {93, &IWirelessCommunicationService::RejectUnavailableRadio, "GetStationConnectionEvent"},
            {94, &IWirelessCommunicationService::RejectUnavailableRadio, "GetAllowedChannels"},
            {95, &IWirelessCommunicationService::RejectUnavailableRadio, "SetActionFrameWithBeacon"},
            {96, &IWirelessCommunicationService::RejectUnavailableRadio, "StopActionFrameWithBeacon"},
            {97, &IWirelessCommunicationService::RejectUnavailableRadio, "SetMaxAssociationNumber"},
            {98, &IWirelessCommunicationService::RejectUnavailableRadio, "SetShortGiSupport"},
            {100, &IWirelessCommunicationService::RejectUnavailableRadio, "Connect"},
            {101, &IWirelessCommunicationService::RejectUnavailableRadio, "GetConnectionEvent"},
            {102, &IWirelessCommunicationService::RejectUnavailableRadio, "CancelConnect"},
            {103, &IWirelessCommunicationService::RejectUnavailableRadio, "Disconnect"},
            {104, &IWirelessCommunicationService::RejectUnavailableRadio, "GetConnectionCompleteEvent"},
            {105, &IWirelessCommunicationService::RejectUnavailableRadio, "GetConnectionStatus"},
            {106, &IWirelessCommunicationService::RejectUnavailableRadio, "GetRssiAndSignalLevel"},
            {107, &IWirelessCommunicationService::RejectUnavailableRadio, "SetMulticastFilter"},
            {108, &IWirelessCommunicationService::RejectUnavailableRadio, "SetPowerSaveMode"},
            {109, &IWirelessCommunicationService::RejectUnavailableRadio, "Cmd109"},
            {110, &IWirelessCommunicationService::RejectUnavailableRadio, "GetWakeupReason"},
            {111, &IWirelessCommunicationService::RejectUnavailableRadio, "SetTcpSessionInformation"},
            {112, &IWirelessCommunicationService::RejectUnavailableRadio, "RemoveTcpSessionInformation"},
            {113, &IWirelessCommunicationService::RejectUnavailableRadio, "SetWowlDelayedWakeTime"},
            {120, &IWirelessCommunicationService::RejectUnavailableRadio, "Cmd120"},
            {140, &IWirelessCommunicationService::RejectUnavailableRadio, "Cmd140"},
            {200, &IWirelessCommunicationService::RejectUnavailableRadio, "SetTxPower"},
            {201, &IWirelessCommunicationService::RejectUnavailableRadio, "ResetTxPower"},
            {202, &IWirelessCommunicationService::RejectUnavailableRadio, "GetTxPower"},
            {203, &IWirelessCommunicationService::RejectUnavailableRadio, "RequestIfUpDown"},
            {204, &IWirelessCommunicationService::RejectUnavailableRadio, "Cmd204"},
            {900, &IWirelessCommunicationService::RejectUnavailableRadio, "Cmd900"},
        };
        RegisterHandlers(functions);
    }

private:
    void RejectUnavailableRadio(HLERequestContext& ctx) {
        IPC::ResponseBuilder rb{ctx, 2};
        rb.Push(ResultUnknown);
    }
};

class IGeneralServiceCreator final : public ServiceFramework<IGeneralServiceCreator> {
public:
    explicit IGeneralServiceCreator(Core::System& system_)
        : ServiceFramework{system_, "wlan"} {
        static const FunctionInfo functions[] = {
            {0, &IGeneralServiceCreator::CreateWirelessCommunicationService,
             "CreateWirelessCommunicationService"},
        };
        RegisterHandlers(functions);
    }

private:
    void CreateWirelessCommunicationService(HLERequestContext& ctx) {
        IPC::ResponseBuilder rb{ctx, 2, 0, 1};
        rb.Push(ResultSuccess);
        rb.PushIpcInterface<IWirelessCommunicationService>(ctx, system);
    }
};

void LoopProcess(Core::System& system) {
    auto server_manager = std::make_unique<ServerManager>(system);
    // Horizon 15 replaced the six legacy WLAN endpoints with "wlan".
    if (FirmwareManager::GetFirmwareVersion(system).first.major >= 15) {
        server_manager->RegisterNamedService(
            "wlan", std::make_shared<IGeneralServiceCreator>(system), 10);
    } else {
        server_manager->RegisterNamedService("wlan:lcl", std::make_shared<ILocalManager>(system), 10);
        server_manager->RegisterNamedService("wlan:lg", std::make_shared<ILocalGetFrame>(system), 10);
        server_manager->RegisterNamedService("wlan:lga", std::make_shared<ILocalGetActionFrame>(system), 10);
        server_manager->RegisterNamedService("wlan:sg", std::make_shared<ISocketGetFrame>(system), 10);
        server_manager->RegisterNamedService("wlan:soc", std::make_shared<ISocketManager>(system), 10);
        server_manager->RegisterNamedService("wlan:dtc", std::make_shared<IDetectManager>(system), 4);
    }
    if (FirmwareManager::GetFirmwareVersion(system).first.major >= 15) {
        server_manager->RegisterNamedService("wlan:p",
                                             std::make_shared<IPrivateServiceCreator>(system), 30);
        server_manager->RegisterNamedService("wlan:nd",
                                             std::make_shared<ISfDriverServiceCreator>(system), 5);
    }
    ServerManager::RunServer(std::move(server_manager));
}

} // namespace Service::WLAN
