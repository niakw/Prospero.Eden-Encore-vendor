// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

// SPDX-FileCopyrightText: Copyright 2024 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "core/core.h"
#include "core/file_sys/registered_cache.h"
#include "core/hle/service/cmif_serialization.h"
#include "core/hle/service/ipc_helpers.h"
#include "core/hle/service/ns/read_only_application_record_interface.h"
#include "core/hle/service/ns/ns_types.h"
#include "core/hle/service/ns/application_manager_interface.h"

namespace Service::NS {

IReadOnlyApplicationRecordInterface::IReadOnlyApplicationRecordInterface(Core::System& system_)
    : ServiceFramework{system_, "IReadOnlyApplicationRecordInterface"} {
    static const FunctionInfo functions[] = {
        {0, D<&IReadOnlyApplicationRecordInterface::HasApplicationRecord>, "HasApplicationRecord"},
        {1, &IReadOnlyApplicationRecordInterface::RejectUnsupportedFailureNotification, "NotifyApplicationFailure"},
        {2, D<&IReadOnlyApplicationRecordInterface::IsDataCorruptedResult>, "IsDataCorruptedResult"},
        {3, D<&IReadOnlyApplicationRecordInterface::ListApplicationRecord>, "ListApplicationRecord"},
    };
    // clang-format on

    RegisterHandlers(functions);
}

void IReadOnlyApplicationRecordInterface::RejectUnsupportedFailureNotification(HLERequestContext& ctx) {
    // No ns failure-notification sink is modeled; fail explicitly rather than acknowledge it.
    IPC::ResponseBuilder rb{ctx, 2};
    rb.Push(IPC::ResultNotSupported);
}

IReadOnlyApplicationRecordInterface::~IReadOnlyApplicationRecordInterface() = default;

Result IReadOnlyApplicationRecordInterface::HasApplicationRecord(
    Out<bool> out_has_application_record, u64 program_id) {
    LOG_DEBUG(Service_NS, "called, program_id={:016X}", program_id);
    const auto records = system.GetContentProviderUnion().ListEntriesFilterOrigin(
        std::nullopt, FileSys::TitleType::Application, FileSys::ContentRecordType::Program,
        program_id);
    *out_has_application_record = !records.empty();
    R_SUCCEED();
}

Result IReadOnlyApplicationRecordInterface::IsDataCorruptedResult(
    Out<bool> out_is_data_corrupted_result, Result result) {
    constexpr u32 DataCorruptedDescriptionMin = 4000;
    constexpr u32 DataCorruptedDescriptionMax = 4999;

    const auto description = result.GetDescription();
    *out_is_data_corrupted_result =
        result.GetModule() == ErrorModule::FS && description >= DataCorruptedDescriptionMin &&
        description <= DataCorruptedDescriptionMax;

    LOG_DEBUG(Service_NS, "called, result={:#x}, is_data_corrupted={}",
              result.GetInnerValue(), *out_is_data_corrupted_result);
    R_SUCCEED();
}

Result IReadOnlyApplicationRecordInterface::ListApplicationRecord(
    OutArray<ApplicationRecord, BufferAttr_HipcMapAlias> out_records, Out<s32> out_count,
    s32 entry_offset) {
    LOG_DEBUG(Service_NS, "delegating to IApplicationManagerInterface::ListApplicationRecord, offset={} limit={}",
              entry_offset, out_records.size());

    R_RETURN(IApplicationManagerInterface(system).ListApplicationRecord(out_records, out_count,
                                                                        entry_offset));
}

} // namespace Service::NS
