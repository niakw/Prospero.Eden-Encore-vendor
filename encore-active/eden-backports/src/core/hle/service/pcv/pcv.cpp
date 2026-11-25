// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

// SPDX-FileCopyrightText: Copyright 2018 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <algorithm>
#include <array>
#include <memory>

#include "core/hle/service/ipc_helpers.h"
#include "core/hle/service/pcv/pcv.h"
#include "core/hle/service/server_manager.h"
#include "core/hle/service/service.h"
#include "frontend_common/firmware_manager.h"

namespace Service::PCV {

class PCV final : public ServiceFramework<PCV> {
public:
    explicit PCV(Core::System& system_) : ServiceFramework{system_, "pcv"} {
        // clang-format off
        static const FunctionInfo functions[] = {
            {0, &PCV::RejectUnsupportedDevice, "SetPowerEnabled"},
            {1, &PCV::RejectUnsupportedDevice, "SetClockEnabled"},
            {2, &PCV::RejectUnsupportedDevice, "SetClockRate"},
            {3, &PCV::RejectUnsupportedDevice, "GetClockRate"},
            {4, &PCV::RejectUnsupportedDevice, "GetState"},
            {5, &PCV::RejectUnsupportedDevice, "GetPossibleClockRates"},
            {6, &PCV::RejectUnsupportedDevice, "SetMinVClockRate"},
            {7, &PCV::RejectUnsupportedDevice, "SetReset"},
            {8, &PCV::RejectUnsupportedDevice, "SetVoltageEnabled"},
            {9, &PCV::RejectUnsupportedDevice, "GetVoltageEnabled"},
            {10, &PCV::RejectUnsupportedDevice, "GetVoltageRange"},
            {11, &PCV::RejectUnsupportedDevice, "SetVoltageValue"},
            {12, &PCV::RejectUnsupportedDevice, "GetVoltageValue"},
            {13, &PCV::RejectUnsupportedDevice, "GetTemperatureThresholds"},
            {14, &PCV::RejectUnsupportedDevice, "SetTemperature"},
            {15, &PCV::RejectUnsupportedDevice, "Initialize"},
            {16, &PCV::RejectUnsupportedDevice, "IsInitialized"},
            {17, &PCV::RejectUnsupportedDevice, "Finalize"},
            {18, &PCV::RejectUnsupportedDevice, "PowerOn"},
            {19, &PCV::RejectUnsupportedDevice, "PowerOff"},
            {20, &PCV::RejectUnsupportedDevice, "ChangeVoltage"},
            {21, &PCV::RejectUnsupportedDevice, "GetPowerClockInfoEvent"},
            {22, &PCV::RejectUnsupportedDevice, "GetOscillatorClock"},
            {23, &PCV::RejectUnsupportedDevice, "GetDvfsTable"},
            {24, &PCV::RejectUnsupportedDevice, "GetModuleStateTable"},
            {25, &PCV::RejectUnsupportedDevice, "GetPowerDomainStateTable"},
            {26, &PCV::RejectUnsupportedDevice, "GetFuseInfo"},
            {27, &PCV::RejectUnsupportedDevice, "GetDramId"},
            {28, &PCV::RejectUnsupportedDevice, "IsPoweredOn"},
            {29, &PCV::RejectUnsupportedDevice, "GetVoltage"},
        };
        // clang-format on

        RegisterHandlers(functions);
    }
private:
    void RejectUnsupportedDevice(HLERequestContext& ctx) {
        // Missing physical Switch power/clock/capture peripheral on the PS5.
        // Do not manufacture successful completion for guest requests.
        IPC::ResponseBuilder rb{ctx, 2};
        rb.Push(ResultUnknown);
    }

};

class PCV_ARB final : public ServiceFramework<PCV_ARB> {
public:
    explicit PCV_ARB(Core::System& system_) : ServiceFramework{system_, "pcv:arb"} {
        // clang-format off
        static const FunctionInfo functions[] = {
            {0, &PCV_ARB::RejectUnsupportedDevice, "ReleaseControl"},
        };
        // clang-format on
        RegisterHandlers(functions);
    }
private:
    void RejectUnsupportedDevice(HLERequestContext& ctx) {
        // Missing physical Switch power/clock/capture peripheral on the PS5.
        // Do not manufacture successful completion for guest requests.
        IPC::ResponseBuilder rb{ctx, 2};
        rb.Push(ResultUnknown);
    }

};

class PCV_IMM final : public ServiceFramework<PCV_IMM> {
public:
    explicit PCV_IMM(Core::System& system_) : ServiceFramework{system_, "pcv:imm"} {
        // clang-format off
        static const FunctionInfo functions[] = {
            {0, &PCV_IMM::RejectUnsupportedDevice, "SetClockRate"},
        };
        // clang-format on
        RegisterHandlers(functions);
    }
private:
    void RejectUnsupportedDevice(HLERequestContext& ctx) {
        // Missing physical Switch power/clock/capture peripheral on the PS5.
        // Do not manufacture successful completion for guest requests.
        IPC::ResponseBuilder rb{ctx, 2};
        rb.Push(ResultUnknown);
    }

};

class IClkrstSession final : public ServiceFramework<IClkrstSession> {
public:
    explicit IClkrstSession(Core::System& system_, DeviceCode device_code_)
        : ServiceFramework{system_, "IClkrstSession"}, device_code(device_code_) {
        // clang-format off
        static const FunctionInfo functions[] = {
            {0, &IClkrstSession::SetClockEnabled, "SetClockEnabled"},
            {1, &IClkrstSession::SetClockDisabled, "SetClockDisabled"},
            {2, &IClkrstSession::SetResetAsserted, "SetResetAsserted"},
            {3, &IClkrstSession::SetResetDeasserted, "SetResetDeasserted"},
            {4, &IClkrstSession::SetPowerEnabled, "SetPowerEnabled"},
            {5, &IClkrstSession::SetPowerDisabled, "SetPowerDisabled"},
            {6, &IClkrstSession::GetState, "GetState"},
            {7, &IClkrstSession::SetClockRate, "SetClockRate"},
            {8, &IClkrstSession::GetClockRate, "GetClockRate"},
            {9, &IClkrstSession::SetMinVClockRate, "SetMinVClockRate"},
            {10, &IClkrstSession::GetPossibleClockRates, "GetPossibleClockRates"},
            {11, &IClkrstSession::GetDvfsTable, "GetDvfsTable"},
        };
        // clang-format on
        RegisterHandlers(functions);
    }

private:
    struct ModuleState {
        u32 clock_frequency{};
        u8 clock_enabled{};
        u8 power_enabled{};
        u8 reset_asserted{};
        u8 reserved{};
        u32 min_v_clock_rate{};
    };
    static_assert(sizeof(ModuleState) == 0xC);

