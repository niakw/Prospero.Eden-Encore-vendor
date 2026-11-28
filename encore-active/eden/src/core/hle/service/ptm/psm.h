// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

// SPDX-FileCopyrightText: Copyright 2018 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <atomic>

#include "core/hle/service/kernel_helpers.h"
#include "core/hle/service/service.h"

namespace Kernel {
class KEvent;
}

namespace Service::PTM {

class PSM final : public ServiceFramework<PSM> {
public:
    explicit PSM(Core::System& system_);
    ~PSM() override;

private:
    enum class ChargerType : u32 {
        Unplugged = 0,
        RegularCharger = 1,
        LowPowerCharger = 2,
        Unknown = 3,
    };
    void GetBatteryChargePercentage(HLERequestContext& ctx);
    void GetChargerType(HLERequestContext& ctx);
    void EnableBatteryCharging(HLERequestContext& ctx);
    void DisableBatteryCharging(HLERequestContext& ctx);
    void IsBatteryChargingEnabled(HLERequestContext& ctx);
    void AcquireControllerPowerSupply(HLERequestContext& ctx);
    void ReleaseControllerPowerSupply(HLERequestContext& ctx);
    void EnableEnoughPowerChargeEmulation(HLERequestContext& ctx);
    void DisableEnoughPowerChargeEmulation(HLERequestContext& ctx);
    void EnableFastBatteryCharging(HLERequestContext& ctx);
    void DisableFastBatteryCharging(HLERequestContext& ctx);
    void OpenSession(HLERequestContext& ctx);
    void GetBatteryVoltageState(HLERequestContext& ctx);
    void GetRawBatteryChargePercentage(HLERequestContext& ctx);
    void IsEnoughPowerSupplied(HLERequestContext& ctx);
    void GetBatteryAgePercentage(HLERequestContext& ctx);
    void GetBatteryChargeInfoEvent(HLERequestContext& ctx);
    void GetBatteryChargeInfoFields(HLERequestContext& ctx);
    void GetBatteryChargeCalibratedEvent(HLERequestContext& ctx);
    void SignalBatteryChargeInfoChanged();

    KernelHelpers::ServiceContext service_context;
    Kernel::KEvent* battery_charge_info_event{};
    Kernel::KEvent* battery_charge_calibrated_event{};

    std::atomic<bool> battery_charging_enabled{true};
    std::atomic<bool> controller_power_supply{false};
    std::atomic<bool> enough_power_charge_emulation{false};
    std::atomic<bool> fast_battery_charging{false};
};

} // namespace Service::PTM
