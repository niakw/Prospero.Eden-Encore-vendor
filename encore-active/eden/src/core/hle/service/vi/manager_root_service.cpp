// SPDX-FileCopyrightText: Copyright 2018 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "core/hle/service/cmif_serialization.h"
#include "core/hle/service/ipc_helpers.h"
#include "core/hle/service/nvnflinger/hos_binder_driver.h"
#include "core/hle/service/vi/application_display_service.h"
#include "core/hle/service/vi/container.h"
#include "core/hle/service/vi/manager_root_service.h"
#include "core/hle/service/vi/service_creator.h"
#include "core/hle/service/vi/vi.h"
#include "core/hle/service/vi/vi_types.h"

namespace Service::VI {

IManagerRootService::IManagerRootService(Core::System& system_,
                                         std::shared_ptr<Container> container)
    : ServiceFramework{system_, "vi:m"}, m_container{std::move(container)} {
    static const FunctionInfo functions[] = {
        {2, C<&IManagerRootService::GetDisplayService>, "GetDisplayService"},
        {3, C<&IManagerRootService::GetDisplayService>, "GetDisplayServiceWithProxyNameExchange"},
        {100, &IManagerRootService::RejectUnavailableFatalDisplay, "PrepareFatal"},
        {101, &IManagerRootService::RejectUnavailableFatalDisplay, "ShowFatal"},
        {102, &IManagerRootService::RejectUnavailableFatalDisplay, "DrawFatalRectangle"},
        {103, &IManagerRootService::RejectUnavailableFatalDisplay, "DrawFatalText32"},
    };
    RegisterHandlers(functions);
}

void IManagerRootService::RejectUnavailableFatalDisplay(HLERequestContext& ctx) {
    // Encore has no Horizon fatal-screen drawing backend on PS5. Keep the normal
    // vi:m display-service path intact and fail the fatal-only surface explicitly.
    IPC::ResponseBuilder rb{ctx, 2};
    rb.Push(IPC::ResultNotSupported);
}

IManagerRootService::~IManagerRootService() = default;

Result IManagerRootService::GetDisplayService(
    Out<SharedPointer<IApplicationDisplayService>> out_application_display_service, Policy policy) {
    LOG_DEBUG(Service_VI, "called");
    R_RETURN(GetApplicationDisplayService(out_application_display_service, system, m_container,
                                          Permission::Manager, policy));
}

} // namespace Service::VI
