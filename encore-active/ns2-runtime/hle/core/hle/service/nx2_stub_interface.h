// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <deque>
#include <mutex>
#include <string>
#include <string_view>

#include <fmt/format.h>
#include "common/logging.h"
#include "core/hle/service/ipc_helpers.h"
#include "core/hle/service/service.h"

namespace Service {

// Lichii's NS2 placeholder interfaces must have an actual IPC object, not
// a success reply lacking the expected object handle/domain reference.
inline constexpr std::string_view Nx2StubPrefix = "nx2:";

class INx2StubInterface final : public ServiceFramework<INx2StubInterface> {
public:
    explicit INx2StubInterface(Core::System& system, const char* name)
        : ServiceFramework{system, name} {}

    static const char* InternName(std::string name) {
        static std::mutex mutex;
        static std::deque<std::string> names;
        std::scoped_lock lock{mutex};
        for (const auto& entry : names) {
            if (entry == name)
                return entry.c_str();
        }
        return names.emplace_back(std::move(name)).c_str();
    }
};

inline void ReturnNx2StubInterface(Core::System& system,
                                    HLERequestContext& ctx, std::string_view owner) {
    const char* const name = INx2StubInterface::InternName(
        fmt::format("{}{}::{}", Nx2StubPrefix, owner, ctx.GetCommand()));
    LOG_WARNING(Service, "(STUBBED) NS2 {}::{} -> interface {}", owner,
                ctx.GetCommand(), name);
    IPC::ResponseBuilder rb{ctx, 2, 0, 1};
    rb.Push(ResultSuccess);
    rb.PushIpcInterface<INx2StubInterface>(ctx, system, name);
}

} // namespace Service
