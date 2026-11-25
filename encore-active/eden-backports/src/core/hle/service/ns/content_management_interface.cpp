// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

// SPDX-FileCopyrightText: Copyright 2024 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <array>
#include <limits>
#include <set>

#include "common/common_funcs.h"
#include "core/core.h"
#include "core/file_sys/common_funcs.h"
#include "core/file_sys/nca_metadata.h"
#include "core/file_sys/registered_cache.h"
#include "core/hle/service/cmif_serialization.h"
#include "core/hle/service/am/applet.h"
#include "core/hle/service/am/applet_manager.h"
#include "core/hle/service/am/window_system.h"
#include "core/hle/service/filesystem/filesystem.h"
#include "core/hle/service/ns/content_management_interface.h"
#include "core/hle/service/ns/ns_types.h"

namespace Service::NS {

IContentManagementInterface::IContentManagementInterface(Core::System& system_)
    : ServiceFramework{system_, "IContentManagementInterface"} {
    // clang-format off
    static const FunctionInfo functions[] = {
        {11, D<&IContentManagementInterface::CalculateApplicationOccupiedSize>, "CalculateApplicationOccupiedSize"},
        {43, D<&IContentManagementInterface::CheckSdCardMountStatus>, "CheckSdCardMountStatus"},
        {47, D<&IContentManagementInterface::GetTotalSpaceSize>, "GetTotalSpaceSize"},
        {48, D<&IContentManagementInterface::GetFreeSpaceSize>, "GetFreeSpaceSize"},
        {71, D<&IContentManagementInterface::GetUnknown71>, "Unknown71"},
        {600, nullptr, "CountApplicationContentMeta"},
        {601, nullptr, "ListApplicationContentMetaStatus"},
        {605, nullptr, "ListApplicationContentMetaStatusWithRightsCheck"},
        {607, D<&IContentManagementInterface::IsAnyApplicationRunning>, "IsAnyApplicationRunning"},
        {608, nullptr, "Unknown608"}, //21.0.0+
    };
    // clang-format on

    RegisterHandlers(functions);
}

IContentManagementInterface::~IContentManagementInterface() = default;

namespace {

u64 SaturatingAddSize(u64 lhs, u64 rhs) {
    return rhs > std::numeric_limits<u64>::max() - lhs ? std::numeric_limits<u64>::max()
                                                        : lhs + rhs;
}

u64 InstalledTitleSize(const FileSys::RegisteredCache& cache, u64 title_id) {
    const auto* meta = cache.GetContentMeta(title_id);
    if (meta == nullptr) {
        return 0;
    }

    u64 total = 0;
    if (const auto meta_id = cache.GetMetaNcaId(title_id); meta_id.has_value()) {
        if (const auto size = cache.Size(*meta_id); size.has_value()) {
            total = SaturatingAddSize(total, *size);
        }
    }
    for (const auto& record : meta->GetContentRecords()) {
        if (const auto size = cache.Size(record.nca_id); size.has_value()) {
            total = SaturatingAddSize(total, *size);
        }
    }
    return total;
}

FileSys::StorageId StorageForInstalledSlot(FileSys::ContentProviderUnionSlot slot) {
    switch (slot) {
    case FileSys::ContentProviderUnionSlot::SysNAND:
        return FileSys::StorageId::NandSystem;
    case FileSys::ContentProviderUnionSlot::UserNAND:
        return FileSys::StorageId::NandUser;
    case FileSys::ContentProviderUnionSlot::SDMC:
        return FileSys::StorageId::SdCard;
    default:
        return FileSys::StorageId::None;
    }
}

} // namespace

Result IContentManagementInterface::CalculateApplicationOccupiedSize(
    Out<ApplicationOccupiedSize> out_size, u64 application_id) {
    LOG_DEBUG(Service_NS, "called, application_id={:016X}", application_id);
    *out_size = {};

    constexpr std::array installed_slots{
        FileSys::ContentProviderUnionSlot::SysNAND,
        FileSys::ContentProviderUnionSlot::UserNAND,
        FileSys::ContentProviderUnionSlot::SDMC,
    };

    auto& providers = system.GetContentProviderUnion();
    std::size_t entity_index = 0;
    for (const auto slot : installed_slots) {
        const auto* provider = providers.GetSlotProvider(slot);
        if (provider == nullptr) {
            continue;
        }

        // NAND and SD factories register these slots as RegisteredCache providers.
        // FrontendManual/External are intentionally not reported as installed storage.
        const auto& cache = *static_cast<const FileSys::RegisteredCache*>(provider);
        const u64 app_size = InstalledTitleSize(cache, application_id);
        const u64 patch_size =
            InstalledTitleSize(cache, FileSys::GetUpdateTitleID(application_id));

        std::set<u64> aoc_title_ids;
        for (const auto* meta : cache.ListContentMetas()) {
            if (meta != nullptr && meta->GetType() == FileSys::TitleType::AOC &&
                FileSys::GetBaseTitleID(meta->GetTitleID()) == application_id) {
                aoc_title_ids.insert(meta->GetTitleID());
            }
        }

        u64 aoc_size = 0;
        for (const auto aoc_id : aoc_title_ids) {
            aoc_size = SaturatingAddSize(aoc_size, InstalledTitleSize(cache, aoc_id));
        }

        if (app_size == 0 && patch_size == 0 && aoc_size == 0) {
            continue;
        }
        if (entity_index >= out_size->entities.size()) {
            break;
        }
        out_size->entities[entity_index++] = {
            .storage_id = StorageForInstalledSlot(slot),
            .app_size = app_size,
            .patch_size = patch_size,
            .aoc_size = aoc_size,
        };
    }

    R_SUCCEED();
}

Result IContentManagementInterface::CheckSdCardMountStatus() {
    LOG_WARNING(Service_NS, "(STUBBED) called");
    R_SUCCEED();
}

Result IContentManagementInterface::GetTotalSpaceSize(Out<s64> out_total_space_size,
                                                      FileSys::StorageId storage_id) {
    LOG_INFO(Service_NS, "(STUBBED) called, storage_id={}", storage_id);
    *out_total_space_size = system.GetFileSystemController().GetTotalSpaceSize(storage_id);
    R_SUCCEED();
}

Result IContentManagementInterface::GetFreeSpaceSize(Out<s64> out_free_space_size,
                                                     FileSys::StorageId storage_id) {
    LOG_INFO(Service_NS, "(STUBBED) called, storage_id={}", storage_id);
    *out_free_space_size = system.GetFileSystemController().GetFreeSpaceSize(storage_id);
    R_SUCCEED();
}

Result IContentManagementInterface::GetUnknown71(Out<u64> out_value_a, Out<u64> out_value_b,
                                                 u8 flag) {
    LOG_INFO(Service_NS, "(STUBBED) called, flag={:02X}", flag);
    *out_value_a = 0;
    *out_value_b = 0;
    R_SUCCEED();
}

Result IContentManagementInterface::IsAnyApplicationRunning(Out<bool> out_running) {
    *out_running = false;
    auto* const window_system = system.GetAppletManager().GetWindowSystem();
    if (window_system == nullptr) {
        R_SUCCEED();
    }
    const auto applet = window_system->GetMainApplet();
    if (applet == nullptr) {
        R_SUCCEED();
    }
    std::scoped_lock lk{applet->lock};
    *out_running = applet->is_process_running && !applet->is_completed;
    R_SUCCEED();
}

} // namespace Service::NS
