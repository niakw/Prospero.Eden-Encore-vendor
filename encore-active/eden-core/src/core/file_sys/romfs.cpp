// SPDX-FileCopyrightText: Copyright 2025 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

// SPDX-FileCopyrightText: Copyright 2018 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <memory>
#include <optional>
#include <cstring>

#include "common/logging.h"
#include "common/common_types.h"
#include "common/string_util.h"
#include "common/swap.h"
#include "core/file_sys/fsmitm_romfsbuild.h"
#include "core/file_sys/romfs.h"
#include "core/file_sys/vfs/vfs.h"
#include "core/file_sys/vfs/vfs_cached.h"
#include "core/file_sys/vfs/vfs_concat.h"
#include "core/file_sys/vfs/vfs_offset.h"
#include "core/file_sys/vfs/vfs_vector.h"

namespace FileSys {
namespace {
constexpr u32 ROMFS_ENTRY_EMPTY = 0xFFFFFFFF;
// Horizon paths are at most 0x300 bytes; even a one-character component uses two.
constexpr size_t MaxDirectoryDepth = 0x300 / 2;

struct TableLocation {
    u64_le offset;
    u64_le size;
};
static_assert(sizeof(TableLocation) == 0x10, "TableLocation has incorrect size.");

struct RomFSHeader {
    u64_le header_size;
    TableLocation directory_hash;
    TableLocation directory_meta;
    TableLocation file_hash;
    TableLocation file_meta;
    u64_le data_offset;
};
static_assert(sizeof(RomFSHeader) == 0x50, "RomFSHeader has incorrect size.");

struct DirectoryEntry {
    u32_le parent;
    u32_le sibling;
    u32_le child_dir;
    u32_le child_file;
    u32_le hash;
    u32_le name_length;
};
static_assert(sizeof(DirectoryEntry) == 0x18, "DirectoryEntry has incorrect size.");

struct FileEntry {
    u32_le parent;
    u32_le sibling;
    u64_le offset;
    u64_le size;
    u32_le hash;
    u32_le name_length;
};
static_assert(sizeof(FileEntry) == 0x20, "FileEntry has incorrect size.");

struct RomFSTraversalContext {
    RomFSHeader header;
    VirtualFile file;
    std::vector<u8> directory_meta;
    std::vector<u8> file_meta;
    std::vector<bool> directory_seen;
    std::vector<bool> file_seen;
};

// Lichii #947: bound the tree walk, keep owning directories alive, and reject
// incomplete records. Offsets remain relative to their original metadata tables.
template <typename EntryType>
std::optional<std::pair<EntryType, std::string>> GetEntry(const std::vector<u8>& meta,
                                                         std::vector<bool>& seen, u32 offset) {
    if (offset >= meta.size() || sizeof(EntryType) > meta.size() - offset || seen[offset]) {
        return std::nullopt;
    }
    seen[offset] = true;
    EntryType entry{};
    std::memcpy(&entry, meta.data() + offset, sizeof(EntryType));
    const size_t name_offset = offset + sizeof(EntryType);
    const size_t name_length = entry.name_length;
    if (name_length > meta.size() - name_offset) {
        return std::nullopt;
    }
    return std::pair{entry, std::string(reinterpret_cast<const char*>(meta.data() + name_offset),
                                        name_length)};
}

bool ProcessFiles(RomFSTraversalContext& ctx, u32 offset, VectorVfsDirectory& parent) {
    const u64 file_size = ctx.file->GetSize();
    const u64 data_offset = ctx.header.data_offset;
    if (data_offset > file_size) {
        return false;
    }
    while (offset != ROMFS_ENTRY_EMPTY) {
        auto entry = GetEntry<FileEntry>(ctx.file_meta, ctx.file_seen, offset);
        if (!entry) {
            return false;
        }
        const u64 relative = entry->first.offset;
        const u64 length = entry->first.size;
        if (relative > file_size - data_offset ||
            length > file_size - data_offset - relative) {
            return false;
        }
        parent.AddFile(std::make_shared<OffsetVfsFile>(ctx.file, length, data_offset + relative,
                                                       std::move(entry->second)));
        offset = entry->first.sibling;
    }
    return true;
}

bool ProcessDirectories(RomFSTraversalContext& ctx, VectorVfsDirectory& root) {
    struct Pending {
        u32 offset;
        VectorVfsDirectory* parent;
        size_t depth;
    };
    std::vector<Pending> pending{{0, &root, 0}};
    while (!pending.empty()) {
        auto [offset, parent, depth] = pending.back();
        pending.pop_back();
        if (depth > MaxDirectoryDepth) {
            return false;
        }
        while (offset != ROMFS_ENTRY_EMPTY) {
            auto entry = GetEntry<DirectoryEntry>(ctx.directory_meta, ctx.directory_seen, offset);
            if (!entry) {
                return false;
            }
            auto current = std::make_shared<VectorVfsDirectory>(
                std::vector<VirtualFile>{}, std::vector<VirtualDir>{}, std::move(entry->second));
            parent->AddDirectory(current);
            if (!ProcessFiles(ctx, entry->first.child_file, *current)) {
                return false;
            }
            if (entry->first.child_dir != ROMFS_ENTRY_EMPTY) {
                pending.push_back({entry->first.child_dir, current.get(), depth + 1});
            }
            offset = entry->first.sibling;
        }
    }
    return true;
}
} // Anonymous namespace

VirtualDir ExtractRomFS(VirtualFile file) {
    auto root_container = std::make_shared<VectorVfsDirectory>();
    if (!file) {
        return root_container;
    }

    RomFSTraversalContext ctx{};

    if (file->ReadObject(&ctx.header) != sizeof(RomFSHeader)) {
        return nullptr;
    }

    if (ctx.header.header_size != sizeof(RomFSHeader)) {
        return nullptr;
    }

    const u64 file_size = file->GetSize();
    const auto fits = [file_size](const TableLocation& table) {
        const u64 offset = table.offset;
        const u64 length = table.size;
        return offset <= file_size && length <= file_size - offset;
    };
    if (!fits(ctx.header.directory_meta) || !fits(ctx.header.file_meta)) {
        LOG_ERROR(Loader, "RomFS metadata table is incomplete");
        return nullptr;
    }
    ctx.file = file;
    ctx.directory_meta =
        file->ReadBytes(ctx.header.directory_meta.size, ctx.header.directory_meta.offset);
    ctx.file_meta = file->ReadBytes(ctx.header.file_meta.size, ctx.header.file_meta.offset);

    if (ctx.directory_meta.size() != ctx.header.directory_meta.size ||
        ctx.file_meta.size() != ctx.header.file_meta.size) {
        LOG_ERROR(Loader, "RomFS metadata read was incomplete");
        return nullptr;
    }
    ctx.directory_seen.resize(ctx.directory_meta.size());
    ctx.file_seen.resize(ctx.file_meta.size());
    if (!ProcessDirectories(ctx, *root_container)) {
        LOG_ERROR(Loader, "RomFS directory or file traversal failed");
        return nullptr;
    }

    if (auto root = root_container->GetSubdirectory(""); root) {
        return root;
    }

    LOG_ERROR(Loader, "RomFS unnamed root is missing");
    return nullptr;
}

VirtualFile CreateRomFS(VirtualDir dir, VirtualDir ext) {
    if (dir == nullptr)
        return nullptr;

    RomFSBuildContext ctx{dir, ext};
    return ConcatenatedVfsFile::MakeConcatenatedFile(0, dir->GetName(), ctx.Build());
}

} // namespace FileSys
