// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

// SPDX-FileCopyrightText: Copyright 2023 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <cstring>

#include "core/file_sys/errors.h"
#include "core/hle/service/cmif_serialization.h"
#include "core/hle/service/filesystem/fsp/fs_i_file.h"

namespace Service::FileSystem {

IFile::IFile(Core::System& system_, FileSys::VirtualFile file_, bool read_only_)
    : ServiceFramework{system_, "IFile"}, backend{std::make_unique<FileSys::Fsa::IFile>(file_)},
      read_only{read_only_} {
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

    R_RETURN(
        backend->Read(reinterpret_cast<size_t*>(out_size.Get()), offset, out_buffer.data(), size));
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

    R_RETURN(backend->Write(offset, buffer.data(), size, option));
}

Result IFile::Flush() {
    LOG_DEBUG(Service_FS, "called");

    R_RETURN(backend->Flush());
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
