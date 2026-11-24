// SPDX-FileCopyrightText: Copyright 2023 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <array>

#include "core/file_sys/fsa/fs_i_file.h"
#include "core/hle/service/cmif_types.h"
#include "core/hle/service/filesystem/filesystem.h"
#include "core/hle/service/service.h"

namespace Service::FileSystem {

struct FileQueryRangeInfo {
    u32 aes_ctr_key_type{};
    u32 speed_emulation_type{};
    std::array<u8, 0x38> reserved{};
};
static_assert(sizeof(FileQueryRangeInfo) == 0x40);

class IFile final : public ServiceFramework<IFile> {
public:
    explicit IFile(Core::System& system_, FileSys::VirtualFile file_, bool read_only_ = false);

private:
    std::unique_ptr<FileSys::Fsa::IFile> backend;
    bool read_only{};

    Result Read(FileSys::ReadOption option, Out<s64> out_size, s64 offset,
                const OutBuffer<BufferAttr_HipcMapAlias | BufferAttr_HipcMapTransferAllowsNonSecure>
                    out_buffer,
                s64 size);
    Result Write(
        const InBuffer<BufferAttr_HipcMapAlias | BufferAttr_HipcMapTransferAllowsNonSecure> buffer,
        FileSys::WriteOption option, s64 offset, s64 size);
    Result Flush();
    Result SetSize(s64 size);
    Result GetSize(Out<s64> out_size);
    Result OperateRange(Out<FileQueryRangeInfo> out_info, s32 op_id, s64 offset, s64 size);
    Result OperateRangeWithBuffer(
        const OutBuffer<BufferAttr_HipcMapAlias | BufferAttr_HipcMapTransferAllowsNonSecure>
            out_buffer,
        const InBuffer<BufferAttr_HipcMapAlias | BufferAttr_HipcMapTransferAllowsNonSecure>
            in_buffer,
        s32 op_id, s64 offset, s64 size);
};

} // namespace Service::FileSystem