    void ReplySuccess(HLERequestContext& ctx) {
        IPC::ResponseBuilder rb{ctx, 2};
        rb.Push(ResultSuccess);
    }

    void SetClockEnabled(HLERequestContext& ctx) {
        module_state.clock_enabled = 1;
        ReplySuccess(ctx);
    }

    void SetClockDisabled(HLERequestContext& ctx) {
        module_state.clock_enabled = 0;
        ReplySuccess(ctx);
    }

    void SetResetAsserted(HLERequestContext& ctx) {
        module_state.reset_asserted = 1;
        ReplySuccess(ctx);
    }

    void SetResetDeasserted(HLERequestContext& ctx) {
        module_state.reset_asserted = 0;
        ReplySuccess(ctx);
    }

    void SetPowerEnabled(HLERequestContext& ctx) {
        module_state.power_enabled = 1;
        ReplySuccess(ctx);
    }

    void SetPowerDisabled(HLERequestContext& ctx) {
        module_state.power_enabled = 0;
        ReplySuccess(ctx);
    }

    void GetState(HLERequestContext& ctx) {
        IPC::ResponseBuilder rb{ctx, 5};
        rb.Push(ResultSuccess);
        rb.PushRaw(module_state);
    }

    void SetClockRate(HLERequestContext& ctx) {
        IPC::RequestParser rp{ctx};
        clock_rate = rp.Pop<u32>();
        module_state.clock_frequency = clock_rate;
        LOG_DEBUG(Service_PCV, "called, clock_rate={}", clock_rate);

        IPC::ResponseBuilder rb{ctx, 2};
        rb.Push(ResultSuccess);
    }

