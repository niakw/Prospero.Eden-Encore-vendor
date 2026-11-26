// SPDX-FileCopyrightText: Copyright 2018 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <algorithm>

#include "core/file_sys/errors.h"
#include "core/hle/service/cmif_serialization.h"
#include "core/hle/service/filesystem/fsp/fs_i_filesystem.h"
#include "core/hle/service/filesystem/fsp/fs_i_multi_commit_manager.h"

namespace Service::FileSystem {

IMultiCommitManager::IMultiCommitManager(Core::System& system_)
    : ServiceFramework{system_, "IMultiCommitManager"} {
    static const FunctionInfo functions[] = {
        {1, D<&IMultiCommitManager::Add>, "Add"},
        {2, D<&IMultiCommitManager::Commit>, "Commit"},
    };
    RegisterHandlers(functions);
}

IMultiCommitManager::~IMultiCommitManager() = default;

Result IMultiCommitManager::Add(std::shared_ptr<IFileSystem> filesystem) {
    LOG_DEBUG(Service_FS, "called, current_count={}", filesystems.size());

    R_UNLESS(filesystem != nullptr, FileSys::ResultInvalidArgument);
    R_UNLESS(filesystems.size() < MaxFileSystems, FileSys::ResultOutOfRange);
    R_UNLESS(std::find(filesystems.begin(), filesystems.end(), filesystem) == filesystems.end(),
             FileSys::ResultInvalidArgument);

    // Encore's host-directory backend applies mutations synchronously and has no
    // provisional transaction/rollback context. Accept one participant, whose
    // Commit contract is therefore truthful; reject a second participant before
    // claiming unsupported cross-filesystem atomicity.
    if (!filesystems.empty()) {
        LOG_WARNING(Service_FS,
                    "multi-filesystem atomic commit is unavailable on the synchronous host backend");
        R_RETURN(ResultUnknown);
    }

    filesystems.emplace_back(std::move(filesystem));
    R_SUCCEED();
}

Result IMultiCommitManager::Commit() {
    LOG_DEBUG(Service_FS, "called, filesystem_count={}", filesystems.size());

    R_UNLESS(filesystems.size() <= 1, ResultUnknown);
    if (!filesystems.empty()) {
        R_UNLESS(filesystems.front() != nullptr, FileSys::ResultInvalidArgument);
        R_TRY(filesystems.front()->Commit());
    }
    filesystems.clear();
    R_SUCCEED();
}

} // namespace Service::FileSystem
