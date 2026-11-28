// SPDX-FileCopyrightText: Copyright 2024 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "core/hle/service/ns/factory_reset_interface.h"

namespace Service::NS {

IFactoryResetInterface::IFactoryResetInterface(Core::System& system_)
    : ServiceFramework{system_, "IFactoryResetInterface"} {
    // clang-format off
        static const FunctionInfo functions[] = {
            {100, D<&IFactoryResetInterface::RejectFactoryReset>, "ResetToFactorySettings"},
            {101, D<&IFactoryResetInterface::RejectFactoryReset>, "ResetToFactorySettingsWithoutUserSaveData"},
            {102, D<&IFactoryResetInterface::RejectFactoryReset>, "ResetToFactorySettingsForRefurbishment"},
            {103, D<&IFactoryResetInterface::RejectFactoryReset>, "ResetToFactorySettingsWithPlatformRegion"},
            {104, D<&IFactoryResetInterface::RejectFactoryReset>, "ResetToFactorySettingsWithPlatformRegionAuthentication"},
            {105, D<&IFactoryResetInterface::RejectFactoryReset>, "RequestResetToFactorySettingsSecurely"},
            {106, D<&IFactoryResetInterface::RejectFactoryReset>, "RequestResetToFactorySettingsWithPlatformRegionAuthenticationSecurely"},
        };
    // clang-format on

    RegisterHandlers(functions);
}

IFactoryResetInterface::~IFactoryResetInterface() = default;

Result IFactoryResetInterface::RejectFactoryReset() {
    LOG_WARNING(Service_NS, "Rejected factory-reset request on release runtime");
    R_RETURN(ResultUnknown);
}

} // namespace Service::NS
