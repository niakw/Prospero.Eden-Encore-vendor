// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

// SPDX-FileCopyrightText: Copyright 2023 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "common/string_util.h"
#include "common/fs/file.h"
#include "common/fs/fs.h"
#include "core/file_sys/errors.h"
#include "core/file_sys/fssrv/fssrv_sf_path.h"
#include "core/hle/service/cmif_serialization.h"
#include "core/hle/service/filesystem/fsp/fs_i_directory.h"
#include "core/hle/service/filesystem/fsp/fs_i_file.h"
#include "core/hle/service/filesystem/fsp/fs_i_filesystem.h"

namespace Service::FileSystem {

IFileSystem::IFileSystem(Core::System& system_, FileSys::VirtualDir dir_, SizeGetter size_getter_,
                         bool read_only_)
    : ServiceFramework{system_, "IFileSystem"}, backend{std::make_unique<FileSys::Fsa::IFileSystem>(
                                                    dir_)},
      size_getter{std::move(size_getter_)}, commit_state{std::make_shared<FileCommitState>()},
      read_only{read_only_} {
    static const FunctionInfo functions[] = {
        {0, D<&IFileSystem::CreateFile>, "CreateFile"},
        {1, D<&IFileSystem::DeleteFile>, "DeleteFile"},
        {2, D<&IFileSystem::CreateDirectory>, "CreateDirectory"},
        {3, D<&IFileSystem::DeleteDirectory>, "DeleteDirectory"},
        {4, D<&IFileSystem::DeleteDirectoryRecursively>, "DeleteDirectoryRecursively"},
        {5, D<&IFileSystem::RenameFile>, "RenameFile"},
        {6, D<&IFileSystem::RenameDirectory>, "RenameDirectory"},
        {7, D<&IFileSystem::GetEntryType>, "GetEntryType"},
        {8, D<&IFileSystem::OpenFile>, "OpenFile"},
        {9, D<&IFileSystem::OpenDirectory>, "OpenDirectory"},
        {10, D<&IFileSystem::Commit>, "Commit"},
        {11, D<&IFileSystem::GetFreeSpaceSize>, "GetFreeSpaceSize"},
        {12, D<&IFileSystem::GetTotalSpaceSize>, "GetTotalSpaceSize"},
        {13, D<&IFileSystem::CleanDirectoryRecursively>, "CleanDirectoryRecursively"},
        {14, D<&IFileSystem::GetFileTimeStampRaw>, "GetFileTimeStampRaw"},
        {15, D<&IFileSystem::QueryEntry>, "QueryEntry"},
        {16, D<&IFileSystem::GetFileSystemAttribute>, "GetFileSystemAttribute"},
    };
    RegisterHandlers(functions);
}

Result IFileSystem::CreateFile(const InLargeData<FileSys::Sf::Path, BufferAttr_HipcPointer> path,
                               s32 option, s64 size) {
    LOG_DEBUG(Service_FS, "called. file={}, option={:#x}, size={:#08x}", path->str, option, size);
    R_UNLESS(!read_only, FileSys::ResultWriteNotPermitted);

    R_RETURN(backend->CreateFile(FileSys::Path(path->str), size));
}

Result IFileSystem::DeleteFile(const InLargeData<FileSys::Sf::Path, BufferAttr_HipcPointer> path) {
    LOG_DEBUG(Service_FS, "called. file={}", path->str);
    R_UNLESS(!read_only, FileSys::ResultWriteNotPermitted);

    R_RETURN(backend->DeleteFile(FileSys::Path(path->str)));
}

Result IFileSystem::CreateDirectory(
    const InLargeData<FileSys::Sf::Path, BufferAttr_HipcPointer> path) {
    LOG_DEBUG(Service_FS, "called. directory={}", path->str);
    R_UNLESS(!read_only, FileSys::ResultWriteNotPermitted);

    R_RETURN(backend->CreateDirectory(FileSys::Path(path->str)));
}

Result IFileSystem::DeleteDirectory(
    const InLargeData<FileSys::Sf::Path, BufferAttr_HipcPointer> path) {
    LOG_DEBUG(Service_FS, "called. directory={}", path->str);
    R_UNLESS(!read_only, FileSys::ResultWriteNotPermitted);

    R_RETURN(backend->DeleteDirectory(FileSys::Path(path->str)));
}

Result IFileSystem::DeleteDirectoryRecursively(
    const InLargeData<FileSys::Sf::Path, BufferAttr_HipcPointer> path) {
    LOG_DEBUG(Service_FS, "called. directory={}", path->str);
    R_UNLESS(!read_only, FileSys::ResultWriteNotPermitted);

    R_RETURN(backend->DeleteDirectoryRecursively(FileSys::Path(path->str)));
}

Result IFileSystem::CleanDirectoryRecursively(
    const InLargeData<FileSys::Sf::Path, BufferAttr_HipcPointer> path) {
    LOG_DEBUG(Service_FS, "called. Directory: {}", path->str);
    R_UNLESS(!read_only, FileSys::ResultWriteNotPermitted);

    R_RETURN(backend->CleanDirectoryRecursively(FileSys::Path(path->str)));
}

Result IFileSystem::RenameFile(
    const InLargeData<FileSys::Sf::Path, BufferAttr_HipcPointer> old_path,
    const InLargeData<FileSys::Sf::Path, BufferAttr_HipcPointer> new_path) {
    LOG_DEBUG(Service_FS, "called. file '{}' to file '{}'", old_path->str, new_path->str);
    R_UNLESS(!read_only, FileSys::ResultWriteNotPermitted);

    R_RETURN(backend->RenameFile(FileSys::Path(old_path->str), FileSys::Path(new_path->str)));
}

Result IFileSystem::RenameDirectory(
    const InLargeData<FileSys::Sf::Path, BufferAttr_HipcPointer> old_path,
    const InLargeData<FileSys::Sf::Path, BufferAttr_HipcPointer> new_path) {
    LOG_DEBUG(Service_FS, "called. directory '{}' to directory '{}'", old_path->str, new_path->str);
    R_UNLESS(!read_only, FileSys::ResultWriteNotPermitted);

    R_RETURN(backend->RenameDirectory(FileSys::Path(old_path->str), FileSys::Path(new_path->str)));
}

Result IFileSystem::OpenFile(OutInterface<IFile> out_interface,
                             const InLargeData<FileSys::Sf::Path, BufferAttr_HipcPointer> path,
                             u32 mode) {
    LOG_DEBUG(Service_FS, "called. file={}, mode={}", path->str, mode);

    const auto open_mode = static_cast<FileSys::OpenMode>(mode);
    R_UNLESS(!read_only || static_cast<u32>(open_mode & FileSys::OpenMode::Write) == 0,
             FileSys::ResultWriteNotPermitted);

    FileSys::VirtualFile vfs_file{};
    R_TRY(backend->OpenFile(&vfs_file, FileSys::Path(path->str), open_mode));

    auto file = std::make_shared<IFile>(system, vfs_file, read_only, commit_state);
    open_files.emplace_back(file);
    *out_interface = std::move(file);
    R_SUCCEED();
}

Result IFileSystem::OpenDirectory(OutInterface<IDirectory> out_interface,
                                  const InLargeData<FileSys::Sf::Path, BufferAttr_HipcPointer> path,
                                  u32 mode) {
    LOG_DEBUG(Service_FS, "called. directory={}, mode={}", path->str, mode);

    FileSys::VirtualDir vfs_dir{};
    R_TRY(backend->OpenDirectory(&vfs_dir, FileSys::Path(path->str),
                                 static_cast<FileSys::OpenDirectoryMode>(mode)));

    *out_interface = std::make_shared<IDirectory>(system, vfs_dir,
                                                  static_cast<FileSys::OpenDirectoryMode>(mode));
    R_SUCCEED();
}

Result IFileSystem::GetEntryType(
    Out<u32> out_type, const InLargeData<FileSys::Sf::Path, BufferAttr_HipcPointer> path) {
    LOG_DEBUG(Service_FS, "called. file={}", path->str);

    FileSys::DirectoryEntryType vfs_entry_type{};
    R_TRY(backend->GetEntryType(&vfs_entry_type, FileSys::Path(path->str)));

    *out_type = static_cast<u32>(vfs_entry_type);
    R_SUCCEED();
}

Result IFileSystem::Commit() {
    LOG_DEBUG(Service_FS, "called, flushing live files and committing dirty host paths");
    R_UNLESS(commit_state != nullptr, ResultUnknown);

    // Flush the exact FILE* streams still owned by live IFile objects first. Closed IFile
    // objects have already gone through fclose(), so their userspace buffers are gone.
    for (auto it = open_files.begin(); it != open_files.end();) {
        if (auto file = it->lock()) {
            R_TRY(file->FlushHostFile());
            ++it;
        } else {
            it = open_files.erase(it);
        }
    }

    // Paths may remain dirty when the guest closed IFile before committing the filesystem.
    // fsync through a fresh descriptor after fclose/fflush makes those writes durable too.
    std::vector<std::string> dirty_paths;
    {
        std::scoped_lock lk{commit_state->mutex};
        dirty_paths.assign(commit_state->dirty_host_paths.begin(),
                           commit_state->dirty_host_paths.end());
    }
    for (const auto& host_path : dirty_paths) {
        if (!Common::FS::IsFile(host_path)) {
            continue;
        }
        Common::FS::IOFile sync_file{host_path, Common::FS::FileAccessMode::ReadWrite,
                                     Common::FS::FileType::BinaryFile,
                                     Common::FS::FileShareFlag::ShareReadWrite};
        R_UNLESS(sync_file.IsOpen(), ResultUnknown);
        R_UNLESS(sync_file.Commit(), ResultUnknown);
        std::scoped_lock lk{commit_state->mutex};
        commit_state->dirty_host_paths.erase(host_path);
    }
    R_SUCCEED();
}

Result IFileSystem::GetFreeSpaceSize(
    Out<s64> out_size, const InLargeData<FileSys::Sf::Path, BufferAttr_HipcPointer> path) {
    LOG_DEBUG(Service_FS, "called");

    *out_size = size_getter.get_free_size();
    R_SUCCEED();
}

Result IFileSystem::GetTotalSpaceSize(
    Out<s64> out_size, const InLargeData<FileSys::Sf::Path, BufferAttr_HipcPointer> path) {
    LOG_DEBUG(Service_FS, "called");

    *out_size = size_getter.get_total_size();
    R_SUCCEED();
}

Result IFileSystem::GetFileTimeStampRaw(
    Out<FileSys::FileTimeStampRaw> out_timestamp,
    const InLargeData<FileSys::Sf::Path, BufferAttr_HipcPointer> path) {
    LOG_DEBUG(Service_FS, "(Partial Implementation) called. file={}", path->str);

    FileSys::FileTimeStampRaw vfs_timestamp{};
    R_TRY(backend->GetFileTimeStampRaw(&vfs_timestamp, FileSys::Path(path->str)));

    *out_timestamp = vfs_timestamp;
    R_SUCCEED();
}


Result IFileSystem::QueryEntry(
    const OutBuffer<BufferAttr_HipcMapAlias | BufferAttr_HipcMapTransferAllowsNonSecure> out_buffer,
    const InBuffer<BufferAttr_HipcMapAlias | BufferAttr_HipcMapTransferAllowsNonSecure> in_buffer,
    FileSys::Fsa::QueryId query_id,
    const InLargeData<FileSys::Sf::Path, BufferAttr_HipcPointer> path) {
    LOG_DEBUG(Service_FS, "called, query_id={}, path={}, in={}, out={}",
              static_cast<u32>(query_id), path->str, in_buffer.size(), out_buffer.size());

    R_RETURN(backend->QueryEntry(reinterpret_cast<char*>(out_buffer.data()), out_buffer.size(),
                                 reinterpret_cast<const char*>(in_buffer.data()), in_buffer.size(),
                                 query_id, FileSys::Path(path->str)));
}

Result IFileSystem::GetFileSystemAttribute(Out<FileSys::FileSystemAttribute> out_attribute) {
    LOG_WARNING(Service_FS, "(STUBBED) called");

    constexpr s32 kEntryNameLengthMax = 0x80;
    constexpr s32 kPathLengthMax = 0x300;

    FileSys::FileSystemAttribute savedata_attribute{};

    savedata_attribute.dir_entry_name_length_max_defined = true;
    savedata_attribute.file_entry_name_length_max_defined = true;
    savedata_attribute.dir_path_name_length_max_defined = true;
    savedata_attribute.file_path_name_length_max_defined = true;

    savedata_attribute.utf16_create_dir_path_len_max_defined = true;
    savedata_attribute.utf16_delete_dir_path_len_max_defined = true;
    savedata_attribute.utf16_rename_src_dir_path_len_max_defined = true;
    savedata_attribute.utf16_rename_dest_dir_path_len_max_defined = true;
    savedata_attribute.utf16_open_dir_path_len_max_defined = true;

    savedata_attribute.utf16_dir_entry_name_length_max_defined = true;
    savedata_attribute.utf16_file_entry_name_length_max_defined = true;
    savedata_attribute.utf16_dir_path_name_length_max_defined = true;
    savedata_attribute.utf16_file_path_name_length_max_defined = true;

    savedata_attribute.dir_entry_name_length_max = kEntryNameLengthMax;
    savedata_attribute.file_entry_name_length_max = kEntryNameLengthMax;
    savedata_attribute.dir_path_name_length_max = kPathLengthMax;
    savedata_attribute.file_path_name_length_max = kPathLengthMax;

    savedata_attribute.utf16_create_dir_path_length_max = kPathLengthMax;
    savedata_attribute.utf16_delete_dir_path_length_max = kPathLengthMax;
    savedata_attribute.utf16_rename_src_dir_path_length_max = kPathLengthMax;
    savedata_attribute.utf16_rename_dest_dir_path_length_max = kPathLengthMax;
    savedata_attribute.utf16_open_dir_path_length_max = kPathLengthMax;

    savedata_attribute.utf16_dir_entry_name_length_max = kEntryNameLengthMax;
    savedata_attribute.utf16_file_entry_name_length_max = kEntryNameLengthMax;
    savedata_attribute.utf16_dir_path_name_length_max = kPathLengthMax;
    savedata_attribute.utf16_file_path_name_length_max = kPathLengthMax;

    *out_attribute = savedata_attribute;
    R_SUCCEED();
}

} // namespace Service::FileSystem
