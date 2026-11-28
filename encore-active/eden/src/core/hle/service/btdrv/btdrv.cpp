// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

// SPDX-FileCopyrightText: Copyright 2018 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "common/logging.h"
#include "core/core.h"
#include "core/hle/kernel/k_event.h"
#include "core/hle/service/btdrv/btdrv.h"
#include "core/hle/service/cmif_serialization.h"
#include "core/hle/service/ipc_helpers.h"
#include "core/hle/service/kernel_helpers.h"
#include "core/hle/service/server_manager.h"
#include "core/hle/service/service.h"
#include "core/hle/service/sm/sm.h"

namespace Service::BtDrv {

class IBluetoothUser final : public ServiceFramework<IBluetoothUser> {
public:
    explicit IBluetoothUser(Core::System& system_)
        : ServiceFramework{system_, "bt"}, service_context{system_, "bt"} {
        // clang-format off
        static const FunctionInfo functions[] = {
            {0, &IBluetoothUser::RejectUnavailableBluetoothUser, "LeClientReadCharacteristic"},
            {1, &IBluetoothUser::RejectUnavailableBluetoothUser, "LeClientReadDescriptor"},
            {2, &IBluetoothUser::RejectUnavailableBluetoothUser, "LeClientWriteCharacteristic"},
            {3, &IBluetoothUser::RejectUnavailableBluetoothUser, "LeClientWriteDescriptor"},
            {4, &IBluetoothUser::RejectUnavailableBluetoothUser, "LeClientRegisterNotification"},
            {5, &IBluetoothUser::RejectUnavailableBluetoothUser, "LeClientDeregisterNotification"},
            {6, &IBluetoothUser::RejectUnavailableBluetoothUser, "SetLeResponse"},
            {7, &IBluetoothUser::RejectUnavailableBluetoothUser, "LeSendIndication"},
            {8, &IBluetoothUser::RejectUnavailableBluetoothUser, "GetLeEventInfo"},
            {9, C<&IBluetoothUser::RegisterBleEvent>, "RegisterBleEvent"},
        };
        // clang-format on
        RegisterHandlers(functions);

        register_event = service_context.CreateEvent("BT:RegisterEvent");
    }

    ~IBluetoothUser() override {
        service_context.CloseEvent(register_event);
    }

private:
    void RejectUnavailableBluetoothUser(HLERequestContext& ctx) {
        LOG_DEBUG(Service_BTDRV, "Rejected guest BLE/GATT user operation: no Switch Bluetooth backend on PS5");
        IPC::ResponseBuilder rb{ctx, 2};
        rb.Push(IPC::ResultNotSupported);
    }

    Result RegisterBleEvent(OutCopyHandle<Kernel::KReadableEvent> out_event) {
        LOG_DEBUG(Service_BTM, "called");

        *out_event = &register_event->GetReadableEvent();
        R_SUCCEED();
    }

    KernelHelpers::ServiceContext service_context;

