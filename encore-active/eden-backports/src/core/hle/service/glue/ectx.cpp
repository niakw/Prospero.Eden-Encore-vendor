// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

// SPDX-FileCopyrightText: Copyright 2021 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "core/hle/service/glue/ectx.h"
#include "core/hle/service/ipc_helpers.h"

namespace Service::Glue {

// This is nn::err::context::IContextRegistrar
class IContextRegistrar : public ServiceFramework<IContextRegistrar> {
public:
    IContextRegistrar(Core::System& system_) : ServiceFramework{system_, "IContextRegistrar"} {
        // clang-format off
        static const FunctionInfo functions[] = {
            {0, &IContextRegistrar::Complete, "Complete"},
        };
        // clang-format on

        RegisterHandlers(functions);
    }

    ~IContextRegistrar() override = default;

private:
    void Complete(HLERequestContext& ctx) {
        struct InputParameters {
            u32 unk;
        };
        struct OutputParameters {
            u32 unk;
        };

        IPC::RequestParser rp{ctx};
        [[maybe_unused]] auto input = rp.PopRaw<InputParameters>();
        [[maybe_unused]] auto value = ctx.ReadBuffer();

        IPC::ResponseBuilder rb{ctx, 3};
        rb.Push(ResultSuccess);
        rb.Push(0);
    }
};

ECTX_W::ECTX_W(Core::System& system_) : ServiceFramework{system_, "ectx:w"} {
    // clang-format off
    static const FunctionInfo functions[] = {
        {0, &ECTX_W::CreateContextRegistrar, "CreateContextRegistrar"},
        {1, &ECTX_W::RejectUnsupportedContextCommand, "CommitContext"},
        {2, &ECTX_W::RejectUnsupportedContextCommand, "RemoveContext"},
    };
    // clang-format on
    RegisterHandlers(functions);
}
void ECTX_W::CreateContextRegistrar(HLERequestContext& ctx) {
    IPC::ResponseBuilder rb{ctx, 2, 0, 1};
    rb.Push(ResultSuccess);
    rb.PushIpcInterface<IContextRegistrar>(ctx, system);
}

void ECTX_W::RejectUnsupportedContextCommand(HLERequestContext& ctx) {
    IPC::ResponseBuilder rb{ctx, 2};
    rb.Push(IPC::ResultNotSupported);
}

ECTX_W::~ECTX_W() = default;

ECTX_R::ECTX_R(Core::System& system_) : ServiceFramework{system_, "ectx:r"} {
    // clang-format off
    static const FunctionInfo functions[] = {
        {0, &ECTX_R::RejectUnsupportedContextCommand, "GetContextInfo"},
        {1, &ECTX_R::RejectUnsupportedContextCommand, "PullContext"},
        {2, &ECTX_R::RejectUnsupportedContextCommand, "ListContextDescriptorWithResultForDebug"},
    };
    // clang-format on
    RegisterHandlers(functions);
}
void ECTX_R::RejectUnsupportedContextCommand(HLERequestContext& ctx) {
    IPC::ResponseBuilder rb{ctx, 2};
    rb.Push(IPC::ResultNotSupported);
}

ECTX_R::~ECTX_R() = default;

ECTX_AW::ECTX_AW(Core::System& system_) : ServiceFramework{system_, "ectx:aw"} {
    // clang-format off
    static const FunctionInfo functions[] = {
        {0, &ECTX_AW::CreateContextRegistrar, "CreateContextRegistrar"},
        {1, &ECTX_AW::RejectUnsupportedContextCommand, "CommitContext"},
    };
    // clang-format on

    RegisterHandlers(functions);
}

void ECTX_AW::RejectUnsupportedContextCommand(HLERequestContext& ctx) {
    IPC::ResponseBuilder rb{ctx, 2};
    rb.Push(IPC::ResultNotSupported);
}

ECTX_AW::~ECTX_AW() = default;

void ECTX_AW::CreateContextRegistrar(HLERequestContext& ctx) {
    IPC::ResponseBuilder rb{ctx, 2, 0, 1};
    rb.Push(ResultSuccess);
    rb.PushIpcInterface<IContextRegistrar>(ctx, system);
}

} // namespace Service::Glue
