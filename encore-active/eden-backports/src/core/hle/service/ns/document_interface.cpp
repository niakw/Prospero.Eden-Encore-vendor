// SPDX-FileCopyrightText: Copyright 2024 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <array>
#include <cstring>
#include <string>
#include <string_view>

#include "core/core.h"
#include "core/file_sys/common_funcs.h"
#include "core/file_sys/registered_cache.h"
#include "core/hle/service/cmif_serialization.h"
#include "core/hle/service/filesystem/fsp/fsp_types.h"
#include "core/hle/service/ns/document_interface.h"

namespace Service::NS {

namespace {

struct ResolvedDocumentContentPath {
    std::string path;
    u64 program_id{};
};

Result ResolveDocumentContentPath(Core::System& system, ResolvedDocumentContentPath* out,
                                  u8 content_type, u64 application_id) {
    const u64 base_id = FileSys::GetBaseTitleID(application_id);
    const u64 update_id = FileSys::GetUpdateTitleID(base_id);
    auto& cp = system.GetContentProviderUnion();

    // NS receives NcmContentType; its values match ContentRecordType (0..6).
    R_UNLESS(content_type <= static_cast<u8>(FileSys::ContentRecordType::DeltaFragment),
             ResultUnknown);
    const auto record_type = static_cast<FileSys::ContentRecordType>(content_type);
    // Installed update takes precedence; fall back to the base title.
    const std::array candidates{update_id, base_id};
    for (const u64 title_id : candidates) {
        const auto slot = cp.GetSlotForEntry(title_id, record_type);
        if (!slot) {
            continue;
        }
        const auto* cache =
            dynamic_cast<const FileSys::RegisteredCache*>(cp.GetSlotProvider(*slot));
        if (!cache) {
            continue;
        }
        const auto relative_path = cache->GetRegisteredContentPath(title_id, record_type);
        if (!relative_path) {
            continue;
        }

        std::string_view prefix;
        switch (*slot) {
        case FileSys::ContentProviderUnionSlot::SysNAND:
            prefix = "@SystemContent://";
            break;
        case FileSys::ContentProviderUnionSlot::UserNAND:
            prefix = "@UserContent://";
            break;
        case FileSys::ContentProviderUnionSlot::SDMC:
            prefix = "@SdCardContent://";
            break;
        default:
            continue;
        }

        out->path = std::string{prefix} + *relative_path;
        out->program_id = title_id;
        R_SUCCEED();
    }
    R_RETURN(ResultUnknown);
}

Result CopyDocumentContentPath(
    OutBuffer<BufferAttr_HipcPointer> out_path, const ResolvedDocumentContentPath& resolved) {
    R_UNLESS(resolved.path.size() + 1 <= out_path.size(), ResultUnknown);
    std::memset(out_path.data(), 0, out_path.size());
    std::memcpy(out_path.data(), resolved.path.data(), resolved.path.size());
    R_SUCCEED();
}

} // namespace

IDocumentInterface::IDocumentInterface(Core::System& system_)
    : ServiceFramework{system_, "IDocumentInterface"} {
    // clang-format off
    static const FunctionInfo functions[] = {
        {21, D<&IDocumentInterface::GetApplicationContentPath>, "GetApplicationContentPath"},
        {23, D<&IDocumentInterface::ResolveApplicationContentPath>, "ResolveApplicationContentPath"},
        {92, D<&IDocumentInterface::GetRunningApplicationProgramId>, "GetRunningApplicationProgramId"},
        {2524, D<&IDocumentInterface::GetApplicationContentPath2>, "GetApplicationContentPath2"},
    };
    // clang-format on

    RegisterHandlers(functions);
}

IDocumentInterface::~IDocumentInterface() = default;

Result IDocumentInterface::GetApplicationContentPath(
    OutBuffer<BufferAttr_HipcPointer> out_path, u8 content_type, u64 application_id) {
    ResolvedDocumentContentPath resolved{};
    R_TRY(ResolveDocumentContentPath(system, &resolved, content_type, application_id));
    R_RETURN(CopyDocumentContentPath(out_path, resolved));
}

Result IDocumentInterface::GetApplicationContentPath2(
    Out<FileSystem::ContentAttributes> out_attributes, Out<u64> out_program_id,
    OutBuffer<BufferAttr_HipcPointer> out_path, u8 content_type, u64 application_id) {
    ResolvedDocumentContentPath resolved{};
    R_TRY(ResolveDocumentContentPath(system, &resolved, content_type, application_id));
    R_TRY(CopyDocumentContentPath(out_path, resolved));
    *out_attributes = FileSystem::ContentAttributes::None;
    *out_program_id = resolved.program_id;
    R_SUCCEED();
}

Result IDocumentInterface::ResolveApplicationContentPath(ContentPath content_path) {
    ResolvedDocumentContentPath resolved{};
    R_RETURN(ResolveDocumentContentPath(system, &resolved, content_path.file_system_proxy_type,
                                        content_path.program_id));
}

Result IDocumentInterface::GetRunningApplicationProgramId(Out<u64> out_program_id,
                                                          u64 caller_program_id) {
    LOG_DEBUG(Service_NS, "called, caller_program_id={:016X}", caller_program_id);
    *out_program_id = system.GetApplicationProcessProgramID();
    R_SUCCEED();
}

} // namespace Service::NS
