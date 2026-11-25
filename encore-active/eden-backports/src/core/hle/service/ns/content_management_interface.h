// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

// SPDX-FileCopyrightText: Copyright 2024 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "core/hle/service/cmif_types.h"
#include "core/hle/service/ns/ns_types.h"
#include "core/hle/service/service.h"

namespace Service::NS {

struct ApplicationContentMetaStatus {
    u8 meta_type;
    FileSys::StorageId storage_id;
    u8 rights_check;
    u8 reserved;
    u32 version;
    u64 application_id;
};
static_assert(sizeof(ApplicationContentMetaStatus) == 0x10);

class IContentManagementInterface final : public ServiceFramework<IContentManagementInterface> {
public:
    explicit IContentManagementInterface(Core::System& system_);
    ~IContentManagementInterface() override;

public:
    Result CalculateApplicationOccupiedSize(Out<ApplicationOccupiedSize> out_size,
                                            u64 application_id);
    Result CheckSdCardMountStatus();
    Result GetTotalSpaceSize(Out<s64> out_total_space_size, FileSys::StorageId storage_id);
    Result GetFreeSpaceSize(Out<s64> out_free_space_size, FileSys::StorageId storage_id);
    Result GetUnknown71(Out<u64> out_value_a, Out<u64> out_value_b, u8 flag);
    Result CountApplicationContentMeta(Out<s32> out_count, u64 application_id);
    Result ListApplicationContentMetaStatus(
        OutArray<ApplicationContentMetaStatus, BufferAttr_HipcMapAlias> out_statuses,
        Out<s32> out_count, s32 index, u64 application_id);
    Result IsAnyApplicationRunning(Out<bool> out_running);
};

} // namespace Service::NS