    void GetClockRate(HLERequestContext& ctx) {
        LOG_DEBUG(Service_PCV, "called, clock_rate={}", clock_rate);

        IPC::ResponseBuilder rb{ctx, 3};
        rb.Push(ResultSuccess);
        rb.Push<u32>(clock_rate);
    }

    void SetMinVClockRate(HLERequestContext& ctx) {
        IPC::RequestParser rp{ctx};
        module_state.min_v_clock_rate = rp.Pop<u32>();

        IPC::ResponseBuilder rb{ctx, 2};
        rb.Push(ResultSuccess);
    }

    void GetPossibleClockRates(HLERequestContext& ctx) {
        IPC::RequestParser rp{ctx};
        const s32 requested_count = rp.Pop<s32>();

        std::array<u32, 7> rates{};
        std::size_t rate_count{};
        switch (device_code) {
        case DeviceCode::Cpu:
            rates = {1020000000U, 1224000000U, 1785000000U, 0, 0, 0, 0};
            rate_count = 3;
            break;
        case DeviceCode::Gpu:
            rates = {76800000U, 230400000U, 307200000U, 384000000U,
                     460800000U, 691200000U, 768000000U};
            rate_count = 7;
            break;
        case DeviceCode::Emc:
            rates = {1065600000U, 1331200000U, 1600000000U, 0, 0, 0, 0};
            rate_count = 3;
            break;
        default:
            if (clock_rate != 0) {
                rates[0] = clock_rate;
                rate_count = 1;
            }
            break;
        }

        const std::size_t buffer_count =
            ctx.CanWriteBuffer() ? ctx.GetWriteBufferSize() / sizeof(u32) : 0;
        const std::size_t max_requested =
            requested_count > 0 ? static_cast<std::size_t>(requested_count) : 0;
        const std::size_t count = std::min({rate_count, buffer_count, max_requested});
        if (count != 0) {
            ctx.WriteBuffer(rates.data(), count * sizeof(u32));
        }

        IPC::ResponseBuilder rb{ctx, 4};
        rb.Push(ResultSuccess);
        rb.Push<s32>(1); // PcvClockRatesListType::Discrete
        rb.Push<s32>(static_cast<s32>(count));
    }

    void GetDvfsTable(HLERequestContext& ctx) {
        IPC::RequestParser rp{ctx};
        const s32 rate_count = rp.Pop<s32>();
        const s32 voltage_count = rp.Pop<s32>();
        LOG_DEBUG(Service_PCV, "called, rate_count={}, voltage_count={}", rate_count,
                  voltage_count);

        // Encore does not emulate Switch voltage rails. Return an empty table instead of
        // fabricating rate/voltage pairs.
        IPC::ResponseBuilder rb{ctx, 3};
        rb.Push(ResultSuccess);
        rb.Push<s32>(0);
    }

