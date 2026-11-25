#include "performance.h"
// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

// SPDX-FileCopyrightText: Copyright 2023 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <algorithm>
#include <chrono>
#include <thread>

#include "core/file_sys/errors.h"
#include "core/hle/service/cmif_serialization.h"
#include "core/hle/service/filesystem/fsp/fs_i_storage.h"

namespace Service::FileSystem {

IStorage::IStorage(Core::System& system_, FileSys::VirtualFile backend_)
    : ServiceFramework{system_, "IStorage"}, backend(std::move(backend_)) {
    static const FunctionInfo functions[] = {
        {0, D<&IStorage::Read>, "Read"},
        {1, D<&IStorage::Write>, "Write"},
        {2, nullptr, "Flush"},
        {3, D<&IStorage::SetSize>, "SetSize"},
        {4, D<&IStorage::GetSize>, "GetSize"},
        {5, nullptr, "OperateRange"},
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

} // namespace Service::FileSystem
