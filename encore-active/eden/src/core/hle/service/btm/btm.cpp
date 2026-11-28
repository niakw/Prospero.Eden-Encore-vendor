// SPDX-FileCopyrightText: Copyright 2025 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

// SPDX-FileCopyrightText: Copyright 2018 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <array>
#include <memory>

#include "core/core.h"
#include "core/hle/kernel/k_event.h"
#include "core/hle/service/kernel_helpers.h"
#include "core/hle/service/cmif_serialization.h"
#include "core/hle/service/ipc_helpers.h"
#include "core/hle/service/btm/btm.h"
#include "core/hle/service/btm/btm_debug.h"
#include "core/hle/service/btm/btm_system.h"
#include "core/hle/service/btm/btm_user.h"
#include "core/hle/service/set/system_settings_server.h"
#include "core/hle/service/sm/sm.h"
#include "core/hle/service/server_manager.h"
#include "core/hle/service/service.h"

namespace Service::BTM {

class IBtm final : public ServiceFramework<IBtm> {
public:
    explicit IBtm(Core::System& system_)
        : ServiceFramework{system_, "btm"}, service_context{system_, "btm"} {
        // clang-format off
        static const FunctionInfo functions[] = {
            {0, &IBtm::RejectUnavailableBluetoothHardware, "GetState"},
            {1, &IBtm::RejectUnavailableBluetoothHardware, "GetHostDeviceProperty"},
            {2, C<&IBtm::AcquireOfflineEvent<0>>, "AcquireDeviceConditionEvent"},
            {3, C<&IBtm::GetOfflineDeviceList>, "GetDeviceCondition"},
            {4, &IBtm::RejectUnavailableBluetoothHardware, "SetBurstMode"},
            {5, &IBtm::RejectUnavailableBluetoothHardware, "SetSlotMode"},
            {6, &IBtm::RejectUnavailableBluetoothHardware, "SetBluetoothMode"},
            {7, &IBtm::RejectUnavailableBluetoothHardware, "SetWlanMode"},
            {8, C<&IBtm::AcquireOfflineEvent<1>>, "AcquireDeviceInfoEvent"},
            {9, C<&IBtm::GetOfflineDeviceList>, "GetDeviceInfo"},
            {10, &IBtm::RejectUnavailableBluetoothHardware, "AddDeviceInfo"},
            {11, &IBtm::RejectUnavailableBluetoothHardware, "RemoveDeviceInfo"},
            {12, &IBtm::RejectUnavailableBluetoothHardware, "IncreaseDeviceInfoOrder"},
            {13, &IBtm::RejectUnavailableBluetoothHardware, "LlrNotify"},
            {14, C<&IBtm::EnableRadio>, "EnableRadio"},
            {15, C<&IBtm::DisableRadio>, "DisableRadio"},
            {16, &IBtm::RejectUnavailableBluetoothHardware, "HidDisconnect"},
            {17, &IBtm::RejectUnavailableBluetoothHardware, "HidSetRetransmissionMode"},
            {18, C<&IBtm::AcquireOfflineEvent<2>>, "AcquireAwakeReqEvent"},
            {19, C<&IBtm::AcquireOfflineEvent<3>>, "AcquireLlrStateEvent"},
            {20, C<&IBtm::IsOfflineLlrStarted>, "IsLlrStarted"},
            {21, &IBtm::RejectUnavailableBluetoothHardware, "EnableSlotSaving"},
            {22, &IBtm::RejectUnavailableBluetoothHardware, "ProtectDeviceInfo"},
            {23, C<&IBtm::AcquireOfflineEvent<4>>, "AcquireBleScanEvent"},
            {24, &IBtm::RejectUnavailableBluetoothHardware, "GetBleScanParameterGeneral"},
            {25, &IBtm::RejectUnavailableBluetoothHardware, "GetBleScanParameterSmartDevice"},
            {26, &IBtm::RejectUnavailableBluetoothHardware, "StartBleScanForGeneral"},
            {27, C<&IBtm::StopOfflineBleScan>, "StopBleScanForGeneral"},
            {28, C<&IBtm::GetOfflineScanResults>, "GetBleScanResultsForGeneral"},
            {29, &IBtm::RejectUnavailableBluetoothHardware, "StartBleScanForPairedDevice"},
            {30, C<&IBtm::StopOfflineBleScan>, "StopBleScanForPairedDevice"},
            {31, &IBtm::RejectUnavailableBluetoothHardware, "StartBleScanForSmartDevice"},
            {32, C<&IBtm::StopOfflineBleScan>, "StopBleScanForSmartDevice"},
            {33, C<&IBtm::GetOfflineScanResults>, "GetBleScanResultsForSmartDevice"},
            {34, C<&IBtm::AcquireOfflineEvent<5>>, "AcquireBleConnectionEvent"},
            {35, &IBtm::RejectUnavailableBluetoothHardware, "BleConnect"},
            {36, &IBtm::RejectUnavailableBluetoothHardware, "BleOverrideConnection"},
            {37, &IBtm::RejectUnavailableBluetoothHardware, "BleDisconnect"},
            {38, C<&IBtm::GetOfflineConnections>, "BleGetConnectionState"},
            {39, &IBtm::RejectUnavailableBluetoothHardware, "BleGetGattClientConditionList"},
            {40, C<&IBtm::AcquireOfflineEvent<6>>, "AcquireBlePairingEvent"},
            {41, &IBtm::RejectUnavailableBluetoothHardware, "BlePairDevice"},
            {42, &IBtm::RejectUnavailableBluetoothHardware, "BleUnpairDeviceOnBoth"},
            {43, &IBtm::RejectUnavailableBluetoothHardware, "BleUnpairDevice"},
            {44, C<&IBtm::GetOfflinePairedAddresses>, "BleGetPairedAddresses"},
            {45, C<&IBtm::AcquireOfflineEvent<7>>, "AcquireBleServiceDiscoveryEvent"},
            {46, &IBtm::RejectUnavailableBluetoothHardware, "GetGattServices"},
            {47, &IBtm::RejectUnavailableBluetoothHardware, "GetGattService"},
            {48, &IBtm::RejectUnavailableBluetoothHardware, "GetGattIncludedServices"},
            {49, &IBtm::RejectUnavailableBluetoothHardware, "GetBelongingService"},
            {50, &IBtm::RejectUnavailableBluetoothHardware, "GetGattCharacteristics"},
            {51, &IBtm::RejectUnavailableBluetoothHardware, "GetGattDescriptors"},
            {52, C<&IBtm::AcquireOfflineEvent<8>>, "AcquireBleMtuConfigEvent"},
            {53, &IBtm::RejectUnavailableBluetoothHardware, "ConfigureBleMtu"},
            {54, &IBtm::RejectUnavailableBluetoothHardware, "GetBleMtu"},
            {55, &IBtm::RejectUnavailableBluetoothHardware, "RegisterBleGattDataPath"},
            {56, &IBtm::RejectUnavailableBluetoothHardware, "UnregisterBleGattDataPath"},
            {57, &IBtm::RejectUnavailableBluetoothHardware, "RegisterAppletResourceUserId"},
            {58, &IBtm::RejectUnavailableBluetoothHardware, "UnregisterAppletResourceUserId"},
            {59, &IBtm::RejectUnavailableBluetoothHardware, "SetAppletResourceUserId"},
            {60, &IBtm::RejectUnavailableBluetoothHardware, "AcquireBleConnectionParameterUpdateEvent"}, //8.0.0+
            {61, &IBtm::RejectUnavailableBluetoothHardware, "SetCeLength"}, //8.0.0+
            {62, &IBtm::RejectUnavailableBluetoothHardware, "EnsureSlotExpansion"}, //9.0.0+
            {63, &IBtm::RejectUnavailableBluetoothHardware, "IsSlotExpansionEnsured"}, //9.0.0+
            {64, &IBtm::RejectUnavailableBluetoothHardware, "CancelConnectionTrigger"}, //10.0.0+
            {65, &IBtm::RejectUnavailableBluetoothHardware, "GetConnectionCapacity"}, //13.0.0+
            {66, &IBtm::RejectUnavailableBluetoothHardware, "GetWlanMode"}, //13.0.0+
            {67, &IBtm::RejectUnavailableBluetoothHardware, "IsSlotSavingEnabled"}, //13.0.0+
            {68, &IBtm::RejectUnavailableBluetoothHardware, "IsSlotSavingForPairingEnabled"}, //13.0.0+
            {69, &IBtm::RejectUnavailableBluetoothHardware, "AcquireAudioDeviceConnectionEvent"}, //13.0.0+
            {70, &IBtm::RejectUnavailableBluetoothHardware, "GetConnectedAudioDevices"}, //13.0.0+
            {71, &IBtm::RejectUnavailableBluetoothHardware, "SetAudioSourceVolume"}, //13.0.0+
            {72, &IBtm::RejectUnavailableBluetoothHardware, "GetAudioSourceVolume"}, //13.0.0+
            {73, &IBtm::RejectUnavailableBluetoothHardware, "RequestAudioDeviceConnectionRejection"}, //13.0.0+
            {74, &IBtm::RejectUnavailableBluetoothHardware, "CancelAudioDeviceConnectionRejection"}, //13.0.0+
            {75, &IBtm::RejectUnavailableBluetoothHardware, "GetPairedAudioDevices"}, //13.0.0+
            {76, &IBtm::RejectUnavailableBluetoothHardware, "SetWlanModeWithOption"}, //13.1.0+
            {100, &IBtm::RejectUnavailableBluetoothHardware, "AcquireConnectionDisallowedEvent"}, //13.0.0+
            {101, &IBtm::RejectUnavailableBluetoothHardware, "GetUsecaseViolationFactor"}, //13.0.0+
            {110, C<&IBtm::GetOfflineDeviceList>, "GetShortenedDeviceInfo"}, //13.0.0+
            {111, &IBtm::RejectUnavailableBluetoothHardware, "AcquirePairingCountUpdateEvent"},//13.0.0+
            {112, &IBtm::RejectUnavailableBluetoothHardware, "Unknown112"}, //14.0.0-14.1.2
            {113, &IBtm::RejectUnavailableBluetoothHardware, "Unknown113"}, //14.0.0-14.1.2
            {114, &IBtm::RejectUnavailableBluetoothHardware, "IsFirstAudioControlConnection"}, //14.0.0+
            {115, C<&IBtm::GetOfflineDeviceList>, "GetShortenedDeviceCondition"}, //14.0.0+
            {116, &IBtm::RejectUnavailableBluetoothHardware, "SetAudioSinkVolume"}, //15.0.0+
            {117, &IBtm::RejectUnavailableBluetoothHardware, "GetAudioSinkVolume"}, //15.0.0+
        };
        // clang-format on

        RegisterHandlers(functions);

        m_set_sys =
            system.ServiceManager().GetService<Service::Set::ISystemSettingsServer>("set:sys", true);
        constexpr std::array names{
            "btm:AcquireDeviceConditionEvent",
            "btm:AcquireDeviceInfoEvent",
            "btm:AcquireAwakeReqEvent",
            "btm:AcquireLlrStateEvent",
            "btm:AcquireBleScanEvent",
            "btm:AcquireBleConnectionEvent",
            "btm:AcquireBlePairingEvent",
            "btm:AcquireBleServiceDiscoveryEvent",
            "btm:AcquireBleMtuConfigEvent",
        };
        for (size_t i = 0; i < names.size(); ++i) {
            offline_events[i] = service_context.CreateEvent(names[i]);
        }
    }

