// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

// SPDX-FileCopyrightText: Copyright 2018 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <algorithm>
#include <array>
#include <cstdlib>
#include <ctime>
#include <vector>

#include <openssl/bn.h>
#include "common/logging.h"
#include "common/settings.h"
#include "core/hle/api_version.h"
#include "core/hle/service/ipc_helpers.h"
#include "core/hle/service/server_manager.h"
#include "core/hle/service/spl/spl.h"
#include "core/hle/service/spl/spl_module.h"

namespace Service::SPL {

Module::Interface::Interface(Core::System& system_, std::shared_ptr<Module> module_,
                             const char* name)
    : ServiceFramework{system_, name}, module{std::move(module_)},
      rng(Settings::values.rng_seed_enabled ? Settings::values.rng_seed.GetValue()
                                            : static_cast<u32>(std::time(nullptr))) {}

Module::Interface::~Interface() = default;

void Module::Interface::GetConfig(HLERequestContext& ctx) {
    IPC::RequestParser rp{ctx};
    const auto config_item = rp.PopEnum<ConfigItem>();

    // This should call svcCallSecureMonitor with the appropriate args.
    // Since we do not have it implemented yet, we will use this for now.
    u64 smc_result{};
    const auto result_code = GetConfigImpl(&smc_result, config_item);

    if (result_code != ResultSuccess) {
        LOG_ERROR(Service_SPL, "called, config_item={}, result_code={}", config_item,
                  result_code.raw);

        IPC::ResponseBuilder rb{ctx, 2};
        rb.Push(result_code);
        return;
    }

    LOG_DEBUG(Service_SPL, "called, config_item={}, result_code={}, smc_result={}", config_item,
              result_code.raw, smc_result);

    IPC::ResponseBuilder rb{ctx, 4};
    rb.Push(result_code);
    rb.Push(smc_result);
}

void Module::Interface::ModularExponentiate(HLERequestContext& ctx) {
    constexpr std::size_t Rsa2048BlockSize = 0x100;

    const auto out_size = ctx.GetWriteBufferSize();
    if (!ctx.CanWriteBuffer(0) || !ctx.CanReadBuffer(0) || !ctx.CanReadBuffer(1) ||
        !ctx.CanReadBuffer(2) || out_size > Rsa2048BlockSize ||
        ctx.GetReadBufferSize(0) > Rsa2048BlockSize ||
        ctx.GetReadBufferSize(1) > Rsa2048BlockSize ||
        ctx.GetReadBufferSize(2) > Rsa2048BlockSize) {
        LOG_ERROR(Service_SPL,
                  "ModularExponentiate invalid buffers: out={:#x}, base={:#x}, exp={:#x}, mod={:#x}",
                  out_size, ctx.GetReadBufferSize(0), ctx.GetReadBufferSize(1),
                  ctx.GetReadBufferSize(2));
        IPC::ResponseBuilder rb{ctx, 2};
        rb.Push(ResultInvalidSize);
        return;
    }

    const auto base = ctx.ReadBuffer(0);
    const auto exp = ctx.ReadBuffer(1);
    const auto mod = ctx.ReadBuffer(2);

    BIGNUM* base_bn = BN_bin2bn(base.data(), static_cast<int>(base.size()), nullptr);
    BIGNUM* exp_bn = BN_bin2bn(exp.data(), static_cast<int>(exp.size()), nullptr);
    BIGNUM* mod_bn = BN_bin2bn(mod.data(), static_cast<int>(mod.size()), nullptr);
    BIGNUM* result_bn = BN_new();
    BN_CTX* bn_ctx = BN_CTX_new();

    const auto cleanup = [&] {
        BN_CTX_free(bn_ctx);
        BN_free(result_bn);
        BN_free(mod_bn);
        BN_free(exp_bn);
        BN_free(base_bn);
    };

    if (base_bn == nullptr || exp_bn == nullptr || mod_bn == nullptr || result_bn == nullptr ||
        bn_ctx == nullptr) {
        cleanup();
        IPC::ResponseBuilder rb{ctx, 2};
        rb.Push(ResultSecureMonitorError);
        return;
    }
    if (BN_is_zero(mod_bn)) {
        cleanup();
        IPC::ResponseBuilder rb{ctx, 2};
        rb.Push(ResultSecureMonitorInvalidArgument);
        return;
    }

    std::array<u8, Rsa2048BlockSize> result{};
    if (BN_mod_exp(result_bn, base_bn, exp_bn, mod_bn, bn_ctx) != 1 ||
        BN_bn2binpad(result_bn, result.data(), static_cast<int>(result.size())) !=
            static_cast<int>(result.size())) {
        cleanup();
        IPC::ResponseBuilder rb{ctx, 2};
        rb.Push(ResultSecureMonitorError);
        return;
    }
    cleanup();

    if (out_size != 0 && ctx.WriteBuffer(result.data(), out_size) != out_size) {
        IPC::ResponseBuilder rb{ctx, 2};
        rb.Push(ResultSecureMonitorError);
        return;
    }

    LOG_DEBUG(Service_SPL, "ModularExponentiate completed, out_size={:#x}", out_size);
    IPC::ResponseBuilder rb{ctx, 2};
    rb.Push(ResultSuccess);
}

void Module::Interface::GenerateAesKek(HLERequestContext& ctx) {
    IPC::RequestParser rp{ctx};
    [[maybe_unused]] const auto key_source = rp.PopRaw<KeySource>();
    const auto generation = rp.Pop<u32>();
    const auto option = rp.Pop<u32>();

    LOG_WARNING(Service_SPL,
                "GenerateAesKek unavailable without a secure-monitor key derivation backend; "
                "generation={:#x}, option={:#x}",
                generation, option);

    IPC::ResponseBuilder rb{ctx, 2};
    rb.Push(ResultSecureMonitorNotImplemented);
}

void Module::Interface::GenerateAesKey(HLERequestContext& ctx) {
    IPC::RequestParser rp{ctx};
    [[maybe_unused]] const auto access_key = rp.PopRaw<AccessKey>();
    [[maybe_unused]] const auto key_source = rp.PopRaw<KeySource>();

    LOG_WARNING(Service_SPL,
                "GenerateAesKey unavailable without a secure-monitor key derivation backend");

    IPC::ResponseBuilder rb{ctx, 2};
    rb.Push(ResultSecureMonitorNotImplemented);
}

void Module::Interface::SetConfig(HLERequestContext& ctx) {
    UNIMPLEMENTED_MSG("SetConfig is not implemented!");

    IPC::ResponseBuilder rb{ctx, 2};
    rb.Push(ResultSecureMonitorNotImplemented);
}

void Module::Interface::GenerateRandomBytes(HLERequestContext& ctx) {
    LOG_DEBUG(Service_SPL, "called");

    const std::size_t size = ctx.GetWriteBufferSize();

    std::uniform_int_distribution<u16> distribution(0, (std::numeric_limits<u8>::max)());
    std::vector<u8> data(size);
    std::generate(data.begin(), data.end(), [&] { return static_cast<u8>(distribution(rng)); });

    ctx.WriteBuffer(data);

    IPC::ResponseBuilder rb{ctx, 2};
    rb.Push(ResultSuccess);
}

void Module::Interface::IsDevelopment(HLERequestContext& ctx) {
    u64 hardware_state{};
    const auto result = GetConfigImpl(&hardware_state, ConfigItem::HardwareState);
    if (result != ResultSuccess) {
        IPC::ResponseBuilder rb{ctx, 2};
        rb.Push(result);
        return;
    }

    const bool is_development =
        hardware_state == static_cast<u64>(HardwareState::Development);
    LOG_DEBUG(Service_SPL, "called, is_development={}", is_development);

    IPC::ResponseBuilder rb{ctx, 3};
    rb.Push(ResultSuccess);
    rb.Push<u8>(is_development);
}

void Module::Interface::SetBootReason(HLERequestContext& ctx) {
    IPC::RequestParser rp{ctx};
    const auto boot_reason = rp.PopRaw<BootReasonValue>();

    std::scoped_lock lk{module->boot_reason_mutex};
    if (module->boot_reason_set) {
        IPC::ResponseBuilder rb{ctx, 2};
        rb.Push(ResultBootReasonAlreadySet);
        return;
    }

    module->boot_reason = boot_reason;
    module->boot_reason_set = true;

    LOG_DEBUG(Service_SPL, "SetBootReason value={:#010x}", boot_reason.value);
    IPC::ResponseBuilder rb{ctx, 2};
    rb.Push(ResultSuccess);
}

void Module::Interface::GetBootReason(HLERequestContext& ctx) {
    BootReasonValue boot_reason{};
    {
        std::scoped_lock lk{module->boot_reason_mutex};
        if (!module->boot_reason_set) {
            IPC::ResponseBuilder rb{ctx, 2};
            rb.Push(ResultBootReasonNotSet);
            return;
        }
        boot_reason = module->boot_reason;
    }

    IPC::ResponseBuilder rb{ctx, 3};
    rb.Push(ResultSuccess);
    rb.PushRaw(boot_reason);
}

Result Module::Interface::GetConfigImpl(u64* out_config, ConfigItem config_item) const {
    switch (config_item) {
    case ConfigItem::DisableProgramVerification:
    case ConfigItem::DramId:
    case ConfigItem::SecurityEngineInterruptNumber:
    case ConfigItem::FuseVersion:
    case ConfigItem::HardwareType:
        return ResultSecureMonitorNotImplemented;
    case ConfigItem::HardwareState:
        // Encore exposes retail/production Horizon semantics. It does not emulate a
        // development unit, and IsDevelopment is defined in terms of this config item.
        *out_config = static_cast<u64>(HardwareState::Production);
        return ResultSuccess;
    case ConfigItem::IsRecoveryBoot:
    case ConfigItem::DeviceId:
    case ConfigItem::BootReason:
    case ConfigItem::MemoryMode:
    case ConfigItem::IsDevelopmentFunctionEnabled:
    case ConfigItem::KernelConfiguration:
    case ConfigItem::IsChargerHiZModeEnabled:
    case ConfigItem::QuestState:
    case ConfigItem::RegulatorType:
    case ConfigItem::DeviceUniqueKeyGeneration:
    case ConfigItem::Package2Hash:
        return ResultSecureMonitorNotImplemented;
    case ConfigItem::ExosphereApiVersion:
        // Get information about the current exosphere version.
        *out_config = (u64{HLE::ApiVersion::ATMOSPHERE_RELEASE_VERSION_MAJOR} << 56) |
                      (u64{HLE::ApiVersion::ATMOSPHERE_RELEASE_VERSION_MINOR} << 48) |
                      (u64{HLE::ApiVersion::ATMOSPHERE_RELEASE_VERSION_MICRO} << 40) |
                      (static_cast<u64>(HLE::ApiVersion::GetTargetFirmware()));
        return ResultSuccess;
    case ConfigItem::ExosphereNeedsReboot:
        // We are executing, so we aren't in the process of rebooting.
        *out_config = u64{0};
        return ResultSuccess;
    case ConfigItem::ExosphereNeedsShutdown:
        // We are executing, so we aren't in the process of shutting down.
        *out_config = u64{0};
        return ResultSuccess;
    case ConfigItem::ExosphereGitCommitHash:
        // Get information about the current exosphere git commit hash.
        *out_config = u64{0};
        return ResultSuccess;
    case ConfigItem::ExosphereHasRcmBugPatch:
        // Get information about whether this unit has the RCM bug patched.
        *out_config = u64{0};
        return ResultSuccess;
    case ConfigItem::ExosphereBlankProdInfo:
        // Get whether this unit should simulate a "blanked" PRODINFO.
        *out_config = u64{0};
        return ResultSuccess;
    case ConfigItem::ExosphereAllowCalWrites:
        // Get whether this unit should allow writing to the calibration partition.
        *out_config = u64{0};
        return ResultSuccess;
    case ConfigItem::ExosphereEmummcType:
        // Get what kind of emummc this unit has active.
        *out_config = u64{0};
        return ResultSuccess;
    case ConfigItem::ExospherePayloadAddress:
        // Gets the physical address of the reboot payload buffer, if one exists.
        return ResultSecureMonitorNotInitialized;
    case ConfigItem::ExosphereLogConfiguration:
        // Get the log configuration.
        *out_config = u64{0};
        return ResultSuccess;
    case ConfigItem::ExosphereForceEnableUsb30:
        // Get whether usb 3.0 should be force-enabled.
        *out_config = u64{0};
        return ResultSuccess;
    default:
        return ResultSecureMonitorInvalidArgument;
    }
}

class CSRNG final : public Module::Interface {
public:
    explicit CSRNG(Core::System& system_, std::shared_ptr<Module> module_)
        : Interface(system_, std::move(module_), "csrng") {
        static const FunctionInfo functions[] = {
            {0, &CSRNG::GenerateRandomBytes, "GenerateRandomBytes"},
        };
        RegisterHandlers(functions);
    }
    ~CSRNG() override = default;
};

void LoopProcess(Core::System& system) {
    auto server_manager = std::make_unique<ServerManager>(system);
    auto module = std::make_shared<Module>();

    server_manager->RegisterNamedService("csrng", std::make_shared<CSRNG>(system, module));
    server_manager->RegisterNamedService("spl:", std::make_shared<SPL>(system, module));
    server_manager->RegisterNamedService("spl:mig", std::make_shared<SPL_MIG>(system, module));
    server_manager->RegisterNamedService("spl:fs", std::make_shared<SPL_FS>(system, module));
    server_manager->RegisterNamedService("spl:ssl", std::make_shared<SPL_SSL>(system, module));
    server_manager->RegisterNamedService("spl:es", std::make_shared<SPL_ES>(system, module));
    server_manager->RegisterNamedService("spl:manu", std::make_shared<SPL_MANU>(system, module));
    ServerManager::RunServer(std::move(server_manager));
}

} // namespace Service::SPL
