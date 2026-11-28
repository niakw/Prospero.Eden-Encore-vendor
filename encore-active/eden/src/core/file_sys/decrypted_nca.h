// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <string>
#include <vector>
#include "core/file_sys/vfs/vfs_types.h"
namespace FileSys {
/// Virtual PFS0 package containing the already-verified NX snapshot entries.
VirtualFile BuildPartitionFile(std::string name, const std::vector<VirtualFile>& files);
/// Virtual plaintext DNCA for NX extracted add-on RomFS content.
VirtualFile BuildAddOnContentNca(u64 title_id, VirtualFile romfs);
} // namespace FileSys
