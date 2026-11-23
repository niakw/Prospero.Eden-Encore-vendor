// SPDX-FileCopyrightText: Copyright 2018 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <array>

#include "core/hle/service/cmif_types.h"
#include "core/hle/service/service.h"

namespace Core {
class System;
}

namespace Service::Set {

class IFactorySettingsServer final : public ServiceFramework<IFactorySettingsServer> {
public:
    explicit IFactorySettingsServer(Core::System& system_);
    ~IFactorySettingsServer() override;

private:
    Result GetBluetoothBdAddress(Out<std::array<u8, 0x6>> out_value);
    Result GetConfigurationId1(Out<std::array<u8, 0x1E>> out_value);
    Result GetAccelerometerOffset(Out<std::array<u8, 0x6>> out_value);
    Result GetAccelerometerScale(Out<std::array<u8, 0x6>> out_value);
    Result GetGyroscopeOffset(Out<std::array<u8, 0x6>> out_value);
    Result GetGyroscopeScale(Out<std::array<u8, 0x6>> out_value);
    Result GetWirelessLanMacAddress(Out<std::array<u8, 0x6>> out_value);
    Result GetWirelessLanCountryCodeCount(Out<s32> out_count);
    Result GetWirelessLanCountryCodes(
        Out<s32> out_count,
        OutArray<std::array<u8, 0x3>, BufferAttr_HipcPointer> out_codes);
    Result GetSerialNumber(Out<std::array<u8, 0x18>> out_value);
    Result GetBatteryLot(Out<std::array<u8, 0x18>> out_value);
};

} // namespace Service::Set
