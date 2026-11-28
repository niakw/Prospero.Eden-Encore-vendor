#include "performance.h"
// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

// SPDX-FileCopyrightText: Copyright 2023 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <algorithm>
#include <array>
#include <chrono>
#include <limits>
#include <thread>

#include "core/file_sys/errors.h"
#include "core/file_sys/fs_operate_range.h"
#include "core/hle/service/cmif_serialization.h"
#include "core/hle/service/ipc_helpers.h"
#include "core/hle/service/filesystem/fsp/fs_i_storage.h"

namespace Service::FileSystem {

IStorage::IStorage(Core::System& system_, FileSys::VirtualFile backend_)
    : ServiceFramework{system_, "IStorage"}, backend(std::move(backend_)) {
    static const FunctionInfo functions[] = {
        {0, D<&IStorage::Read>, "Read"},
        {1, D<&IStorage::Write>, "Write"},
        {2, D<&IStorage::Flush>, "Flush"},
        {3, D<&IStorage::SetSize>, "SetSize"},
        {4, D<&IStorage::GetSize>, "GetSize"},
        {5, D<&IStorage::OperateRange>, "OperateRange"},
    };
    RegisterHandlers(functions);
}

Result IStorage::Read(
    OutBuffer<BufferAttr_HipcMapAlias | BufferAttr_HipcMapTransferAllowsNonSecure> out_bytes,
    s64 offset, s64 length) {
    LOG_DEBUG(Service_FS, "called, offset={:#x}, length={}", offset, length);

    R_UNLESS(length >= 0, FileSys::ResultInvalidSize);
    R_UNLESS(offset >= 0, FileSys::ResultInvalidOffset);
    // A guest can declare a larger read than its mapped output IPC buffer.
    R_UNLESS(static_cast<u64>(length) <= out_bytes.size(), FileSys::ResultInvalidSize);

    // Charge at most 64 KiB of each request at the 40 MiB/s console-like
    // streaming rate, per IStorage session. Base service locking serializes
    // commands and protects next_read_tick; avoid the old generic service
    // microsleep, which was also applied to unrelated storage users.
    const auto now = std::chrono::steady_clock::now();
    if (now < next_read_tick) {
        std::this_thread::sleep_until(next_read_tick);
    }
    const auto charge = (std::min)(length, MaxChargedReadSize);
    next_read_tick = std::chrono::steady_clock::now() +
        std::chrono::nanoseconds{charge * 1'000'000'000 / ReadBytesPerSecond};

    // Read the data from the Storage backend
    const auto fs_start = ::Eden::Performance::detailed_gpu_profile.load(std::memory_order_relaxed) ? ::Eden::Performance::NowNs() : 0;
    const auto fs_read = backend->Read(out_bytes.data(), length, offset);
    if (fs_read != static_cast<std::size_t>(length))
        LOG_ERROR(Service_FS, "EDEN_FS_SHORT_STORAGE offset={:#x} length={} read={} size={}", offset, length, fs_read, backend->GetSize());
    if (fs_start) {
        ::Eden::Performance::AddSince(::Eden::Performance::guest_fs_storage, fs_start);
        ::Eden::Performance::guest_fs_storage_bytes.fetch_add(
            static_cast<unsigned long long>(fs_read), std::memory_order_relaxed);
    }

    // The storage IPC contract has no partial-read byte count. Do not report
    // success when the backing file supplied fewer bytes than the guest asked for:
    // the untouched portion of the mapped output buffer is not valid content.
    R_UNLESS(fs_read == static_cast<std::size_t>(length), FileSys::ResultOutOfRange);
    R_SUCCEED();
}


Result IStorage::Write(
    InBuffer<BufferAttr_HipcMapAlias | BufferAttr_HipcMapTransferAllowsNonSecure> in_bytes,
    s64 offset, s64 length) {
    LOG_DEBUG(Service_FS, "called, offset={:#x}, length={}", offset, length);

    R_UNLESS(length >= 0, FileSys::ResultInvalidSize);
    R_UNLESS(offset >= 0, FileSys::ResultInvalidOffset);
    R_UNLESS(static_cast<u64>(length) <= in_bytes.size(), FileSys::ResultInvalidSize);
    R_UNLESS(backend->IsWritable(), FileSys::ResultWriteNotPermitted);

    const u64 storage_size = backend->GetSize();
    const u64 write_offset = static_cast<u64>(offset);
    const u64 write_length = static_cast<u64>(length);
    R_UNLESS(write_offset <= storage_size, FileSys::ResultOutOfRange);
    R_UNLESS(write_length <= storage_size - write_offset, FileSys::ResultOutOfRange);

    const auto written = backend->Write(in_bytes.data(), static_cast<std::size_t>(write_length),
                                        static_cast<std::size_t>(write_offset));
    R_UNLESS(written == static_cast<std::size_t>(write_length), FileSys::ResultWriteNotPermitted);

    R_SUCCEED();
}

Result IStorage::Flush() {
    // There is no deferred flush queue in the VFS. Writes complete in
    // IStorage::Write, matching FileSys::Fsa::IFile::DoFlush semantics.
    R_SUCCEED();
}

Result IStorage::SetSize(s64 size) {
    LOG_DEBUG(Service_FS, "called, size={}", size);

    R_UNLESS(size >= 0, FileSys::ResultInvalidSize);
    R_UNLESS(backend->IsWritable(), FileSys::ResultWriteNotPermitted);
    R_UNLESS(backend->Resize(static_cast<std::size_t>(size)), FileSys::ResultWriteNotPermitted);

    R_SUCCEED();
}

Result IStorage::GetSize(Out<u64> out_size) {
    *out_size = backend->GetSize();

    LOG_DEBUG(Service_FS, "called, size={}", *out_size);

    R_SUCCEED();
}

Result IStorage::OperateRange(Out<StorageQueryRangeInfo> out_info, s32 operation_id,
                              s64 offset, s64 size) {
    LOG_DEBUG(Service_FS, "called, operation_id={}, offset={:#x}, size={}",
              operation_id, offset, size);

    switch (static_cast<FileSys::OperationId>(operation_id)) {
    case FileSys::OperationId::Invalidate:
        // VirtualFile has no separate storage-range cache to invalidate.
        *out_info = {};
        R_SUCCEED();
    case FileSys::OperationId::QueryRange:
        // This VFS storage does not apply AES-CTR or speed-emulation range metadata.
        *out_info = {};
        R_SUCCEED();
    default:
        R_THROW(FileSys::ResultNotImplemented);
    }
}

IStorageForBatchRead::IStorageForBatchRead(Core::System& system_, FileSys::VirtualFile backend_)
    : ServiceFramework{system_, "IStorageForBatchRead"}, backend(std::move(backend_)) {
    static const FunctionInfo functions[] = {
        {0, D<&IStorageForBatchRead::Read>, "Read"},
        {1, D<&IStorageForBatchRead::Write>, "Write"},
        {2, D<&IStorageForBatchRead::Flush>, "Flush"},
        {3, D<&IStorageForBatchRead::SetSize>, "SetSize"},
        {4, D<&IStorageForBatchRead::GetSize>, "GetSize"},
        {5, &IStorageForBatchRead::UnsupportedOperateRange, "OperateRange"},
        {10, D<&IStorageForBatchRead::BatchRead>, "BatchRead"},
    };
    RegisterHandlers(functions);
}

Result IStorageForBatchRead::ReadImpl(u8* out, std::size_t capacity, s64 offset, s64 length) {
    R_UNLESS(length >= 0, FileSys::ResultInvalidSize);
    R_UNLESS(offset >= 0, FileSys::ResultInvalidOffset);
    R_UNLESS(static_cast<u64>(length) <= capacity, FileSys::ResultInvalidSize);

    const auto now = std::chrono::steady_clock::now();
    if (now < next_read_tick) {
        std::this_thread::sleep_until(next_read_tick);
    }
    const auto charge = (std::min)(length, MaxChargedReadSize);
    next_read_tick = std::chrono::steady_clock::now() +
        std::chrono::nanoseconds{charge * 1'000'000'000 / ReadBytesPerSecond};

    const auto fs_start =
        ::Eden::Performance::detailed_gpu_profile.load(std::memory_order_relaxed)
            ? ::Eden::Performance::NowNs()
            : 0;
    const auto fs_read = backend->Read(out, static_cast<std::size_t>(length),
                                       static_cast<std::size_t>(offset));
    if (fs_start) {
        ::Eden::Performance::AddSince(::Eden::Performance::guest_fs_storage, fs_start);
        ::Eden::Performance::guest_fs_storage_bytes.fetch_add(
            static_cast<unsigned long long>(fs_read), std::memory_order_relaxed);
    }
    R_UNLESS(fs_read == static_cast<std::size_t>(length), FileSys::ResultOutOfRange);
    R_SUCCEED();
}

Result IStorageForBatchRead::Read(
    OutBuffer<BufferAttr_HipcMapAlias | BufferAttr_HipcMapTransferAllowsNonSecure> out_bytes,
    s64 offset, s64 length) {
    R_RETURN(ReadImpl(out_bytes.data(), out_bytes.size(), offset, length));
}

Result IStorageForBatchRead::Write(
    InBuffer<BufferAttr_HipcMapAlias | BufferAttr_HipcMapTransferAllowsNonSecure> in_bytes,
    s64 offset, s64 length) {
    R_UNLESS(length >= 0, FileSys::ResultInvalidSize);
    R_UNLESS(offset >= 0, FileSys::ResultInvalidOffset);
    R_UNLESS(static_cast<u64>(length) <= in_bytes.size(), FileSys::ResultInvalidSize);
    R_UNLESS(backend->IsWritable(), FileSys::ResultWriteNotPermitted);

    const u64 storage_size = backend->GetSize();
    const u64 write_offset = static_cast<u64>(offset);
    const u64 write_length = static_cast<u64>(length);
    R_UNLESS(write_offset <= storage_size, FileSys::ResultOutOfRange);
    R_UNLESS(write_length <= storage_size - write_offset, FileSys::ResultOutOfRange);
    const auto written = backend->Write(in_bytes.data(), static_cast<std::size_t>(write_length),
                                        static_cast<std::size_t>(write_offset));
    R_UNLESS(written == static_cast<std::size_t>(write_length),
             FileSys::ResultWriteNotPermitted);
    R_SUCCEED();
}

Result IStorageForBatchRead::Flush() {
    R_SUCCEED();
}

Result IStorageForBatchRead::SetSize(s64 size) {
    R_UNLESS(size >= 0, FileSys::ResultInvalidSize);
    R_UNLESS(backend->IsWritable(), FileSys::ResultWriteNotPermitted);
    R_UNLESS(backend->Resize(static_cast<std::size_t>(size)), FileSys::ResultWriteNotPermitted);
    R_SUCCEED();
}

Result IStorageForBatchRead::GetSize(Out<u64> out_size) {
    *out_size = backend->GetSize();
    R_SUCCEED();
}

void IStorageForBatchRead::UnsupportedOperateRange(HLERequestContext& ctx) {
    IPC::ResponseBuilder rb{ctx, 2};
    rb.Push(FileSys::ResultNotImplemented);
}

Result IStorageForBatchRead::BatchRead(
    OutBuffer<BufferAttr_HipcMapAlias | BufferAttr_HipcMapTransferAllowsNonSecure> out0,
    OutBuffer<BufferAttr_HipcMapAlias | BufferAttr_HipcMapTransferAllowsNonSecure> out1,
    OutBuffer<BufferAttr_HipcMapAlias | BufferAttr_HipcMapTransferAllowsNonSecure> out2,
    OutBuffer<BufferAttr_HipcMapAlias | BufferAttr_HipcMapTransferAllowsNonSecure> out3,
    OutBuffer<BufferAttr_HipcMapAlias | BufferAttr_HipcMapTransferAllowsNonSecure> out4,
    OutBuffer<BufferAttr_HipcMapAlias | BufferAttr_HipcMapTransferAllowsNonSecure> out5,
    OutBuffer<BufferAttr_HipcMapAlias | BufferAttr_HipcMapTransferAllowsNonSecure> out6,
    InArray<s64, BufferAttr_HipcMapAlias> offsets) {
    R_UNLESS(offsets.size() <= 7, FileSys::ResultInvalidSize);

    using BatchBuffer =
        OutBuffer<BufferAttr_HipcMapAlias | BufferAttr_HipcMapTransferAllowsNonSecure>;
    const std::array<BatchBuffer, 7> outputs{out0, out1, out2, out3, out4, out5, out6};

    for (std::size_t i = 0; i < offsets.size(); ++i) {
        R_UNLESS(outputs[i].size() <= static_cast<std::size_t>(std::numeric_limits<s64>::max()),
                 FileSys::ResultInvalidSize);
        R_TRY(ReadImpl(outputs[i].data(), outputs[i].size(), offsets[i],
                       static_cast<s64>(outputs[i].size())));
    }
    R_SUCCEED();
}

} // namespace Service::FileSystem
