// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

// SPDX-FileCopyrightText: Copyright 2024 yuzu Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#include <memory>

#include "common/logging.h"
#include "core/core.h"
#include "core/hle/kernel/k_event.h"
#include "core/hle/service/btm/btm_user_core.h"
#include "core/hle/service/cmif_serialization.h"
#include "core/hle/service/ipc_helpers.h"

namespace Service::BTM {

IBtmUserCore::IBtmUserCore(Core::System& system_)
    : ServiceFramework{system_, "IBtmUserCore"}, service_context{system_, "IBtmUserCore"} {
    // clang-format off
    static const FunctionInfo functions[] = {
        {0, C<&IBtmUserCore::AcquireBleScanEvent>, "AcquireBleScanEvent"},
        {1, &IBtmUserCore::RejectUnsupportedBleOperation, "GetBleScanFilterParameter"},
        {2, &IBtmUserCore::RejectUnsupportedBleOperation, "GetBleScanFilterParameter2"},
        {3, &IBtmUserCore::RejectUnsupportedBleOperation, "StartBleScanForGeneral"},
        {4, C<&IBtmUserCore::StopOfflineBleScan>, "StopBleScanForGeneral"},
        {5, C<&IBtmUserCore::GetOfflineBleScanResults>, "GetBleScanResultsForGeneral"},
        {6, &IBtmUserCore::RejectUnsupportedBleOperation, "StartBleScanForPaired"},
        {7, C<&IBtmUserCore::StopOfflineBleScan>, "StopBleScanForPaired"},
        {8, &IBtmUserCore::RejectUnsupportedBleOperation, "StartBleScanForSmartDevice"},
        {9, C<&IBtmUserCore::StopOfflineBleScan>, "StopBleScanForSmartDevice"},
        {10, C<&IBtmUserCore::GetOfflineBleScanResults>, "GetBleScanResultsForSmartDevice"},
        {17, C<&IBtmUserCore::AcquireBleConnectionEvent>, "AcquireBleConnectionEvent"},
        {18, &IBtmUserCore::RejectUnsupportedBleOperation, "BleConnect"},
        {19, &IBtmUserCore::RejectUnsupportedBleOperation, "BleDisconnect"},
        {20, C<&IBtmUserCore::GetOfflineBleConnectionState>, "BleGetConnectionState"},
        {21, C<&IBtmUserCore::AcquireBlePairingEvent>, "AcquireBlePairingEvent"},
        {22, &IBtmUserCore::RejectUnsupportedBleOperation, "BlePairDevice"},
        {23, &IBtmUserCore::RejectUnsupportedBleOperation, "BleUnPairDevice"},
        {24, &IBtmUserCore::RejectUnsupportedBleOperation, "BleUnPairDevice2"},
        {25, C<&IBtmUserCore::GetOfflineBlePairedDevices>, "BleGetPairedDevices"},
        {26, C<&IBtmUserCore::AcquireBleServiceDiscoveryEvent>, "AcquireBleServiceDiscoveryEvent"},
        {27, &IBtmUserCore::RejectUnsupportedBleOperation, "GetGattServices"},
        {28, &IBtmUserCore::RejectUnsupportedBleOperation, "GetGattService"},
        {29, &IBtmUserCore::RejectUnsupportedBleOperation, "GetGattIncludedServices"},
        {30, &IBtmUserCore::RejectUnsupportedBleOperation, "GetBelongingGattService"},
        {31, &IBtmUserCore::RejectUnsupportedBleOperation, "GetGattCharacteristics"},
        {32, &IBtmUserCore::RejectUnsupportedBleOperation, "GetGattDescriptors"},
        {33, C<&IBtmUserCore::AcquireBleMtuConfigEvent>, "AcquireBleMtuConfigEvent"},
        {34, &IBtmUserCore::RejectUnsupportedBleOperation, "ConfigureBleMtu"},
        {35, &IBtmUserCore::RejectUnsupportedBleOperation, "GetBleMtu"},
        {36, &IBtmUserCore::RejectUnsupportedBleOperation, "RegisterBleGattDataPath"},
        {37, &IBtmUserCore::RejectUnsupportedBleOperation, "UnregisterBleGattDataPath"},
    };
    // clang-format on
    RegisterHandlers(functions);

    scan_event = service_context.CreateEvent("IBtmUserCore:ScanEvent");
    connection_event = service_context.CreateEvent("IBtmUserCore:ConnectionEvent");
    pairing_event = service_context.CreateEvent("IBtmUserCore:PairingEvent");
    service_discovery_event = service_context.CreateEvent("IBtmUserCore:DiscoveryEvent");
    config_event = service_context.CreateEvent("IBtmUserCore:ConfigEvent");
}

void IBtmUserCore::RejectUnsupportedBleOperation(HLERequestContext& ctx) {
    // Native Switch Bluetooth transport is not available on PS5.
    IPC::ResponseBuilder rb{ctx, 2};
    rb.Push(IPC::ResultNotSupported);
}

IBtmUserCore::~IBtmUserCore() {
    service_context.CloseEvent(scan_event);
    service_context.CloseEvent(connection_event);
    service_context.CloseEvent(pairing_event);
    service_context.CloseEvent(service_discovery_event);
    service_context.CloseEvent(config_event);
}

Result IBtmUserCore::AcquireBleScanEvent(Out<bool> out_is_valid,
                                         OutCopyHandle<Kernel::KReadableEvent> out_event) {
    LOG_DEBUG(Service_BTM, "Returning live BLE service event");

    *out_is_valid = true;
    *out_event = &scan_event->GetReadableEvent();
    R_SUCCEED();
}

Result IBtmUserCore::AcquireBleConnectionEvent(Out<bool> out_is_valid,
                                               OutCopyHandle<Kernel::KReadableEvent> out_event) {
    LOG_DEBUG(Service_BTM, "Returning live BLE service event");

    *out_is_valid = true;
    *out_event = &connection_event->GetReadableEvent();
    R_SUCCEED();
}

Result IBtmUserCore::AcquireBleServiceDiscoveryEvent(
    Out<bool> out_is_valid, OutCopyHandle<Kernel::KReadableEvent> out_event) {
    LOG_DEBUG(Service_BTM, "Returning live BLE service event");

    *out_is_valid = true;
    *out_event = &service_discovery_event->GetReadableEvent();
    R_SUCCEED();
}

Result IBtmUserCore::AcquireBleMtuConfigEvent(Out<bool> out_is_valid,
                                              OutCopyHandle<Kernel::KReadableEvent> out_event) {
    LOG_DEBUG(Service_BTM, "Returning live BLE service event");

    *out_is_valid = true;
    *out_event = &config_event->GetReadableEvent();
    R_SUCCEED();
}

Result IBtmUserCore::StopOfflineBleScan() {
    // Guest BLE scanning has no radio backend. Stopping an inactive scan is idempotent.
    R_SUCCEED();
}

Result IBtmUserCore::GetOfflineBleScanResults(ClientAppletResourceUserId,
                                             Out<u8> out_count,
                                             OutBuffer<BufferAttr_HipcMapAlias> out_results) {
    // No guest BLE scanner produces records. A zero count means no entries are consumed.
    *out_count = 0;
    R_SUCCEED();
}

Result IBtmUserCore::GetOfflineBleConnectionState(ClientAppletResourceUserId,
                                                 Out<u8> out_count,
                                                 OutBuffer<BufferAttr_HipcPointer> out_results) {
    // Host controller transport is not exposed as a Horizon BLE connection.
    *out_count = 0;
    R_SUCCEED();
}

Result IBtmUserCore::AcquireBlePairingEvent(Out<bool> out_is_valid,
                                           OutCopyHandle<Kernel::KReadableEvent> out_event) {
    // Keep a distinct, unsignalled event; never fabricate a completed pairing.
    *out_is_valid = true;
    *out_event = &pairing_event->GetReadableEvent();
    R_SUCCEED();
}

Result IBtmUserCore::GetOfflineBlePairedDevices(u64, Out<u8> out_count,
                                               OutBuffer<BufferAttr_HipcPointer> out_results) {
    // The 8-byte advertisement filter cannot match any guest-paired BLE device.
    *out_count = 0;
    R_SUCCEED();
}

} // namespace Service::BTM
