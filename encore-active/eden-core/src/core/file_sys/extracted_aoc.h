// SPDX-FileCopyrightText: Copyright 2026 Oliv3rmoon
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <algorithm>
#include <memory>
#include <vector>

#include "common/settings.h"
#include "core/file_sys/common_funcs.h"
#include "core/file_sys/romfs.h"
#include "core/file_sys/vfs/vfs_real.h"
#include "extracted_aoc_paths.h"

namespace FileSys::ExtractedAoc {

inline std::vector<Entry> Installed() {
    // Set at guest launch to the selected title's collocated roots plus
    // central add-on stores. Never infer content from a sibling base game.
    return Scan(Settings::values.external_content_dirs);
}

inline std::vector<u64> InstalledIds() {
    std::vector<u64> ids;
    for (const auto& entry : Installed()) ids.push_back(entry.title_id);
    return ids;
}

inline VirtualFile OpenRomFS(u64 title_id) {
    // Match aoc:u: a directly requested AOC data storage must also honor
    // the game's disabled-DLC setting instead of bypassing the service list.
    const auto& disabled = Settings::values.disabled_addons[GetBaseTitleID(title_id)];
    if (std::find(disabled.begin(), disabled.end(), "DLC") != disabled.end())
        return nullptr;
    // The directory-backed VFS files returned by CreateRomFS retain their
    // filesystem backend. This shared owner must outlive those guest handles.
    static const auto filesystem = std::make_shared<RealVfsFilesystem>();
    for (const auto& entry : Installed()) {
        if (entry.title_id != title_id || !SafeDirectory(entry.romfs_path))
            continue;
        auto directory = filesystem->OpenDirectory(entry.romfs_path.string(), OpenMode::Read);
        if (directory == nullptr) return nullptr;
        return CreateRomFS(std::move(directory));
    }
    return nullptr;
}

} // namespace FileSys::ExtractedAoc
