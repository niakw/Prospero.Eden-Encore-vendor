// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

// SPDX-FileCopyrightText: Copyright 2024 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "core/hle/service/cmif_serialization.h"
#include "core/hle/service/ipc_helpers.h"
#include "core/hle/service/ns/dynamic_rights_interface.h"

namespace Service::NS {

IDynamicRightsInterface::IDynamicRightsInterface(Core::System& system_)
    : ServiceFramework{system_, "DynamicRightsInterface"} {
    // clang-format off
    static const FunctionInfo functions[] = {
        {0, &IDynamicRightsInterface::RejectUnavailableSwitchBackend, "RequestApplicationRightsOnServer"},
        {1, &IDynamicRightsInterface::RejectUnavailableSwitchBackend, "RequestAssignRights"},
        {4, &IDynamicRightsInterface::RejectUnavailableSwitchBackend, "DeprecatedRequestAssignRightsToResume"},
        {5, D<&IDynamicRightsInterface::VerifyActivatedRightsOwners>, "VerifyActivatedRightsOwners"},
        {6, &IDynamicRightsInterface::RejectUnavailableSwitchBackend, "DeprecatedGetApplicationRightsStatus"},
        {7, &IDynamicRightsInterface::RejectUnavailableSwitchBackend, "RequestPrefetchForDynamicRights"},
        {8, &IDynamicRightsInterface::RejectUnavailableSwitchBackend, "GetDynamicRightsState"},
        {9, &IDynamicRightsInterface::RejectUnavailableSwitchBackend, "RequestApplicationRightsOnServerToResume"},
        {10, &IDynamicRightsInterface::RejectUnavailableSwitchBackend, "RequestAssignRightsToResume"},
        {11, &IDynamicRightsInterface::RejectUnavailableSwitchBackend, "GetActivatedRightsUsers"},
        {12, &IDynamicRightsInterface::RejectUnavailableSwitchBackend, "GetApplicationRightsStatus"},
        {13, D<&IDynamicRightsInterface::GetRunningApplicationStatus>, "GetRunningApplicationStatus"},
        {14, &IDynamicRightsInterface::RejectUnavailableSwitchBackend, "SelectApplicationLicense"},
        {15, &IDynamicRightsInterface::RejectUnavailableSwitchBackend, "RequestContentsAuthorizationToken"},
        {16, &IDynamicRightsInterface::RejectUnavailableSwitchBackend, "QualifyUser"},
        {17, &IDynamicRightsInterface::RejectUnavailableSwitchBackend, "QualifyUserWithProcessId"},
        {18, D<&IDynamicRightsInterface::NotifyApplicationRightsCheckStart>, "NotifyApplicationRightsCheckStart"},
        {19, &IDynamicRightsInterface::RejectUnavailableSwitchBackend, "UpdateUserList"},
        {20, &IDynamicRightsInterface::RejectUnavailableSwitchBackend, "IsRightsLostUser"},
        {21, &IDynamicRightsInterface::RejectUnavailableSwitchBackend, "SetRequiredAddOnContentsOnContentsAvailabilityTransition"},
        {22, &IDynamicRightsInterface::RejectUnavailableSwitchBackend, "GetLimitedApplicationLicense"},
        {23, &IDynamicRightsInterface::RejectUnavailableSwitchBackend, "GetLimitedApplicationLicenseUpgradableEvent"},
        {24, &IDynamicRightsInterface::RejectUnavailableSwitchBackend, "NotifyLimitedApplicationLicenseUpgradableEventForDebug"},
        {25, &IDynamicRightsInterface::RejectUnavailableSwitchBackend, "RequestProceedDynamicRightsState"},
        {26, D<&IDynamicRightsInterface::HasAccountRestrictedRightsInRunningApplications>, "HasAccountRestrictedRightsInRunningApplications"},
        {27, &IDynamicRightsInterface::RejectUnavailableSwitchBackend, "Unknown27"}, //20.0.0+
        {28, &IDynamicRightsInterface::RejectUnavailableSwitchBackend, "Unknown28"}, //20.0.0+
        {29, &IDynamicRightsInterface::RejectUnavailableSwitchBackend, "Unknown29"}, //21.0.0+
        {30, &IDynamicRightsInterface::RejectUnavailableSwitchBackend, "Unknown30"}, //21.0.0+
    };
    // clang-format on

    RegisterHandlers(functions);
}

IDynamicRightsInterface::~IDynamicRightsInterface() = default;

void IDynamicRightsInterface::RejectUnavailableSwitchBackend(HLERequestContext& ctx) {
    IPC::ResponseBuilder rb{ctx, 2};
    rb.Push(ResultUnknown);
}

Result IDynamicRightsInterface::NotifyApplicationRightsCheckStart() {
    LOG_WARNING(Service_NS, "(STUBBED) called");
    R_SUCCEED();
}

Result IDynamicRightsInterface::GetRunningApplicationStatus(Out<u32> out_status,
                                                            u64 rights_handle) {
    *out_status = 0;
    LOG_WARNING(Service_NS,
                "GetRunningApplicationStatus unavailable without a dynamic-rights environment, "
                "rights_handle={:#x}",
                rights_handle);
    R_THROW(ResultUnknown);
}

Result IDynamicRightsInterface::VerifyActivatedRightsOwners(u64 rights_handle) {
    LOG_WARNING(Service_NS,
                "VerifyActivatedRightsOwners unavailable without a dynamic-rights environment, "
                "rights_handle={:#x}",
                rights_handle);
    R_THROW(ResultUnknown);
}

Result IDynamicRightsInterface::HasAccountRestrictedRightsInRunningApplications(
    Out<bool> out_is_restricted) {
    LOG_WARNING(Service_NS, "(STUBBED) called");
    *out_is_restricted = 0;
    R_SUCCEED();
}

} // namespace Service::NS