    ~IBtm() override {
        for (auto* event : offline_events) {
            service_context.CloseEvent(event);
        }
    }

private:
    template <size_t Index>
    Result AcquireOfflineEvent(Out<bool> out_valid,
                               OutCopyHandle<Kernel::KReadableEvent> out_event) {
        static_assert(Index < 9);
        // There is no guest radio backend. Keep independent, live events
        // unsignalled rather than reporting false scan/pairing completions.
        *out_valid = true;
        *out_event = &offline_events[Index]->GetReadableEvent();
        R_SUCCEED();
    }

    // HOS >= 13 uses a profile u32, pointer output and signed count for these lists.
    // PS5 controller transport is not a guest Bluetooth adapter.
    Result GetOfflineDeviceList(u32 profile, Out<s32> out_count,
                                OutBuffer<BufferAttr_HipcPointer>) {
        R_UNLESS(profile <= 2, ResultUnknown);
        *out_count = 0;
        R_SUCCEED();
    }

    Result IsOfflineLlrStarted(Out<bool> out_started) {
        *out_started = false;
        R_SUCCEED();
    }

    Result StopOfflineBleScan() {
        // No guest BLE scan is active; stop is idempotent.
        R_SUCCEED();
    }

    Result GetOfflineScanResults(Out<u8> out_count, OutBuffer<BufferAttr_HipcMapAlias>) {
        *out_count = 0;
        R_SUCCEED();
    }

