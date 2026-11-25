// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

// SPDX-FileCopyrightText: Copyright 2018 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "core/hle/service/ipc_helpers.h"
#include "core/hle/service/sockets/nsd.h"

#include "common/string_util.h"

namespace Service::Sockets {

constexpr Result ResultOverflow{ErrorModule::NSD, 6};

// This is nn::oe::ServerEnvironmentType
enum class ServerEnvironmentType : u8 {
    Dd,
    Lp,
    Sd,
    Sp,
    Dp,
};

// This is nn::nsd::EnvironmentIdentifier
struct EnvironmentIdentifier {
    std::array<u8, 8> identifier;
};
static_assert(sizeof(EnvironmentIdentifier) == 0x8);

NSD::NSD(Core::System& system_, const char* name) : ServiceFramework{system_, name} {
    // clang-format off
    static const FunctionInfo functions[] = {
        {5, &NSD::RejectUnavailableNintendoSetting, "GetSettingUrl"},
        {10, &NSD::RejectUnavailableNintendoSetting, "GetSettingName"},
        {11, &NSD::GetEnvironmentIdentifier, "GetEnvironmentIdentifier"},
        {12, &NSD::RejectUnavailableNintendoSetting, "GetDeviceId"},
        {13, &NSD::RejectUnavailableNintendoSetting, "DeleteSettings"},
        {14, &NSD::RejectUnavailableNintendoSetting, "ImportSettings"},
        {15, &NSD::SetChangeEnvironmentIdentifierDisabled, "SetChangeEnvironmentIdentifierDisabled"},
        {20, &NSD::Resolve, "Resolve"},
        {21, &NSD::ResolveEx, "ResolveEx"},
        {30, &NSD::RejectUnavailableNintendoSetting, "GetNasServiceSetting"},
        {31, &NSD::RejectUnavailableNintendoSetting, "GetNasServiceSettingEx"},
        {40, &NSD::RejectUnavailableNintendoSetting, "GetNasRequestFqdn"},
        {41, &NSD::RejectUnavailableNintendoSetting, "GetNasRequestFqdnEx"},
        {42, &NSD::RejectUnavailableNintendoSetting, "GetNasApiFqdn"},
        {43, &NSD::RejectUnavailableNintendoSetting, "GetNasApiFqdnEx"},
        {50, &NSD::RejectUnavailableNintendoSetting, "GetCurrentSetting"},
        {51, &NSD::UnsupportedTestCommand, "WriteTestParameter"},
        {52, &NSD::UnsupportedTestCommand, "ReadTestParameter"},
        {60, &NSD::UnsupportedTestCommand, "ReadSaveDataFromFsForTest"},
        {61, &NSD::UnsupportedTestCommand, "WriteSaveDataToFsForTest"},
        {62, &NSD::UnsupportedTestCommand, "DeleteSaveDataOfFsForTest"},
        {63, &NSD::IsChangeEnvironmentIdentifierDisabled, "IsChangeEnvironmentIdentifierDisabled"},
        {64, &NSD::RejectUnavailableNintendoSetting, "SetWithoutDomainExchangeFqdns"},
        {100, &NSD::GetApplicationServerEnvironmentType, "GetApplicationServerEnvironmentType"},
        {101, &NSD::RejectUnavailableNintendoSetting, "SetApplicationServerEnvironmentType"},
        {102, &NSD::RejectUnavailableNintendoSetting, "DeleteApplicationServerEnvironmentType"},
    };
    // clang-format on

    RegisterHandlers(functions);
}

static std::string ResolveImpl(const std::string& fqdn_in) {
    // The real implementation makes various substitutions.
    // For now we just return the string as-is, which is good enough when not
    // connecting to real Nintendo servers.
    LOG_WARNING(Service, "(STUBBED) called, fqdn_in={}", fqdn_in);
    return fqdn_in;
}

static Result ResolveCommon(const std::string& fqdn_in, std::array<char, 0x100>& fqdn_out) {
    const auto res = ResolveImpl(fqdn_in);
    if (res.size() >= fqdn_out.size()) {
        return ResultOverflow;
    }
    std::memcpy(fqdn_out.data(), res.c_str(), res.size() + 1);
    return ResultSuccess;
}

void NSD::SetChangeEnvironmentIdentifierDisabled(HLERequestContext& ctx) {
    IPC::RequestParser rp{ctx};
    change_environment_identifier_disabled = rp.Pop<bool>();

    LOG_DEBUG(Service, "called, change_environment_identifier_disabled={}",
              change_environment_identifier_disabled);

    IPC::ResponseBuilder rb{ctx, 2};
    rb.Push(ResultSuccess);
}

void NSD::IsChangeEnvironmentIdentifierDisabled(HLERequestContext& ctx) {
    LOG_DEBUG(Service, "called, disabled={}", change_environment_identifier_disabled);

    IPC::ResponseBuilder rb{ctx, 3};
    rb.Push(ResultSuccess);
    rb.Push(change_environment_identifier_disabled);
}

void NSD::Resolve(HLERequestContext& ctx) {
    const std::string fqdn_in = Common::StringFromBuffer(ctx.ReadBuffer(0));

    std::array<char, 0x100> fqdn_out{};
    const Result res = ResolveCommon(fqdn_in, fqdn_out);

    ctx.WriteBuffer(fqdn_out);
    IPC::ResponseBuilder rb{ctx, 2};
    rb.Push(res);
}

void NSD::ResolveEx(HLERequestContext& ctx) {
    const std::string fqdn_in = Common::StringFromBuffer(ctx.ReadBuffer(0));

    std::array<char, 0x100> fqdn_out{};
    const Result res = ResolveCommon(fqdn_in, fqdn_out);

    if (res.IsError()) {
        IPC::ResponseBuilder rb{ctx, 2};
        rb.Push(res);
        return;
    }

    ctx.WriteBuffer(fqdn_out);
    IPC::ResponseBuilder rb{ctx, 4};
    rb.Push(ResultSuccess);
    rb.Push(ResultSuccess);
}

void NSD::GetEnvironmentIdentifier(HLERequestContext& ctx) {
    // Nintendo production environment switching is intentionally disabled; use the stable local
    // production-style identifier already expected by Eden's pass-through resolver.
    constexpr EnvironmentIdentifier lp1 = {
        .identifier = {'l', 'p', '1', '\0', '\0', '\0', '\0', '\0'}};
    ctx.WriteBuffer(lp1);
    LOG_DEBUG(Service, "Returning synthetic NSD environment identifier lp1");

    IPC::ResponseBuilder rb{ctx, 2};
    rb.Push(ResultSuccess);
}

void NSD::GetApplicationServerEnvironmentType(HLERequestContext& ctx) {
    IPC::ResponseBuilder rb{ctx, 3};
    rb.Push(ResultSuccess);
    rb.Push(static_cast<u32>(ServerEnvironmentType::Lp));
}

void NSD::UnsupportedTestCommand(HLERequestContext& ctx) {
    LOG_WARNING(Service, "NSD test/debug command is not supported by the production runtime");
    IPC::ResponseBuilder rb{ctx, 2};
    rb.Push(IPC::ResultNotSupported);
}

void NSD::RejectUnavailableNintendoSetting(HLERequestContext& ctx) {
    LOG_WARNING(Service, "NSD Nintendo network setting is unavailable in Encore");
    IPC::ResponseBuilder rb{ctx, 2};
    rb.Push(IPC::ResultNotSupported);
}

NSD::~NSD() = default;

} // namespace Service::Sockets
