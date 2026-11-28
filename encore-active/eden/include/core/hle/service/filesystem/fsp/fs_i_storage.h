// SPDX-FileCopyrightText: Copyright 2023 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <array>
#include <chrono>

#include "core/file_sys/vfs/vfs.h"
#include "core/hle/service/cmif_types.h"
#include "core/hle/service/filesystem/filesystem.h"
#include "core/hle/service/service.h"

namespace Service::FileSystem {

struct StorageQueryRangeInfo {
    s32 aes_ctr_key_type{};
    s32 speed_emulation_type{};
    std::array<u8, 0x38> reserved{};
};
static_assert(sizeof(StorageQueryRangeInfo) == 0x40);

class IStorage final : public ServiceFramework<IStorage> {
public:
    explicit IStorage(Core::System& system_, FileSys::VirtualFile backend_);

private:
    FileSys::VirtualFile backend;
    static constexpr s64 ReadBytesPerSecond = 40'000'000;
    static constexpr s64 MaxChargedReadSize = 64 * 1024;
    std::chrono::steady_clock::time_point next_read_tick{};

    Result Read(
        OutBuffer<BufferAttr_HipcMapAlias | BufferAttr_HipcMapTransferAllowsNonSecure> out_bytes,
        s64 offset, s64 length);
    Result Write(
        InBuffer<BufferAttr_HipcMapAlias | BufferAttr_HipcMapTransferAllowsNonSecure> in_bytes,
        s64 offset, s64 length);
    Result Flush();
    Result SetSize(s64 size);
    Result GetSize(Out<u64> out_size);
    Result OperateRange(Out<StorageQueryRangeInfo> out_info, s32 operation_id, s64 offset,
                        s64 size);
};

class IStorageForBatchRead final : public ServiceFramework<IStorageForBatchRead> {
public:
    explicit IStorageForBatchRead(Core::System& system_, FileSys::VirtualFile backend_);

private:
    FileSys::VirtualFile backend;
    static constexpr s64 ReadBytesPerSecond = 40'000'000;
    static constexpr s64 MaxChargedReadSize = 64 * 1024;
    std::chrono::steady_clock::time_point next_read_tick{};

    Result Read(
        OutBuffer<BufferAttr_HipcMapAlias | BufferAttr_HipcMapTransferAllowsNonSecure> out_bytes,
        s64 offset, s64 length);
    Result Write(
        InBuffer<BufferAttr_HipcMapAlias | BufferAttr_HipcMapTransferAllowsNonSecure> in_bytes,
        s64 offset, s64 length);
    Result Flush();
    Result SetSize(s64 size);
    Result GetSize(Out<u64> out_size);
    void UnsupportedOperateRange(HLERequestContext& ctx);
    Result BatchRead(
        OutBuffer<BufferAttr_HipcMapAlias | BufferAttr_HipcMapTransferAllowsNonSecure> out0,
        OutBuffer<BufferAttr_HipcMapAlias | BufferAttr_HipcMapTransferAllowsNonSecure> out1,
        OutBuffer<BufferAttr_HipcMapAlias | BufferAttr_HipcMapTransferAllowsNonSecure> out2,
        OutBuffer<BufferAttr_HipcMapAlias | BufferAttr_HipcMapTransferAllowsNonSecure> out3,
        OutBuffer<BufferAttr_HipcMapAlias | BufferAttr_HipcMapTransferAllowsNonSecure> out4,
        OutBuffer<BufferAttr_HipcMapAlias | BufferAttr_HipcMapTransferAllowsNonSecure> out5,
        OutBuffer<BufferAttr_HipcMapAlias | BufferAttr_HipcMapTransferAllowsNonSecure> out6,
        InArray<s64, BufferAttr_HipcMapAlias> offsets);
    Result ReadImpl(u8* out, std::size_t capacity, s64 offset, s64 length);
};

} // namespace Service::FileSystem
