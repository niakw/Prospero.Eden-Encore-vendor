#include "performance.h"
// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

// SPDX-FileCopyrightText: Copyright 2023 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <cstring>

#include "common/overflow.h"
#include "common/fs/file.h"
#include "common/fs/fs.h"

#include "core/file_sys/errors.h"
#include "core/hle/service/cmif_serialization.h"
#include "core/hle/service/filesystem/fsp/fs_i_file.h"

namespace Service::FileSystem {

IFile::IFile(Core::System& system_, FileSys::VirtualFile file_, bool read_only_,
             std::shared_ptr<FileCommitState> commit_state_)
    : ServiceFramework{system_, "IFile"}, backend{std::make_unique<FileSys::Fsa::IFile>(file_)},
      vfs_file{file_}, commit_state{std::move(commit_state_)}, read_only{read_only_} {
    // clang-format off
    static const FunctionInfo functions[] = {
        {0, D<&IFile::Read>, "Read"},
        {1, D<&IFile::Write>, "Write"},
        {2, D<&IFile::Flush>, "Flush"},
        {3, D<&IFile::SetSize>, "SetSize"},
        {4, D<&IFile::GetSize>, "GetSize"},
        {5, D<&IFile::OperateRange>, "OperateRange"},
        {6, D<&IFile::OperateRangeWithBuffer>, "OperateRangeWithBuffer"},
    };
    // clang-format on
    RegisterHandlers(functions);
}


void IFile::MarkDirtyHostPath() {
    if (!commit_state || !vfs_file || !vfs_file->IsWritable()) {
        return;
    }
    const auto host_path = vfs_file->GetFullPath();
    if (host_path.empty() || !Common::FS::IsFile(host_path)) {
        return;
    }
    std::scoped_lock lk{commit_state->mutex};
    commit_state->dirty_host_paths.insert(host_path);
}

void IFile::ClearDirtyHostPath(const std::string& host_path) {
    if (!commit_state || host_path.empty()) {
        return;
    }
    std::scoped_lock lk{commit_state->mutex};
    commit_state->dirty_host_paths.erase(host_path);
}

Result IFile::FlushHostFile() {
    R_UNLESS(vfs_file != nullptr, FileSys::ResultInvalidArgument);

    // RealVfsFile::GetSize() flushes the same IOFile stream before stat'ing the file.
    // For virtual/read-only files there may be no host path to commit; preserve compatibility.
    static_cast<void>(vfs_file->GetSize());
    if (!vfs_file->IsWritable()) {
        R_SUCCEED();
    }

    const auto host_path = vfs_file->GetFullPath();
    if (host_path.empty() || !Common::FS::IsFile(host_path)) {
        R_SUCCEED();
    }

    Common::FS::IOFile sync_file{host_path, Common::FS::FileAccessMode::ReadWrite,
                                 Common::FS::FileType::BinaryFile,
                                 Common::FS::FileShareFlag::ShareReadWrite};
    R_UNLESS(sync_file.IsOpen(), ResultUnknown);
    R_UNLESS(sync_file.Commit(), ResultUnknown);
    ClearDirtyHostPath(host_path);
    R_SUCCEED();
}

Result IFile::Read(
    FileSys::ReadOption option, Out<s64> out_size, s64 offset,
    const OutBuffer<BufferAttr_HipcMapAlias | BufferAttr_HipcMapTransferAllowsNonSecure> out_buffer,
    s64 size) {
    LOG_DEBUG(Service_FS, "called, option={}, offset={:#x}, length={}", option.value, offset,
              size);

    // The requested guest transfer length must fit the mapped IPC output buffer.
    // A negative s64 would otherwise wrap to a huge size_t inside Fsa::IFile.
    R_UNLESS(offset >= 0, FileSys::ResultInvalidOffset);
    R_UNLESS(size >= 0 && static_cast<u64>(size) <= out_buffer.size(),
             FileSys::ResultInvalidSize);

    const auto fs_start = ::Eden::Performance::detailed_gpu_profile.load(std::memory_order_relaxed) ? ::Eden::Performance::NowNs() : 0;
    const Result fs_result =
        backend->Read(reinterpret_cast<size_t*>(out_size.Get()), offset, out_buffer.data(), size);
    if (fs_start) {
        ::Eden::Performance::AddSince(::Eden::Performance::guest_fs_file, fs_start);
        ::Eden::Performance::guest_fs_file_bytes.fetch_add(static_cast<unsigned long long>(*out_size.Get()), std::memory_order_relaxed);
    // Diagnostic only: avoid formatting every file read while quiet.
        LOG_WARNING(Service_FS, "EDEN_FS_FILE_READ offset={:#x} size={} read={} result={:#x} ns={}", offset, size, *out_size.Get(), fs_result.raw, ::Eden::Performance::NowNs() - fs_start);
    }
    R_RETURN(fs_result);
}

Result IFile::Write(
    const InBuffer<BufferAttr_HipcMapAlias | BufferAttr_HipcMapTransferAllowsNonSecure> buffer,
    FileSys::WriteOption option, s64 offset, s64 size) {
    LOG_DEBUG(Service_FS, "called, option={}, offset={:#x}, length={}", option.value, offset,
              size);
    R_UNLESS(!read_only, FileSys::ResultWriteNotPermitted);
    R_UNLESS(offset >= 0, FileSys::ResultInvalidOffset);
    R_UNLESS(size >= 0 && static_cast<u64>(size) <= buffer.size(),
             FileSys::ResultInvalidSize);

    // Fsa::IFile::DoWrite asserts on a short write but can still report success.
    // Use the same VFS file directly so the guest receives an error for an incomplete write.
    R_UNLESS(Common::CanAddWithoutOverflow<s64>(offset, size), FileSys::ResultOutOfRange);
    if (size == 0) {
        if (option.HasFlushFlag()) {
            R_TRY(backend->Flush());
        }
        R_SUCCEED();
    }
    R_UNLESS(vfs_file != nullptr, FileSys::ResultInvalidArgument);
    R_UNLESS(buffer.data() != nullptr, FileSys::ResultNullptrArgument);
    const auto requested = static_cast<std::size_t>(size);
    const auto written = vfs_file->Write(buffer.data(), requested,
                                         static_cast<std::size_t>(offset));
    if (written != requested) {
        LOG_ERROR(Service_FS, "Incomplete guest file write: requested={}, written={}",
                  requested, written);
        R_RETURN(ResultUnknown);
    }
    MarkDirtyHostPath();
    if (option.HasFlushFlag()) {
        R_TRY(FlushHostFile());
    }
    R_SUCCEED();
}

Result IFile::Flush() {
    LOG_DEBUG(Service_FS, "called");

    R_RETURN(FlushHostFile());
}

Result IFile::SetSize(s64 size) {
    LOG_DEBUG(Service_FS, "called, size={}", size);
    R_UNLESS(!read_only, FileSys::ResultWriteNotPermitted);
    R_UNLESS(size >= 0, FileSys::ResultInvalidSize);

    // Fsa::IFile::DoSetSize currently ignores the VFS Resize() bool.
    // Verify the observable size rather than reporting success after a failed resize.
    R_TRY(backend->SetSize(size));
    s64 actual_size{};
    R_TRY(backend->GetSize(&actual_size));
    R_UNLESS(actual_size == size, ResultUnknown);
    MarkDirtyHostPath();
    R_SUCCEED();
}


Result IFile::OperateRange(Out<FileQueryRangeInfo> out_info, s32 op_id, s64 offset, s64 size) {
    LOG_DEBUG(Service_FS, "called, op_id={}, offset={:#x}, size={}", op_id, offset, size);

    *out_info = {};
    switch (static_cast<FileSys::OperationId>(op_id)) {
    case FileSys::OperationId::Invalidate:
        R_SUCCEED();
    case FileSys::OperationId::QueryRange: {
        R_UNLESS(offset >= 0 && size >= 0, FileSys::ResultOutOfRange);
        s64 file_size{};
        R_TRY(backend->GetSize(&file_size));
        R_UNLESS(offset <= file_size, FileSys::ResultOutOfRange);
        R_SUCCEED();
    }
    default:
        R_SUCCEED();
    }
}

Result IFile::OperateRangeWithBuffer(
    const OutBuffer<BufferAttr_HipcMapAlias | BufferAttr_HipcMapTransferAllowsNonSecure> out_buffer,
    const InBuffer<BufferAttr_HipcMapAlias | BufferAttr_HipcMapTransferAllowsNonSecure> in_buffer,
    s32 op_id, s64 offset, s64 size) {
    LOG_DEBUG(Service_FS, "called, op_id={}, offset={:#x}, size={}, in={}, out={}", op_id, offset,
              size, in_buffer.size(), out_buffer.size());

    switch (static_cast<FileSys::OperationId>(op_id)) {
    case FileSys::OperationId::Invalidate:
        R_SUCCEED();
    case FileSys::OperationId::QueryRange:
        R_UNLESS(out_buffer.size() >= sizeof(FileQueryRangeInfo), FileSys::ResultOutOfRange);
        std::memset(out_buffer.data(), 0, sizeof(FileQueryRangeInfo));
        R_SUCCEED();
    default:
        R_RETURN(FileSys::ResultNotImplemented);
    }
}

Result IFile::GetSize(Out<s64> out_size) {
    LOG_DEBUG(Service_FS, "called");

    R_RETURN(backend->GetSize(out_size));
}

} // namespace Service::FileSystem
