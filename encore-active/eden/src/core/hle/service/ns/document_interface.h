// SPDX-FileCopyrightText: Copyright 2024 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "core/hle/service/cmif_types.h"
#include "core/hle/service/filesystem/fsp/fsp_types.h"
#include "core/hle/service/ns/ns_types.h"
#include "core/hle/service/service.h"

namespace Service::NS {

class IDocumentInterface final : public ServiceFramework<IDocumentInterface> {
public:
    explicit IDocumentInterface(Core::System& system_);
    ~IDocumentInterface() override;

private:
    Result GetApplicationContentPath(OutBuffer<BufferAttr_HipcPointer> out_path,
                                     u8 content_type, u64 application_id);
    Result GetApplicationContentPath2(
        Out<FileSystem::ContentAttributes> out_attributes, Out<u64> out_program_id,
        OutBuffer<BufferAttr_HipcPointer> out_path, u8 content_type, u64 application_id);
    Result ResolveApplicationContentPath(ContentPath content_path);
    Result GetRunningApplicationProgramId(Out<u64> out_program_id, u64 caller_program_id);
};

} // namespace Service::NS
