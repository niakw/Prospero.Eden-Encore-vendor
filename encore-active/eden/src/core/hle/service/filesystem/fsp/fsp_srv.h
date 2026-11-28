// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

// SPDX-FileCopyrightText: Copyright 2018 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <array>
#include <memory>
#include <vector>
#include "core/file_sys/fs_save_data_types.h"
#include "core/file_sys/fssrv/fssrv_sf_path.h"
#include "core/hle/service/cmif_types.h"
#include "core/hle/service/filesystem/fsp/fsp_types.h"
#include "core/hle/service/service.h"

namespace Core {
class Reporter;
}

namespace FileSys {
class ContentProvider;
class FileSystemBackend;
} // namespace FileSys

namespace Service::FileSystem {

class RomFsController;
class SaveDataController;

class IFileSystem;
class ISaveDataInfoReader;
class ISaveDataTransferProhibiter;
class IStorage;
class IStorageForBatchRead;
class IMultiCommitManager;

enum class AccessLogVersion : u32 {
    V7_0_0 = 2,

    Latest = V7_0_0,
};

enum class AccessLogMode : u32 {
    None,
    Log,
    SdCard,
};

using FspRightsId = std::array<u8, 0x10>;
static_assert(sizeof(FspRightsId) == 0x10);
using FspAccessKey = std::array<u8, 0x10>;
static_assert(sizeof(FspAccessKey) == 0x10);

struct RightsIdAndKeyGeneration {
    u8 key_generation{};
    std::array<u8, 0x7> padding{};
    FspRightsId rights_id{};
};
static_assert(sizeof(RightsIdAndKeyGeneration) == 0x18);

struct ProgramIndexMapInfo {
    u64 program_id{};
    u64 main_program_id{};
    u8 program_index{};
    std::array<u8, 0xF> reserved{};
};
static_assert(sizeof(ProgramIndexMapInfo) == 0x20);

class FSP_SRV final : public ServiceFramework<FSP_SRV> {
public:
    explicit FSP_SRV(Core::System& system_);
    ~FSP_SRV() override;

private:
    void UnsupportedDebugCommand(HLERequestContext& ctx);
    void RejectUnavailableStorageCommand(HLERequestContext& ctx);
    Result OpenFileSystem(
        OutInterface<IFileSystem> out_interface, FileSystemProxyType type,
        InLargeData<FileSys::Sf::Path, BufferAttr_HipcPointer> path);
    Result OpenSaveDataInternalStorageFileSystem(
        OutInterface<IFileSystem> out_interface, FileSys::SaveDataSpaceId space_id,
        u64 save_data_id);
    Result QuerySaveDataInternalStorageTotalSize(
        Out<s64> out_size, FileSys::SaveDataSpaceId space_id, u64 save_data_id);
    Result SetCurrentProcess(ClientProcessId pid);
    Result OpenDataFileSystemByCurrentProcess(OutInterface<IFileSystem> out_interface);
    Result OpenDataFileSystemByProgramId(OutInterface<IFileSystem> out_interface,
                                         u64 open_program_id);
    Result OpenDataFileSystemWithProgramIndex(OutInterface<IFileSystem> out_interface,
                                              u8 program_index);
    Result OpenFileSystemWithPatch(OutInterface<IFileSystem> out_interface,
                                   FileSystemProxyType type, u64 open_program_id);
    Result OpenFileSystemWithIdObsolete(
        OutInterface<IFileSystem> out_interface, FileSystemProxyType type, u64 open_program_id,
        InLargeData<FileSys::Sf::Path, BufferAttr_HipcPointer> path);
    Result OpenFileSystemWithId(
        OutInterface<IFileSystem> out_interface, ContentAttributes attributes,
        FileSystemProxyType type, u64 open_program_id,
        InLargeData<FileSys::Sf::Path, BufferAttr_HipcPointer> path);
    Result OpenFileSystemWithIdImpl(OutInterface<IFileSystem> out_interface,
                                    ContentAttributes attributes, FileSystemProxyType type,
                                    u64 open_program_id, const FileSys::Sf::Path& path);
    Result OpenBisFileSystem(
        OutInterface<IFileSystem> out_interface, FileSys::BisPartitionId partition_id,
        InLargeData<FileSys::Sf::Path, BufferAttr_HipcPointer> path);
    Result OpenBisStorage(OutInterface<IStorage> out_interface,
                          FileSys::BisPartitionId partition_id);
    Result InvalidateBisCache();
    Result GetFreeSpaceSizeForSaveData(Out<s64> out_free_space_size,
                                       FileSys::SaveDataSpaceId space_id);
    Result OpenSdCardFileSystem(OutInterface<IFileSystem> out_interface);
    Result CreateSaveDataFileSystem(FileSys::SaveDataCreationInfo save_create_struct,
                                    FileSys::SaveDataAttribute save_struct, u128 uid);
    Result CreateSaveDataFileSystemBySystemSaveDataId(
        FileSys::SaveDataAttribute save_struct, FileSys::SaveDataCreationInfo save_create_struct);
    Result CreateSaveDataFileSystemWithHashSalt(
        FileSys::SaveDataAttribute attribute, FileSys::SaveDataCreationInfo creation_info,
        FileSys::SaveDataMetaInfo meta_info, FileSys::HashSalt hash_salt);
    Result CreateSaveDataFileSystemWithCreationInfo2(
        FileSys::SaveDataCreationInfo2 save_data_creation_info);
    Result IsExFatSupported(Out<bool> out_is_supported);
    Result OpenGameCardStorage(OutInterface<IStorage> out_interface, u32 handle, u32 partition_id);
    Result OpenGameCardFileSystem(OutInterface<IFileSystem> out_interface, u32 handle,
                                  u32 partition_id);
    Result OpenSaveDataFileSystem(OutInterface<IFileSystem> out_interface,
                                  FileSys::SaveDataSpaceId space_id,
                                  FileSys::SaveDataAttribute attribute);
    Result OpenSaveDataFileSystemBySystemSaveDataId(OutInterface<IFileSystem> out_interface,
                                                    FileSys::SaveDataSpaceId space_id,
                                                    FileSys::SaveDataAttribute attribute);
    Result OpenReadOnlySaveDataFileSystem(OutInterface<IFileSystem> out_interface,
                                          FileSys::SaveDataSpaceId space_id,
                                          FileSys::SaveDataAttribute attribute);
    Result OpenSaveDataInfoReader(OutInterface<ISaveDataInfoReader> out_interface);
    Result OpenSaveDataInfoReaderBySaveDataSpaceId(OutInterface<ISaveDataInfoReader> out_interface,
                                                   FileSys::SaveDataSpaceId space);
    Result OpenSaveDataInfoReaderOnlyCacheStorage(OutInterface<ISaveDataInfoReader> out_interface);
    Result OpenSaveDataInfoReaderWithFilter(OutInterface<ISaveDataInfoReader> out_interface,
                                            FileSys::SaveDataSpaceId space_id,
                                            FileSys::SaveDataFilter filter);
    Result WriteSaveDataFileSystemExtraDataWithMask(
        InBuffer<BufferAttr_HipcMapAlias> buffer, InBuffer<BufferAttr_HipcMapAlias> mask_buffer,
        FileSys::SaveDataSpaceId space_id, u64 save_data_id);
    Result FindSaveDataWithFilter(Out<s64> out_count, OutBuffer<BufferAttr_HipcMapAlias> out_buffer,
                                  FileSys::SaveDataSpaceId space_id,
                                  FileSys::SaveDataFilter filter);
    Result WriteSaveDataFileSystemExtraData(InBuffer<BufferAttr_HipcMapAlias> buffer,
                                            FileSys::SaveDataSpaceId space_id, u64 save_data_id);
    Result WriteSaveDataFileSystemExtraDataWithMaskBySaveDataAttribute(
        InBuffer<BufferAttr_HipcMapAlias> buffer, InBuffer<BufferAttr_HipcMapAlias> mask_buffer,
        FileSys::SaveDataSpaceId space_id, FileSys::SaveDataAttribute attribute);
    Result ReadSaveDataFileSystemExtraData(OutBuffer<BufferAttr_HipcMapAlias> out_buffer,
                                           u64 save_data_id);
    Result ReadSaveDataFileSystemExtraDataBySaveDataAttribute(
        OutBuffer<BufferAttr_HipcMapAlias> out_buffer, FileSys::SaveDataSpaceId space_id,
        FileSys::SaveDataAttribute attribute);
    Result ReadSaveDataFileSystemExtraDataBySaveDataSpaceId(
        OutBuffer<BufferAttr_HipcMapAlias> out_buffer, FileSys::SaveDataSpaceId space_id,
        u64 save_data_id);
    Result ReadSaveDataFileSystemExtraDataWithMaskBySaveDataAttribute(
        FileSys::SaveDataSpaceId space_id, FileSys::SaveDataAttribute attribute,
        InBuffer<BufferAttr_HipcMapAlias> mask_buffer,
        OutBuffer<BufferAttr_HipcMapAlias> out_buffer);
    Result OpenSaveDataTransferProhibiter(OutInterface<ISaveDataTransferProhibiter> out_prohibiter,
                                          u64 id);
    Result OpenImageDirectoryFileSystem(OutInterface<IFileSystem> out_interface,
                                        ImageDirectoryId image_directory_id);
    Result OpenContentStorageFileSystem(OutInterface<IFileSystem> out_interface,
                                        ContentStorageId content_storage_id);
    Result OpenDataStorageByCurrentProcess(OutInterface<IStorage> out_interface);
    Result OpenDataStorageByProgramId(OutInterface<IStorage> out_interface, u64 program_id);
    Result OpenDataStorageByDataId(OutInterface<IStorage> out_interface,
                                   FileSys::StorageId storage_id, u32 unknown, u64 title_id);
    Result OpenDataFileSystemByDataId(OutInterface<IFileSystem> out_interface,
                                      FileSys::StorageId storage_id, u32 padding,
                                      u64 data_id);
    Result OpenPatchDataStorageByCurrentProcess(OutInterface<IStorage> out_interface);
    Result OpenDataStorageWithProgramIndex(OutInterface<IStorage> out_interface, u8 program_index);
    Result OpenDataStorageByPath(
        OutInterface<IStorage> out_interface, ContentAttributes attributes,
        FileSystemProxyType type,
        InLargeData<FileSys::Sf::Path, BufferAttr_HipcPointer> path);
    Result OpenDataStorageByCurrentProcessForBatchRead(
        OutInterface<IStorageForBatchRead> out_interface);
    Result OpenDataStorageByProgramIdForBatchRead(
        OutInterface<IStorageForBatchRead> out_interface, u64 program_id);
    Result OpenDataStorageWithProgramIndexForBatchRead(
        OutInterface<IStorageForBatchRead> out_interface, u8 program_index);
    Result OpenDataStorageByPathForBatchRead(
        OutInterface<IStorageForBatchRead> out_interface, ContentAttributes attributes,
        FileSystemProxyType type,
        InLargeData<FileSys::Sf::Path, BufferAttr_HipcPointer> path);
    Result GetRightsId(Out<FspRightsId> out_rights_id, FileSys::StorageId storage_id,
                       u64 program_id_);
    Result GetRightsIdByPath(
        Out<FspRightsId> out_rights_id,
        InLargeData<FileSys::Sf::Path, BufferAttr_HipcPointer> path);
    Result GetRightsIdAndKeyGenerationByPath(
        Out<RightsIdAndKeyGeneration> out_result, ContentAttributes attributes,
        InLargeData<FileSys::Sf::Path, BufferAttr_HipcPointer> path);
    Result GetProgramIdByPath(
        Out<u64> out_program_id, ContentAttributes attributes,
        InLargeData<FileSys::Sf::Path, BufferAttr_HipcPointer> path);
    Result RegisterExternalKey(FspRightsId rights_id, FspAccessKey access_key);
    Result UnregisterAllExternalKey();
    Result UnregisterExternalKey(FspRightsId rights_id);
    Result SetSdCardAccessibility(bool is_accessible);
    Result IsSdCardAccessible(Out<bool> out_is_accessible);
    Result DisableAutoSaveDataCreation();
    Result SetGlobalAccessLogMode(AccessLogMode access_log_mode_);
    Result GetGlobalAccessLogMode(Out<AccessLogMode> out_access_log_mode);
    Result OutputAccessLogToSdCard(InBuffer<BufferAttr_HipcMapAlias> log_message_buffer);
    Result RegisterProgramIndexMapInfo(InBuffer<BufferAttr_HipcMapAlias> map_info_buffer,
                                       s32 program_count);
    Result FlushAccessLogOnSdCard();
    Result GetProgramIndexForAccessLog(Out<AccessLogVersion> out_access_log_version,
                                       Out<u32> out_access_log_program_index);
    Result OpenMultiCommitManager(OutInterface<IMultiCommitManager> out_interface);
    Result ExtendSaveDataFileSystem(FileSys::SaveDataSpaceId space_id, u64 save_data_id,
                                    s64 available_size, s64 journal_size);
    Result DeleteCacheStorage(u16 index);
    Result GetCacheStorageSize(s32 index, Out<s64> out_data_size, Out<s64> out_journal_size);

    FileSystemController& fsc;
    const FileSys::ContentProvider& content_provider;
    const Core::Reporter& reporter;

    FileSys::VirtualFile romfs;
    u64 current_process_id = 0;
    std::vector<ProgramIndexMapInfo> program_index_map;
    AccessLogMode access_log_mode = AccessLogMode::None;
    bool sd_card_accessible = true;
    u64 program_id = 0;
    std::shared_ptr<SaveDataController> save_data_controller;
    std::shared_ptr<RomFsController> romfs_controller;
};

} // namespace Service::FileSystem