    DeviceCode device_code;
    ModuleState module_state{};
    u32 clock_rate{};
};

class CLKRST final : public ServiceFramework<CLKRST> {
public:
    explicit CLKRST(Core::System& system_, const char* name) : ServiceFramework{system_, name} {
        // clang-format off
        static const FunctionInfo functions[] = {
            {0, &CLKRST::OpenSession, "OpenSession"},
            {1, &CLKRST::GetTemperatureThresholds, "GetTemperatureThresholds"},
            {2, &CLKRST::SetTemperature, "SetTemperature"},
            {3, &CLKRST::RejectUnsupportedDevice, "GetModuleStateTable"},
            {4, &CLKRST::RejectUnsupportedDevice, "GetModuleStateTableEvent"},
            {5, &CLKRST::RejectUnsupportedDevice, "GetModuleStateTableMaxCount"},
        };
        // clang-format on

        RegisterHandlers(functions);
    }

private:
    void OpenSession(HLERequestContext& ctx) {
        IPC::RequestParser rp{ctx};
        const auto device_code = static_cast<DeviceCode>(rp.Pop<u32>());
        const auto unknown_input = rp.Pop<u32>();

        LOG_DEBUG(Service_PCV, "called, device_code={}, input={}", device_code, unknown_input);

        IPC::ResponseBuilder rb{ctx, 2, 0, 1};
        rb.Push(ResultSuccess);
        rb.PushIpcInterface<IClkrstSession>(ctx, system, device_code);
    }

    void GetTemperatureThresholds(HLERequestContext& ctx) {
        IPC::RequestParser rp{ctx};
        const u32 module_id = rp.Pop<u32>();
        LOG_DEBUG(Service_PCV, "called, module_id={}, no emulated thermal thresholds", module_id);

        // Horizon returns a buffer plus a count. The PS5 port has no Switch thermal trip table.
        IPC::ResponseBuilder rb{ctx, 3};
        rb.Push(ResultSuccess);
        rb.Push<u32>(0);
    }

    void SetTemperature(HLERequestContext& ctx) {
        IPC::RequestParser rp{ctx};
        temperature_raw = rp.Pop<u32>();
        LOG_DEBUG(Service_PCV, "called, raw_temperature={}", temperature_raw);

        IPC::ResponseBuilder rb{ctx, 2};
        rb.Push(ResultSuccess);
    }

    u32 temperature_raw{};
private:
    void RejectUnsupportedDevice(HLERequestContext& ctx) {
        // Missing physical Switch power/clock/capture peripheral on the PS5.
        // Do not manufacture successful completion for guest requests.
        IPC::ResponseBuilder rb{ctx, 2};
        rb.Push(ResultUnknown);
    }

};

class CLKRST_A final : public ServiceFramework<CLKRST_A> {
public:
    explicit CLKRST_A(Core::System& system_) : ServiceFramework{system_, "clkrst:a"} {
        // clang-format off
        static const FunctionInfo functions[] = {
            {0, &CLKRST_A::RejectUnsupportedDevice, "ReleaseControl"},
        };
        // clang-format on

        RegisterHandlers(functions);
    }
private:
    void RejectUnsupportedDevice(HLERequestContext& ctx) {
        // Missing physical Switch power/clock/capture peripheral on the PS5.
        // Do not manufacture successful completion for guest requests.
        IPC::ResponseBuilder rb{ctx, 2};
        rb.Push(ResultUnknown);
    }

};

void LoopProcess(Core::System& system) {
    auto server_manager = std::make_unique<ServerManager>(system);

    server_manager->RegisterNamedService("pcv", std::make_shared<PCV>(system));
    // HOS 8 replaced pcv:arb/imm with clkrst. Keep old ports for old guests.
    const auto major = FirmwareManager::GetFirmwareVersion(system).first.major;
    if (major < 8) {
        server_manager->RegisterNamedService("pcv:arb", std::make_shared<PCV_ARB>(system));
        server_manager->RegisterNamedService("pcv:imm", std::make_shared<PCV_IMM>(system));
    } else {
        server_manager->RegisterNamedService("clkrst", std::make_shared<CLKRST>(system, "clkrst"));
        server_manager->RegisterNamedService("clkrst:i", std::make_shared<CLKRST>(system, "clkrst:i"));
        server_manager->RegisterNamedService("clkrst:a", std::make_shared<CLKRST_A>(system));
    }
    ServerManager::RunServer(std::move(server_manager));
}

} // namespace Service::PCV
