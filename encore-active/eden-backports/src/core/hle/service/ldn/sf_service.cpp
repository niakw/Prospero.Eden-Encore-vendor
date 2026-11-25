// SPDX-FileCopyrightText: Copyright 2024 yuzu Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#include "core/hle/service/ipc_helpers.h"
#include "core/hle/service/ldn/sf_service.h"

namespace Service::LDN {

ISfService::ISfService(Core::System& system_) : ServiceFramework{system_, "ISfService"} {
    // clang-format off
    static const FunctionInfo functions[] = {
            {0, &ISfService::RejectUnavailableNetwork, "Initialize"},
            {256, &ISfService::RejectUnavailableNetwork, "AttachNetworkInterfaceStateChangeEvent"},
            {264, &ISfService::RejectUnavailableNetwork, "GetNetworkInterfaceLastError"},
            {272, &ISfService::RejectUnavailableNetwork, "GetRole"},
            {280, &ISfService::RejectUnavailableNetwork, "GetAdvertiseData"},
            {288, &ISfService::RejectUnavailableNetwork, "GetGroupInfo"},
            {296, &ISfService::RejectUnavailableNetwork, "GetGroupInfo2"},
            {304, &ISfService::RejectUnavailableNetwork, "GetGroupOwner"},
            {312, &ISfService::RejectUnavailableNetwork, "GetIpConfig"},
            {320, &ISfService::RejectUnavailableNetwork, "GetLinkLevel"},
            {512, &ISfService::RejectUnavailableNetwork, "Scan"},
            {768, &ISfService::RejectUnavailableNetwork, "CreateGroup"},
            {776, &ISfService::RejectUnavailableNetwork, "DestroyGroup"},
            {784, &ISfService::RejectUnavailableNetwork, "SetAdvertiseData"},
            {1536, &ISfService::RejectUnavailableNetwork, "SendToOtherGroup"},
            {1544, &ISfService::RejectUnavailableNetwork, "RecvFromOtherGroup"},
            {1552, &ISfService::RejectUnavailableNetwork, "AddAcceptableGroupId"},
            {1560, &ISfService::RejectUnavailableNetwork, "ClearAcceptableGroupId"},
    };
    // clang-format on

    RegisterHandlers(functions);
}

void ISfService::RejectUnavailableNetwork(HLERequestContext& ctx) {
    IPC::ResponseBuilder rb{ctx, 2};
    rb.Push(ResultUnknown);
}

ISfService::~ISfService() = default;

} // namespace Service::LDN
