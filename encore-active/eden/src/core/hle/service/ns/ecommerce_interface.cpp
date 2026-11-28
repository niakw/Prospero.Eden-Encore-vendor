// SPDX-FileCopyrightText: Copyright 2024 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "core/hle/service/ipc_helpers.h"
#include "core/hle/service/ns/ecommerce_interface.h"

namespace Service::NS {

IECommerceInterface::IECommerceInterface(Core::System& system_)
    : ServiceFramework{system_, "IECommerceInterface"} {
    // clang-format off
    static const FunctionInfo functions[] = {
        {0, &IECommerceInterface::RejectUnavailableECommerce, "RequestLinkDevice"},
        {1, &IECommerceInterface::RejectUnavailableECommerce, "RequestCleanupAllPreInstalledApplications"},
        {2, &IECommerceInterface::RejectUnavailableECommerce, "RequestCleanupPreInstalledApplication"},
        {3, &IECommerceInterface::RejectUnavailableECommerce, "RequestSyncRights"},
        {4, &IECommerceInterface::RejectUnavailableECommerce, "RequestUnlinkDevice"},
        {5, &IECommerceInterface::RejectUnavailableECommerce, "RequestRevokeAllELicense"},
        {6, &IECommerceInterface::RejectUnavailableECommerce, "RequestSyncRightsBasedOnAssignedELicenses"},
    };
    // clang-format on

    RegisterHandlers(functions);
}

void IECommerceInterface::RejectUnavailableECommerce(HLERequestContext& ctx) {
    LOG_WARNING(Service_NS, "e-commerce backend is unavailable in Encore");
    IPC::ResponseBuilder rb{ctx, 2};
    rb.Push(IPC::ResultNotSupported);
}

IECommerceInterface::~IECommerceInterface() = default;

} // namespace Service::NS