    Result GetOfflineConnections(Out<u8> out_count, OutBuffer<BufferAttr_HipcPointer>) {
        *out_count = 0;
        R_SUCCEED();
    }

    Result GetOfflinePairedAddresses(u64, Out<u8> out_count,
                                     OutBuffer<BufferAttr_HipcPointer>) {
        *out_count = 0;
        R_SUCCEED();
    }

    Result EnableRadio() {
        LOG_DEBUG(Service_BTM, "called through legacy btm interface");
        R_RETURN(m_set_sys->SetBluetoothEnableFlag(true));
    }

    Result DisableRadio() {
        LOG_DEBUG(Service_BTM, "called through legacy btm interface");
        R_RETURN(m_set_sys->SetBluetoothEnableFlag(false));
    }

    void RejectUnavailableBluetoothHardware(HLERequestContext& ctx) {
        LOG_WARNING(Service_BTM,
                    "Rejected guest Bluetooth/BLE hardware command: no Switch radio backend on PS5");
        IPC::ResponseBuilder rb{ctx, 2};
        rb.Push(ResultUnknown);
    }

    KernelHelpers::ServiceContext service_context;
    std::array<Kernel::KEvent*, 9> offline_events{};
    std::shared_ptr<Service::Set::ISystemSettingsServer> m_set_sys;
};

void LoopProcess(Core::System& system) {
    auto server_manager = std::make_unique<ServerManager>(system);

    server_manager->RegisterNamedService("btm", std::make_shared<IBtm>(system));
    server_manager->RegisterNamedService("btm:dbg", std::make_shared<IBtmDebug>(system));
    server_manager->RegisterNamedService("btm:sys", std::make_shared<IBtmSystem>(system));
    server_manager->RegisterNamedService("btm:u", std::make_shared<IBtmUser>(system));
    ServerManager::RunServer(std::move(server_manager));
}

} // namespace Service::BTM
