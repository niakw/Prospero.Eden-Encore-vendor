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

    filesystems.emplace_back(std::move(filesystem));
    R_SUCCEED();
}

Result IMultiCommitManager::Commit() {
    LOG_DEBUG(Service_FS, "called, filesystem_count={}", filesystems.size());

    // Host-directory file systems apply mutations synchronously. We still keep
    // the participant list and invoke each interface's Commit contract, but
    // Encore cannot reproduce Horizon's provisional-commit context/rollback.
    for (const auto& filesystem : filesystems) {
        R_UNLESS(filesystem != nullptr, FileSys::ResultInvalidArgument);
        R_TRY(filesystem->Commit());
    }
    filesystems.clear();
    R_SUCCEED();
}

} // namespace Service::FileSystem
