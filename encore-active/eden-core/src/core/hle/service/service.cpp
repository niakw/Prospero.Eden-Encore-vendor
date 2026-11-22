#include "performance.h"
#include "core/arm/debug.h"
// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

// SPDX-FileCopyrightText: Copyright 2018 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <chrono>
#include <deque>
#include <memory>
#include <mutex>
#include <string>
#include <fmt/ranges.h>
#include <string_view>
#include <thread>
#include "common/assert.h"
#include "common/logging.h"
#include "common/settings.h"
#include "core/core.h"
#include "core/hle/ipc.h"
#include "core/hle/kernel/k_process.h"
#include "core/hle/kernel/k_event.h"
#include "core/hle/kernel/kernel.h"
#include "core/hle/kernel/svc_results.h"
#include "core/hle/service/ipc_helpers.h"
#include "core/hle/service/kernel_helpers.h"
#include "core/hle/service/service.h"
#include "core/hle/service/sm/sm.h"
#include "core/reporter.h"

namespace Service {

[[nodiscard]] static bool IsSafeOptionalNoOp(std::string_view service_name,
                                             std::string_view function_name) {
    // not implemented yet on Encore: these commands only publish optional
    // notifications/telemetry. Eden does not require their side effects for
    // gameplay, so acknowledge them without inventing a larger subsystem.
    if (service_name == "pdm:ntfy") {
        return function_name == "NotifyAppletEvent" ||
               function_name == "NotifyOperationModeChangeEvent" ||
               function_name == "NotifyPowerStateChangeEvent" ||
               function_name == "NotifyClearAllEvent" ||
               function_name == "NotifyEventForDebug" ||
               function_name == "SuspendUserAccountEventService" ||
               function_name == "ResumeUserAccountEventService" ||
               function_name == "NotifyLibraryAppletEvent";
    }
    if (service_name == "ILogger") {
        return function_name == "TransmitHashedLog" || function_name == "DevNotify";
    }
    if (service_name == "erpt:c") {
        return function_name == "SetInitialLaunchSettingsCompletionTime" ||
               function_name == "ClearInitialLaunchSettingsCompletionTime" ||
               function_name == "UpdatePowerOnTime" ||
               function_name == "UpdateApplicationLaunchTime" ||
               function_name == "ClearApplicationLaunchTime" ||
               function_name == "RegisterRunningApplet" ||
               function_name == "UnregisterRunningApplet" ||
               function_name == "UpdateAppletSuspendedDuration" ||
               function_name == "InvalidateForcedShutdownDetection";
    }
    if (service_name == "eupld:c") {
        return function_name == "SetUrl" || function_name == "ImportCrt" ||
               function_name == "ImportPki" || function_name == "SetAutoUpload";
    }
    if (service_name == "ICommonStateGetter") {
        return function_name == "AllowToEnterSleep" ||
               function_name == "DisallowToEnterSleep";
    }
    if (service_name == "ISelfController") {
        return function_name == "SetDesirableKeyboardLayout" ||
               function_name == "ReportMultimediaError";
    }
    return false;
}

/// @brief Creates a function string for logging, complete with the name (or header code, depending
/// on what's passed in) the port name, and all the cmd_buf arguments.
[[maybe_unused]] static std::string MakeFunctionString(std::string_view name, std::string_view port_name, const u32* cmd_buf) {
    // Number of params == bits 0-5 + bits 6-11
    int num_params = (cmd_buf[0] & 0x3F) + ((cmd_buf[0] >> 6) & 0x3F);
    std::string function_string = fmt::format("fn '{}': port={}", name, port_name);
    for (int i = 1; i <= num_params; ++i)
        function_string += fmt::format(", cmd_buf[{}]={:#x}", i, cmd_buf[i]);
    return function_string;
}

ServiceFrameworkBase::ServiceFrameworkBase(Core::System& system_, const char* service_name_, u32 max_sessions_, InvokerFn* handler_invoker_)
    : SessionRequestHandler(system_.Kernel(), service_name_)
    , system{system_}
    , service_name{service_name_}
    , handler_invoker{handler_invoker_}
    , max_sessions{max_sessions_}
{}

ServiceFrameworkBase::~ServiceFrameworkBase() {
    // Wait for other threads to release access before destroying
    const auto guard = ServiceFrameworkBase::LockService();
    if (stub_event) {
        stub_context->CloseEvent(stub_event);
    }
}

namespace {
constexpr u32 StubZeroedWords = 32;
void PushStubZeroes(IPC::ResponseBuilder& rb) {
    for (u32 i = 0; i < StubZeroedWords; ++i) rb.Push<u32>(0);
}
// Explicit NS2 AM getters return this placeholder interface instead of
// an invalid null object; its unknown requests receive zeroed words.
class INx2PlaceholderInterface final : public ServiceFramework<INx2PlaceholderInterface> {
public:
    explicit INx2PlaceholderInterface(Core::System& system, const char* name)
        : ServiceFramework{system, name} {}
};
const char* InternPlaceholderName(std::string_view owner, u32 command) {
    static std::mutex names_mutex;
    static std::deque<std::string> names;
    const auto label = fmt::format("nx2:{}::{}", owner, command);
    std::scoped_lock lock{names_mutex};
    for (const auto& name : names) {
        if (name == label) return name.c_str();
    }
    return names.emplace_back(label).c_str();
}
} // namespace

void ServiceFrameworkBase::StubReplyZeroed(HLERequestContext& ctx) {
    LOG_WARNING(Service, "(STUBBED) NS2 {}::{} -> zero reply", service_name, ctx.GetCommand());
    IPC::ResponseBuilder rb{ctx, 2 + StubZeroedWords};
    rb.Push(ResultSuccess);
    PushStubZeroes(rb);
}
void ServiceFrameworkBase::StubReplyEvent(HLERequestContext& ctx) {
    if (!stub_event) {
        stub_context = std::make_unique<KernelHelpers::ServiceContext>(system, service_name);
        stub_event = stub_context->CreateEvent(fmt::format("{}:StubEvent", service_name));
    }
    LOG_WARNING(Service, "(STUBBED) NS2 {}::{} -> unsignaled event", service_name, ctx.GetCommand());
    IPC::ResponseBuilder rb{ctx, 2 + StubZeroedWords, 1, 0};
    rb.Push(ResultSuccess);
    PushStubZeroes(rb);
    rb.PushCopyObjects(ctx, stub_event->GetReadableEvent());
}
void ServiceFrameworkBase::StubReplyInterface(HLERequestContext& ctx) {
    const char* name = InternPlaceholderName(service_name, ctx.GetCommand());
    LOG_WARNING(Service, "(STUBBED) NS2 {}::{} -> interface {}", service_name, ctx.GetCommand(), name);
    IPC::ResponseBuilder rb{ctx, 2, 0, 1};
    rb.Push(ResultSuccess);
    rb.PushIpcInterface<INx2PlaceholderInterface>(ctx, system, name);
}

void ServiceFrameworkBase::RegisterHandlersBase(const FunctionInfoBase* functions, std::size_t n) {
    // Usually this array is sorted by id already, so hint to insert at the end
    handlers.reserve(handlers.size() + n);
    for (std::size_t i = 0; i < n; ++i)
        handlers.emplace_hint(handlers.cend(), functions[i].expected_header, functions[i]);
}

void ServiceFrameworkBase::RegisterHandlersBaseTipc(const FunctionInfoBase* functions, std::size_t n) {
    // Usually this array is sorted by id already, so hint to insert at the end
    handlers_tipc.reserve(handlers_tipc.size() + n);
    for (std::size_t i = 0; i < n; ++i)
        handlers_tipc.emplace_hint(handlers_tipc.cend(), functions[i].expected_header, functions[i]);
}

void ServiceFrameworkBase::ReportUnimplementedFunction(HLERequestContext& ctx,
                                                       const FunctionInfoBase* info) {
    // Only interfaces created by an explicit NS2 stub getter may use
    // this zero-filled unknown-command fallback; all real services stay closed.
    if (std::string_view{service_name}.starts_with("nx2:")) {
        StubReplyZeroed(ctx);
        return;
    }
    auto cmd_buf = ctx.CommandBuffer();
    std::string function_name = info == nullptr ? "<unknown>" : info->name;

    fmt::memory_buffer buf;
    fmt::format_to(std::back_inserter(buf), "function '{}({})': port='{}' cmd_buf={{[0]={:#x}", ctx.GetCommand(), function_name, service_name, cmd_buf[0]);
    for (int i = 1; i <= 8; ++i)
        fmt::format_to(std::back_inserter(buf), ", [{}]={:#x}", i, cmd_buf[i]);
    buf.push_back('}');

    if (info != nullptr && IsSafeOptionalNoOp(service_name, function_name)) {
        // not implemented yet on Encore: optional service with no guest-visible
        // output contract needed by Eden. Deliberately succeed as a no-op.
        LOG_WARNING(Service, "(STUBBED) optional no-op {}::{} ({})", service_name,
                    ctx.GetCommand(), function_name);
        IPC::ResponseBuilder rb{ctx, 2};
        rb.Push(ResultSuccess);
        return;
    }

    system.GetReporter().SaveUnimplementedFunctionReport(ctx, ctx.GetCommand(), function_name, service_name);
    UNIMPLEMENTED_MSG("Unknown / unimplemented {}", fmt::to_string(buf));
    if (Settings::values.use_auto_stub) {
        LOG_WARNING(Service, "Using auto stub fallback!");
        IPC::ResponseBuilder rb{ctx, 2};
        rb.Push(ResultSuccess);
    } else {
        // Unknown IDs need a complete IPC reply too. Previously, only known
        // IDs with null handlers were fail-closed; an unregistered NS2 command
        // could fall through without building any response at all.
        LOG_WARNING(Service, "Unsupported IPC command: {}::{} ({})", service_name,
                    ctx.GetCommand(), function_name);
        IPC::ResponseBuilder rb{ctx, 2};
        rb.Push(Kernel::ResultNotImplemented);
    }
}

void ServiceFrameworkBase::InvokeRequest(HLERequestContext& ctx) {
    auto it = handlers.find(ctx.GetCommand());
    FunctionInfoBase const* info = it == handlers.end() ? nullptr : &it->second;
    if (info == nullptr || info->handler_callback == nullptr)
        return ReportUnimplementedFunction(ctx, info);

    LOG_TRACE(Service, "{}", MakeFunctionString(info->name, GetServiceName(), ctx.CommandBuffer()));
    handler_invoker(this, info->handler_callback, ctx);


}

void ServiceFrameworkBase::InvokeRequestTipc(HLERequestContext& ctx) {
    auto it = handlers_tipc.find(ctx.GetCommand());
    FunctionInfoBase const* info = it == handlers_tipc.end() ? nullptr : &it->second;
    if (info == nullptr || info->handler_callback == nullptr)
        return ReportUnimplementedFunction(ctx, info);

    LOG_TRACE(Service, "{}", MakeFunctionString(info->name, GetServiceName(), ctx.CommandBuffer()));
    handler_invoker(this, info->handler_callback, ctx);
}

Result ServiceFrameworkBase::HandleSyncRequest(Kernel::KServerSession& session,
                                               HLERequestContext& ctx) {
    const auto guard = LockService();

    Result result = ResultSuccess;

    switch (ctx.GetCommandType()) {
    case IPC::CommandType::Close:
    case IPC::CommandType::TIPC_Close: {
        IPC::ResponseBuilder rb{ctx, 2};
        rb.Push(ResultSuccess);
        result = IPC::ResultSessionClosed;
        break;
    }
    case IPC::CommandType::ControlWithContext:
    case IPC::CommandType::Control: {
        system.ServiceManager().InvokeControlRequest(ctx);
        break;
    }
    case IPC::CommandType::RequestWithContext:
    case IPC::CommandType::Request: {
        {
            const bool profile_hle = ::Eden::Performance::detailed_gpu_profile.load(std::memory_order_relaxed);
            const auto hle_start = profile_hle ? ::Eden::Performance::NowNs() : 0;
        InvokeRequest(ctx);
            if (profile_hle)
                ::Eden::Performance::RecordHle(service_name, ctx.GetCommand(), ::Eden::Performance::NowNs() - hle_start);
            if (::Eden::Performance::trace_fs_callers.load(std::memory_order_relaxed) && std::string_view{service_name} == "IFileSystem") {
                std::string frames;
                for (const auto& entry : Core::GetBacktrace(&ctx.GetThread()))
                    frames += fmt::format(" {}+{:X}", entry.module, entry.offset);
                LOG_WARNING(Service_FS, "EDEN_FS_CALLER command={} thread={} name={} frames={}", ctx.GetCommand(),
                            ctx.GetThread().GetThreadId(), Core::GetThreadName(&ctx.GetThread()).value_or("-"), frames);
            }
        }
        break;
    }
    default:
        if (ctx.IsTipc()) {
        {
            const bool profile_hle = ::Eden::Performance::detailed_gpu_profile.load(std::memory_order_relaxed);
            const auto hle_start = profile_hle ? ::Eden::Performance::NowNs() : 0;
            InvokeRequestTipc(ctx);
            if (profile_hle)
                ::Eden::Performance::RecordHle(service_name, ctx.GetCommand(), ::Eden::Performance::NowNs() - hle_start);
            if (::Eden::Performance::trace_fs_callers.load(std::memory_order_relaxed) && std::string_view{service_name} == "IFileSystem") {
                std::string frames;
                for (const auto& entry : Core::GetBacktrace(&ctx.GetThread()))
                    frames += fmt::format(" {}+{:X}", entry.module, entry.offset);
                LOG_WARNING(Service_FS, "EDEN_FS_CALLER command={} thread={} name={} frames={}", ctx.GetCommand(),
                            ctx.GetThread().GetThreadId(), Core::GetThreadName(&ctx.GetThread()).value_or("-"), frames);
            }
        }
            break;
        }

        UNIMPLEMENTED_MSG("command_type={}", ctx.GetCommandType());
        break;
    }

    // If emulation was shutdown, we are closing service threads, do not write the response back to
    // memory that may be shutting down as well.
    if (system.IsPoweredOn()) {
        ctx.WriteToOutgoingCommandBuffer();
    }

    return result;
}

} // namespace Service
