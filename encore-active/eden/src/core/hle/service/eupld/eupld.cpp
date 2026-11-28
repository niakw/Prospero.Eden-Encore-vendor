// SPDX-FileCopyrightText: Copyright 2018 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <memory>

#include "core/hle/service/eupld/eupld.h"
#include "core/hle/service/server_manager.h"
#include "core/hle/service/service.h"
#include "core/hle/service/ipc_helpers.h"

namespace Service::EUPLD {

class ErrorUploadContext final : public ServiceFramework<ErrorUploadContext> {
public:
    explicit ErrorUploadContext(Core::System& system_) : ServiceFramework{system_, "eupld:c"} {
        // clang-format off
        static const FunctionInfo functions[] = {
            {0, &ErrorUploadContext::RejectUnavailableNintendoOnline, "SetUrl"},
            {1, &ErrorUploadContext::RejectUnavailableNintendoOnline, "ImportCrt"},
            {2, &ErrorUploadContext::RejectUnavailableNintendoOnline, "ImportPki"},
            {3, &ErrorUploadContext::RejectUnavailableNintendoOnline, "SetAutoUpload"},
            {4, &ErrorUploadContext::RejectUnavailableNintendoOnline, "GetAutoUpload"},
        };
        // clang-format on

        RegisterHandlers(functions);
    }

    // Encore NS1: unavailable Nintendo online backend; explicit error, never fake success.
    void RejectUnavailableNintendoOnline(HLERequestContext& ctx) {
        IPC::ResponseBuilder rb{ctx, 2};
        rb.Push(ResultUnknown);
    }

};

class ErrorUploadRequest final : public ServiceFramework<ErrorUploadRequest> {
public:
    explicit ErrorUploadRequest(Core::System& system_) : ServiceFramework{system_, "eupld:r"} {
        // clang-format off
        static const FunctionInfo functions[] = {
            {0, &ErrorUploadRequest::RejectUnavailableNintendoOnline, "Initialize"},
            {1, &ErrorUploadRequest::RejectUnavailableNintendoOnline, "UploadAll"},
            {2, &ErrorUploadRequest::RejectUnavailableNintendoOnline, "UploadSelected"},
            {3, &ErrorUploadRequest::RejectUnavailableNintendoOnline, "GetUploadStatus"},
            {4, &ErrorUploadRequest::RejectUnavailableNintendoOnline, "CancelUpload"},
            {5, &ErrorUploadRequest::RejectUnavailableNintendoOnline, "GetResult"},
        };
        // clang-format on

        RegisterHandlers(functions);
    }

    // Encore NS1: unavailable Nintendo online backend; explicit error, never fake success.
    void RejectUnavailableNintendoOnline(HLERequestContext& ctx) {
        IPC::ResponseBuilder rb{ctx, 2};
        rb.Push(ResultUnknown);
    }

};

void LoopProcess(Core::System& system) {
    auto server_manager = std::make_unique<ServerManager>(system);

    server_manager->RegisterNamedService("eupld:c", std::make_shared<ErrorUploadContext>(system));
    server_manager->RegisterNamedService("eupld:r", std::make_shared<ErrorUploadRequest>(system));
    ServerManager::RunServer(std::move(server_manager));
}

} // namespace Service::EUPLD
