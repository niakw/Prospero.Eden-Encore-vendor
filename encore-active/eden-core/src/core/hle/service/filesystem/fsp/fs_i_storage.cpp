#include "performance.h"
// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

// SPDX-FileCopyrightText: Copyright 2023 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "core/file_sys/errors.h"
#include "core/hle/service/cmif_serialization.h"
#include "core/hle/service/filesystem/fsp/fs_i_storage.h"

namespace Service::FileSystem {

IStorage::IStorage(Core::System& system_, FileSys::VirtualFile backend_)
    : ServiceFramework{system_, "IStorage"}, backend(std::move(backend_)) {
    static const FunctionInfo functions[] = {
        {0, D<&IStorage::Read>, "Read"},
        {1, nullptr, "Write"},
        {2, nullptr, "Flush"},
        {3, nullptr, "SetSize"},
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

    // Read the data from the Storage backend
    const auto fs_start = ::Eden::Performance::detailed_gpu_profile.load(std::memory_order_relaxed) ? ::Eden::Performance::NowNs() : 0;
    const auto fs_read = backend->Read(out_bytes.data(), length, offset);
    if (fs_read != static_cast<std::size_t>(length))
        LOG_ERROR(Service_FS, "EDEN_FS_SHORT_STORAGE offset={:#x} length={} read={} size={}", offset, length, fs_read, backend->GetSize());
    if (fs_start) {
        ::Eden::Performance::AddSince(::Eden::Performance::guest_fs_storage, fs_start);
        ::Eden::Performance::guest_fs_storage_bytes.fetch_add(static_cast<unsigned long long>(length), std::memory_order_relaxed);
    }

    R_SUCCEED();
}

Result IStorage::GetSize(Out<u64> out_size) {
    *out_size = backend->GetSize();

    LOG_DEBUG(Service_FS, "called, size={}", *out_size);

    R_SUCCEED();
}

} // namespace Service::FileSystem
