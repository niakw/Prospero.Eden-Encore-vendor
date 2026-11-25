// SPDX-FileCopyrightText: Copyright 2018 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "core/hle/service/hid/xcd.h"
#include "core/hle/service/ipc_helpers.h"

namespace Service::HID {

XCD_SYS::XCD_SYS(Core::System& system_) : ServiceFramework{system_, "xcd:sys"} {
    // clang-format off
    static const FunctionInfo functions[] = {
        {0, &XCD_SYS::UnsupportedPhysicalRailCommand, "GetDataFormat"},
        {1, &XCD_SYS::UnsupportedPhysicalRailCommand, "SetDataFormat"},
        {2, &XCD_SYS::UnsupportedPhysicalRailCommand, "GetMcuState"},
        {3, &XCD_SYS::UnsupportedPhysicalRailCommand, "SetMcuState"},
        {4, &XCD_SYS::UnsupportedPhysicalRailCommand, "GetMcuVersionForNfc"},
        {5, &XCD_SYS::UnsupportedPhysicalRailCommand, "CheckNfcDevicePower"},
        {10, &XCD_SYS::UnsupportedPhysicalRailCommand, "SetNfcEvent"},
        {11, &XCD_SYS::UnsupportedPhysicalRailCommand, "GetNfcInfo"},
        {12, &XCD_SYS::UnsupportedPhysicalRailCommand, "StartNfcDiscovery"},
        {13, &XCD_SYS::UnsupportedPhysicalRailCommand, "StopNfcDiscovery"},
        {14, &XCD_SYS::UnsupportedPhysicalRailCommand, "StartNtagRead"},
        {15, &XCD_SYS::UnsupportedPhysicalRailCommand, "StartNtagWrite"},
        {16, &XCD_SYS::UnsupportedPhysicalRailCommand, "SendNfcRawData"},
        {17, &XCD_SYS::UnsupportedPhysicalRailCommand, "RegisterMifareKey"},
        {18, &XCD_SYS::UnsupportedPhysicalRailCommand, "ClearMifareKey"},
        {19, &XCD_SYS::UnsupportedPhysicalRailCommand, "StartMifareRead"},
        {20, &XCD_SYS::UnsupportedPhysicalRailCommand, "StartMifareWrite"},
        {101, &XCD_SYS::UnsupportedPhysicalRailCommand, "GetAwakeTriggerReasonForLeftRail"},
        {102, &XCD_SYS::UnsupportedPhysicalRailCommand, "GetAwakeTriggerReasonForRightRail"},
        {103, &XCD_SYS::UnsupportedPhysicalRailCommand, "GetAwakeTriggerBatteryLevelTransitionForLeftRail"},
        {104, &XCD_SYS::UnsupportedPhysicalRailCommand, "GetAwakeTriggerBatteryLevelTransitionForRightRail"},
    };
    // clang-format on

    RegisterHandlers(functions);
}

void XCD_SYS::UnsupportedPhysicalRailCommand(HLERequestContext& ctx) {
    // xcd:sys controls the Switch rail MCU/NFC hardware (NTag/Mifare and
    // physical left/right rail wake state). Encore/PS5 has no such hardware
    // backend; recognize the command but fail explicitly rather than faking it.
    IPC::ResponseBuilder rb{ctx, 2};
    rb.Push(IPC::ResultNotSupported);
}

XCD_SYS::~XCD_SYS() = default;

} // namespace Service::HID