    Kernel::KEvent* register_event;
};

class IBluetoothDriver final : public ServiceFramework<IBluetoothDriver> {
public:
    explicit IBluetoothDriver(Core::System& system_) : ServiceFramework{system_, "btdrv"} {
        // clang-format off
        static const FunctionInfo functions[] = {
            {0, &IBluetoothDriver::RejectUnavailableBluetoothHardware, "InitializeBluetoothDriver"},
            {1, &IBluetoothDriver::RejectUnavailableBluetoothHardware, "InitializeBluetooth"},
            {2, &IBluetoothDriver::RejectUnavailableBluetoothHardware, "EnableBluetooth"},
            {3, &IBluetoothDriver::RejectUnavailableBluetoothHardware, "DisableBluetooth"},
            {4, &IBluetoothDriver::RejectUnavailableBluetoothHardware, "FinalizeBluetooth"},
            {5, &IBluetoothDriver::RejectUnavailableBluetoothHardware, "GetAdapterProperties"},
            {6, &IBluetoothDriver::RejectUnavailableBluetoothHardware, "GetAdapterProperty"},
            {7, &IBluetoothDriver::RejectUnavailableBluetoothHardware, "SetAdapterProperty"},
            {8, &IBluetoothDriver::RejectUnavailableBluetoothHardware, "StartInquiry"},
            {9, &IBluetoothDriver::RejectUnavailableBluetoothHardware, "StopInquiry"},
            {10, &IBluetoothDriver::RejectUnavailableBluetoothHardware, "CreateBond"},
            {11, &IBluetoothDriver::RejectUnavailableBluetoothHardware, "RemoveBond"},
            {12, &IBluetoothDriver::RejectUnavailableBluetoothHardware, "CancelBond"},
            {13, &IBluetoothDriver::RejectUnavailableBluetoothHardware, "RespondToPinRequest"},
            {14, &IBluetoothDriver::RejectUnavailableBluetoothHardware, "RespondToSspRequest"},
            {15, &IBluetoothDriver::RejectUnavailableBluetoothHardware, "GetEventInfo"},
            {16, &IBluetoothDriver::RejectUnavailableBluetoothHardware, "InitializeHid"},
            {17, &IBluetoothDriver::RejectUnavailableBluetoothHardware, "OpenHidConnection"},
            {18, &IBluetoothDriver::RejectUnavailableBluetoothHardware, "CloseHidConnection"},
            {19, &IBluetoothDriver::RejectUnavailableBluetoothHardware, "WriteHidData"},
            {20, &IBluetoothDriver::RejectUnavailableBluetoothHardware, "WriteHidData2"},
            {21, &IBluetoothDriver::RejectUnavailableBluetoothHardware, "SetHidReport"},
            {22, &IBluetoothDriver::RejectUnavailableBluetoothHardware, "GetHidReport"},
            {23, &IBluetoothDriver::RejectUnavailableBluetoothHardware, "TriggerConnection"},
            {24, &IBluetoothDriver::RejectUnavailableBluetoothHardware, "AddPairedDeviceInfo"},
            {25, &IBluetoothDriver::RejectUnavailableBluetoothHardware, "GetPairedDeviceInfo"},
            {26, &IBluetoothDriver::RejectUnavailableBluetoothHardware, "FinalizeHid"},
            {27, &IBluetoothDriver::RejectUnavailableBluetoothHardware, "GetHidEventInfo"},
            {28, &IBluetoothDriver::RejectUnavailableBluetoothHardware, "SetTsi"},
            {29, &IBluetoothDriver::RejectUnavailableBluetoothHardware, "EnableBurstMode"},
            {30, &IBluetoothDriver::RejectUnavailableBluetoothHardware, "SetZeroRetransmission"},
            {31, &IBluetoothDriver::RejectUnavailableBluetoothHardware, "EnableMcMode"},
            {32, &IBluetoothDriver::RejectUnavailableBluetoothHardware, "EnableLlrScan"},
            {33, &IBluetoothDriver::RejectUnavailableBluetoothHardware, "DisableLlrScan"},
            {34, C<&IBluetoothDriver::EnableRadio>, "EnableRadio"},
            {35, &IBluetoothDriver::RejectUnavailableBluetoothHardware, "SetVisibility"},
            {36, &IBluetoothDriver::RejectUnavailableBluetoothHardware, "EnableTbfcScan"},
            {37, &IBluetoothDriver::RejectUnavailableBluetoothHardware, "RegisterHidReportEvent"},
            {38, &IBluetoothDriver::RejectUnavailableBluetoothHardware, "GetHidReportEventInfo"},
            {39, &IBluetoothDriver::RejectUnavailableBluetoothHardware, "GetLatestPlr"},
            {40, &IBluetoothDriver::RejectUnavailableBluetoothHardware, "GetPendingConnections"},
            {41, &IBluetoothDriver::RejectUnavailableBluetoothHardware, "GetChannelMap"},
            {42, &IBluetoothDriver::RejectUnavailableBluetoothHardware, "EnableTxPowerBoostSetting"},
            {43, &IBluetoothDriver::RejectUnavailableBluetoothHardware, "IsTxPowerBoostSettingEnabled"},
            {44, &IBluetoothDriver::RejectUnavailableBluetoothHardware, "EnableAfhSetting"},
            {45, &IBluetoothDriver::RejectUnavailableBluetoothHardware, "IsAfhSettingEnabled"},
            {46, &IBluetoothDriver::RejectUnavailableBluetoothHardware, "InitializeBle"},
            {47, &IBluetoothDriver::RejectUnavailableBluetoothHardware, "EnableBle"},
            {48, &IBluetoothDriver::RejectUnavailableBluetoothHardware, "DisableBle"},
            {49, &IBluetoothDriver::RejectUnavailableBluetoothHardware, "FinalizeBle"},
            {50, &IBluetoothDriver::RejectUnavailableBluetoothHardware, "SetBleVisibility"},
            {51, &IBluetoothDriver::RejectUnavailableBluetoothHardware, "SetBleConnectionParameter"},
            {52, &IBluetoothDriver::RejectUnavailableBluetoothHardware, "SetBleDefaultConnectionParameter"},
            {53, &IBluetoothDriver::RejectUnavailableBluetoothHardware, "SetBleAdvertiseData"},
            {54, &IBluetoothDriver::RejectUnavailableBluetoothHardware, "SetBleAdvertiseParameter"},
            {55, &IBluetoothDriver::RejectUnavailableBluetoothHardware, "StartBleScan"},
            {56, &IBluetoothDriver::RejectUnavailableBluetoothHardware, "StopBleScan"},
            {57, &IBluetoothDriver::RejectUnavailableBluetoothHardware, "AddBleScanFilterCondition"},
            {58, &IBluetoothDriver::RejectUnavailableBluetoothHardware, "DeleteBleScanFilterCondition"},
            {59, &IBluetoothDriver::RejectUnavailableBluetoothHardware, "DeleteBleScanFilter"},
            {60, &IBluetoothDriver::RejectUnavailableBluetoothHardware, "ClearBleScanFilters"},
            {61, &IBluetoothDriver::RejectUnavailableBluetoothHardware, "EnableBleScanFilter"},
            {62, &IBluetoothDriver::RejectUnavailableBluetoothHardware, "RegisterGattClient"},
            {63, &IBluetoothDriver::RejectUnavailableBluetoothHardware, "UnregisterGattClient"},
            {64, &IBluetoothDriver::RejectUnavailableBluetoothHardware, "UnregisterAllGattClients"},
            {65, &IBluetoothDriver::RejectUnavailableBluetoothHardware, "ConnectGattServer"},
            {66, &IBluetoothDriver::RejectUnavailableBluetoothHardware, "CancelConnectGattServer"},
            {67, &IBluetoothDriver::RejectUnavailableBluetoothHardware, "DisconnectGattServer"},
            {68, &IBluetoothDriver::RejectUnavailableBluetoothHardware, "GetGattAttribute"},
            {69, &IBluetoothDriver::RejectUnavailableBluetoothHardware, "GetGattService"},
            {70, &IBluetoothDriver::RejectUnavailableBluetoothHardware, "ConfigureAttMtu"},
            {71, &IBluetoothDriver::RejectUnavailableBluetoothHardware, "RegisterGattServer"},
            {72, &IBluetoothDriver::RejectUnavailableBluetoothHardware, "UnregisterGattServer"},
            {73, &IBluetoothDriver::RejectUnavailableBluetoothHardware, "ConnectGattClient"},
            {74, &IBluetoothDriver::RejectUnavailableBluetoothHardware, "DisconnectGattClient"},
            {75, &IBluetoothDriver::RejectUnavailableBluetoothHardware, "AddGattService"},
            {76, &IBluetoothDriver::RejectUnavailableBluetoothHardware, "EnableGattService"},
            {77, &IBluetoothDriver::RejectUnavailableBluetoothHardware, "AddGattCharacteristic"},
            {78, &IBluetoothDriver::RejectUnavailableBluetoothHardware, "AddGattDescriptor"},
            {79, &IBluetoothDriver::RejectUnavailableBluetoothHardware, "GetBleManagedEventInfo"},
            {80, &IBluetoothDriver::RejectUnavailableBluetoothHardware, "GetGattFirstCharacteristic"},
            {81, &IBluetoothDriver::RejectUnavailableBluetoothHardware, "GetGattNextCharacteristic"},
            {82, &IBluetoothDriver::RejectUnavailableBluetoothHardware, "GetGattFirstDescriptor"},
            {83, &IBluetoothDriver::RejectUnavailableBluetoothHardware, "GetGattNextDescriptor"},
            {84, &IBluetoothDriver::RejectUnavailableBluetoothHardware, "RegisterGattManagedDataPath"},
            {85, &IBluetoothDriver::RejectUnavailableBluetoothHardware, "UnregisterGattManagedDataPath"},
            {86, &IBluetoothDriver::RejectUnavailableBluetoothHardware, "RegisterGattHidDataPath"},
            {87, &IBluetoothDriver::RejectUnavailableBluetoothHardware, "UnregisterGattHidDataPath"},
            {88, &IBluetoothDriver::RejectUnavailableBluetoothHardware, "RegisterGattDataPath"},
            {89, &IBluetoothDriver::RejectUnavailableBluetoothHardware, "UnregisterGattDataPath"},
            {90, &IBluetoothDriver::RejectUnavailableBluetoothHardware, "ReadGattCharacteristic"},
            {91, &IBluetoothDriver::RejectUnavailableBluetoothHardware, "ReadGattDescriptor"},
            {92, &IBluetoothDriver::RejectUnavailableBluetoothHardware, "WriteGattCharacteristic"},
            {93, &IBluetoothDriver::RejectUnavailableBluetoothHardware, "WriteGattDescriptor"},
            {94, &IBluetoothDriver::RejectUnavailableBluetoothHardware, "RegisterGattNotification"},
            {95, &IBluetoothDriver::RejectUnavailableBluetoothHardware, "UnregisterGattNotification"},
            {96, &IBluetoothDriver::RejectUnavailableBluetoothHardware, "GetLeHidEventInfo"},
            {97, &IBluetoothDriver::RejectUnavailableBluetoothHardware, "RegisterBleHidEvent"},
            {98, &IBluetoothDriver::RejectUnavailableBluetoothHardware, "SetBleScanParameter"},
            {99, &IBluetoothDriver::RejectUnavailableBluetoothHardware, "MoveToSecondaryPiconet"},
            {100, &IBluetoothDriver::RejectUnavailableBluetoothHardware, "IsBluetoothEnabled"},
            {128, &IBluetoothDriver::RejectUnavailableBluetoothHardware, "AcquireAudioEvent"},
            {129, &IBluetoothDriver::RejectUnavailableBluetoothHardware, "GetAudioEventInfo"},
            {130, &IBluetoothDriver::RejectUnavailableBluetoothHardware, "OpenAudioConnection"},
            {131, &IBluetoothDriver::RejectUnavailableBluetoothHardware, "CloseAudioConnection"},
            {132, &IBluetoothDriver::RejectUnavailableBluetoothHardware, "OpenAudioOut"},
            {133, &IBluetoothDriver::RejectUnavailableBluetoothHardware, "CloseAudioOut"},
            {134, &IBluetoothDriver::RejectUnavailableBluetoothHardware, "AcquireAudioOutStateChangedEvent"},
            {135, &IBluetoothDriver::RejectUnavailableBluetoothHardware, "StartAudioOut"},
            {136, &IBluetoothDriver::RejectUnavailableBluetoothHardware, "StopAudioOut"},
            {137, &IBluetoothDriver::RejectUnavailableBluetoothHardware, "GetAudioOutState"},
            {138, &IBluetoothDriver::RejectUnavailableBluetoothHardware, "GetAudioOutFeedingCodec"},
            {139, &IBluetoothDriver::RejectUnavailableBluetoothHardware, "GetAudioOutFeedingParameter"},
            {140, &IBluetoothDriver::RejectUnavailableBluetoothHardware, "AcquireAudioOutBufferAvailableEvent"},
            {141, &IBluetoothDriver::RejectUnavailableBluetoothHardware, "SendAudioData"},
            {142, &IBluetoothDriver::RejectUnavailableBluetoothHardware, "AcquireAudioControlInputStateChangedEvent"},
            {143, &IBluetoothDriver::RejectUnavailableBluetoothHardware, "GetAudioControlInputState"},
            {144, &IBluetoothDriver::RejectUnavailableBluetoothHardware, "AcquireAudioConnectionStateChangedEvent"},
            {145, &IBluetoothDriver::RejectUnavailableBluetoothHardware, "GetConnectedAudioDevice"},
            {146, &IBluetoothDriver::RejectUnavailableBluetoothHardware, "CloseAudioControlInput"},
            {147, &IBluetoothDriver::RejectUnavailableBluetoothHardware, "RegisterAudioControlNotification"},
            {148, &IBluetoothDriver::RejectUnavailableBluetoothHardware, "SendAudioControlPassthroughCommand"},
            {149, &IBluetoothDriver::RejectUnavailableBluetoothHardware, "SendAudioControlSetAbsoluteVolumeCommand"},
            {150, &IBluetoothDriver::RejectUnavailableBluetoothHardware, "AcquireAudioSinkVolumeLocallyChangedEvent"},
            {151, &IBluetoothDriver::RejectUnavailableBluetoothHardware, "AcquireAudioSinkVolumeUpdateRequestCompletedEvent"},
            {152, &IBluetoothDriver::RejectUnavailableBluetoothHardware, "GetAudioSinkVolume"},
            {153, &IBluetoothDriver::RejectUnavailableBluetoothHardware, "RequestUpdateAudioSinkVolume"},
            {154, &IBluetoothDriver::RejectUnavailableBluetoothHardware, "IsAudioSinkVolumeSupported"},
            {256, &IBluetoothDriver::RejectUnavailableBluetoothHardware, "IsManufacturingMode"},
            {257, &IBluetoothDriver::UnsupportedDebugCommand, "EmulateBluetoothCrash"},
            {258, &IBluetoothDriver::RejectUnavailableBluetoothHardware, "GetBleChannelMap"},
        };
        // clang-format on

        RegisterHandlers(functions);
    }

private:
    void RejectUnavailableBluetoothHardware(HLERequestContext& ctx) {
        LOG_DEBUG(Service_BTDRV, "Rejected btdrv hardware operation: Switch Bluetooth UART/BSA backend unavailable");
        IPC::ResponseBuilder rb{ctx, 2};
        rb.Push(IPC::ResultNotSupported);
    }

    void UnsupportedDebugCommand(HLERequestContext& ctx) {
        LOG_WARNING(Service_BTDRV, "Bluetooth crash emulation is disabled in the production runtime");
        IPC::ResponseBuilder rb{ctx, 2};
        rb.Push(IPC::ResultNotSupported);
    }

    Result EnableRadio() {
        LOG_DEBUG(Service_BTDRV, "EnableRadio acknowledged locally; no guest Bluetooth radio is exposed");
        R_SUCCEED();
    }
};

void LoopProcess(Core::System& system) {
    auto server_manager = std::make_unique<ServerManager>(system);

    server_manager->RegisterNamedService("btdrv", std::make_shared<IBluetoothDriver>(system));
    server_manager->RegisterNamedService("bt", std::make_shared<IBluetoothUser>(system));
    ServerManager::RunServer(std::move(server_manager));
}

} // namespace Service::BtDrv
