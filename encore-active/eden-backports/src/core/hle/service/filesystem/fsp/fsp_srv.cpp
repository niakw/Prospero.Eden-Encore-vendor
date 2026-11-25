// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

// SPDX-FileCopyrightText: Copyright 2018 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <algorithm>
#include <cinttypes>
#include <cstring>
#include <iterator>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "common/assert.h"
#include "common/common_types.h"
#include "common/hex_util.h"
#include "common/logging.h"
#include "common/settings.h"
#include "common/string_util.h"
#include "core/core.h"
#include "core/crypto/key_manager.h"
#include "core/file_sys/bis_factory.h"
#include "core/file_sys/card_image.h"
#include "core/file_sys/content_archive.h"
#include "core/file_sys/errors.h"
#include "core/file_sys/fs_directory.h"
#include "core/file_sys/fs_filesystem.h"
#include "core/file_sys/nca_metadata.h"
#include "core/file_sys/patch_manager.h"
#include "core/file_sys/registered_cache.h"
#include "core/file_sys/romfs.h"
#include "core/file_sys/romfs_factory.h"
#include "core/file_sys/savedata_factory.h"
#include "core/file_sys/system_archive/system_archive.h"
#include "core/file_sys/vfs/vfs.h"
#include "core/hle/result.h"
#include "core/hle/service/cmif_serialization.h"
#include "core/hle/service/filesystem/filesystem.h"
#include "core/hle/service/filesystem/fsp/fs_i_filesystem.h"
#include "core/hle/service/filesystem/fsp/fs_i_multi_commit_manager.h"
#include "core/hle/service/filesystem/fsp/fs_i_save_data_info_reader.h"
#include "core/hle/service/filesystem/fsp/fs_i_storage.h"
#include "core/hle/service/filesystem/fsp/fsp_srv.h"
#include "core/hle/service/filesystem/fsp/save_data_transfer_prohibiter.h"
#include "core/hle/service/filesystem/romfs_controller.h"
#include "core/hle/service/filesystem/save_data_controller.h"
#include "core/hle/service/hle_ipc.h"
#include "core/hle/service/ipc_helpers.h"
#include "core/loader/loader.h"
#include "core/reporter.h"

namespace Service::FileSystem {

namespace {

constexpr Result ResultNcaExternalKeyInconsistent{ErrorModule::FS, 6452};

Result ResolveRegisteredNcaPath(FileSystemController& fsc, const FileSys::Sf::Path& path,
                                FileSys::StorageId* out_storage_id,
                                std::shared_ptr<FileSys::NCA>* out_nca) {
    const std::string_view content_path{path.str, FileSys::Sf::Path::GetPathLength(path)};
    FileSys::RegisteredCache* cache{};
    FileSys::StorageId storage_id{FileSys::StorageId::None};

    if (content_path.starts_with("@UserContent://registered/")) {
        cache = fsc.GetUserNANDContents();
        storage_id = FileSys::StorageId::NandUser;
    } else if (content_path.starts_with("@SdCardContent://registered/")) {
        cache = fsc.GetSDMCContents();
        storage_id = FileSys::StorageId::SdCard;
    } else if (content_path.starts_with("@SystemContent://registered/")) {
        cache = fsc.GetSystemNANDContents();
        storage_id = FileSys::StorageId::NandSystem;
    } else {
        R_RETURN(FileSys::ResultPathNotFound);
    }
    R_UNLESS(cache != nullptr, FileSys::ResultTargetNotFound);

    const auto slash = content_path.find_last_of('/');
    R_UNLESS(slash != std::string_view::npos, FileSys::ResultPathNotFound);
    const auto file_name = content_path.substr(slash + 1);
    const bool normal_name = file_name.size() == 36 && file_name.ends_with(".nca");
    const bool meta_name = file_name.size() == 41 && file_name.ends_with(".cnmt.nca");
    R_UNLESS(normal_name || meta_name, FileSys::ResultPathNotFound);

    const auto id_hex = file_name.substr(0, 32);
    const bool valid_hex = std::all_of(id_hex.begin(), id_hex.end(), [](char ch) {
        return (ch >= '0' && ch <= '9') || (ch >= 'a' && ch <= 'f') ||
               (ch >= 'A' && ch <= 'F');
    });
    R_UNLESS(valid_hex, FileSys::ResultPathNotFound);

    const auto nca_id = Common::HexStringToArray<0x10>(id_hex);
    auto raw_nca = cache->GetContentFile(nca_id);
    R_UNLESS(raw_nca != nullptr, FileSys::ResultTargetNotFound);

    auto nca = std::make_shared<FileSys::NCA>(std::move(raw_nca));
    const auto status = nca->GetStatus();
    R_UNLESS(status == Loader::ResultStatus::Success ||
                 status == Loader::ResultStatus::ErrorMissingBKTRBaseRomFS,
             FileSys::ResultTargetNotFound);

    *out_storage_id = storage_id;
    *out_nca = std::move(nca);
    R_SUCCEED();
}

std::string GetCacheStorageDirectoryName(u64 program_id, u16 index) {
    if (index == 0) {
        return fmt::format("{:016X}", program_id);
    }
    return fmt::format("{:016X}_{:04X}", program_id, index);
}

std::string GetCacheStorageRelativePath(u64 program_id, u16 index) {
    return fmt::format("save/cache/{}", GetCacheStorageDirectoryName(program_id, index));
}

std::optional<ISaveDataInfoReader::SaveDataInfo> FindDirectorySaveById(
    Core::System& system, const std::shared_ptr<SaveDataController>& controller,
    FileSys::SaveDataSpaceId space_id, u64 save_data_id) {
    if (save_data_id == 0) {
        return std::nullopt;
    }
    FileSys::SaveDataFilter filter{};
    filter.use_save_data_id = true;
    filter.rank = FileSys::SaveDataRank::Secondary;
    filter.attribute.system_save_data_id = save_data_id;
    const ISaveDataInfoReader reader{system, controller, space_id};
    return reader.FindSaveDataInfo(filter, 0);
}

std::optional<ISaveDataInfoReader::SaveDataInfo> FindDirectorySaveByAttribute(
    Core::System& system, const std::shared_ptr<SaveDataController>& controller,
    FileSys::SaveDataSpaceId space_id, const FileSys::SaveDataAttribute& attribute,
    u64 current_program_id) {
    FileSys::SaveDataFilter filter{};
    filter.attribute = attribute;
    filter.use_save_data_type = true;
    filter.use_user_id = true;
    filter.use_index = true;
    filter.use_save_data_id = attribute.system_save_data_id != 0;
    filter.use_program_id =
        attribute.program_id != 0 || attribute.type == FileSys::SaveDataType::Account ||
        attribute.type == FileSys::SaveDataType::Device ||
        attribute.type == FileSys::SaveDataType::Cache ||
        attribute.type == FileSys::SaveDataType::Temporary ||
        attribute.type == FileSys::SaveDataType::Bcat;
    filter.rank = attribute.rank;

    const ISaveDataInfoReader reader{system, controller, space_id};
    return reader.FindSaveDataInfo(filter, current_program_id);
}

} // namespace

FSP_SRV::FSP_SRV(Core::System& system_)
    : ServiceFramework{system_, "fsp-srv"}, fsc{system.GetFileSystemController()},
      content_provider{system.GetContentProvider()}, reporter{system.GetReporter()} {
    // clang-format off
    static const FunctionInfo functions[] = {
        {0, nullptr, "OpenFileSystem"},
        {1, D<&FSP_SRV::SetCurrentProcess>, "SetCurrentProcess"},
        {2, D<&FSP_SRV::OpenDataFileSystemByCurrentProcess>, "OpenDataFileSystemByCurrentProcess"},
        {7, D<&FSP_SRV::OpenFileSystemWithPatch>, "OpenFileSystemWithPatch"},
        {8, D<&FSP_SRV::OpenFileSystemWithIdObsolete>, "OpenFileSystemWithIdObsolete"},
        {9, D<&FSP_SRV::OpenDataFileSystemByProgramId>, "OpenDataFileSystemByProgramId"},
        {10, D<&FSP_SRV::OpenFileSystemWithId>, "OpenFileSystemWithId"},
        {11, D<&FSP_SRV::OpenBisFileSystem>, "OpenBisFileSystem"},
        {12, D<&FSP_SRV::OpenBisStorage>, "OpenBisStorage"},
        {13, D<&FSP_SRV::InvalidateBisCache>, "InvalidateBisCache"},
        {17, &FSP_SRV::UnsupportedDebugCommand, "OpenHostFileSystem"},
        {18, D<&FSP_SRV::OpenSdCardFileSystem>, "OpenSdCardFileSystem"},
        {19, &FSP_SRV::UnsupportedDebugCommand, "FormatSdCardFileSystem"},
        {21, &FSP_SRV::UnsupportedDebugCommand, "DeleteSaveDataFileSystem"},
        {22, D<&FSP_SRV::CreateSaveDataFileSystem>, "CreateSaveDataFileSystem"},
        {23, D<&FSP_SRV::CreateSaveDataFileSystemBySystemSaveDataId>, "CreateSaveDataFileSystemBySystemSaveDataId"},
        {24, &FSP_SRV::UnsupportedDebugCommand, "RegisterSaveDataFileSystemAtomicDeletion"},
        {25, &FSP_SRV::UnsupportedDebugCommand, "DeleteSaveDataFileSystemBySaveDataSpaceId"},
        {26, &FSP_SRV::UnsupportedDebugCommand, "FormatSdCardDryRun"},
        {27, D<&FSP_SRV::IsExFatSupported>, "IsExFatSupported"},
        {28, &FSP_SRV::UnsupportedDebugCommand, "DeleteSaveDataFileSystemBySaveDataAttribute"},
        {30, D<&FSP_SRV::OpenGameCardStorage>, "OpenGameCardStorage"},
        {31, D<&FSP_SRV::OpenGameCardFileSystem>, "OpenGameCardFileSystem"},
        {32, D<&FSP_SRV::ExtendSaveDataFileSystem>, "ExtendSaveDataFileSystem"},
        {33, D<&FSP_SRV::DeleteCacheStorage>, "DeleteCacheStorage"},
        {34, D<&FSP_SRV::GetCacheStorageSize>, "GetCacheStorageSize"},
        {35, D<&FSP_SRV::CreateSaveDataFileSystemWithHashSalt>, "CreateSaveDataFileSystemWithHashSalt"},
        {36, &FSP_SRV::UnsupportedDebugCommand, "OpenHostFileSystemWithOption"},
        {37, D<&FSP_SRV::CreateSaveDataFileSystemWithCreationInfo2>, "CreateSaveDataFileSystemWithCreationInfo2"},
        {51, D<&FSP_SRV::OpenSaveDataFileSystem>, "OpenSaveDataFileSystem"},
        {52, D<&FSP_SRV::OpenSaveDataFileSystemBySystemSaveDataId>, "OpenSaveDataFileSystemBySystemSaveDataId"},
        {53, D<&FSP_SRV::OpenReadOnlySaveDataFileSystem>, "OpenReadOnlySaveDataFileSystem"},
        {57, D<&FSP_SRV::ReadSaveDataFileSystemExtraDataBySaveDataSpaceId>, "ReadSaveDataFileSystemExtraDataBySaveDataSpaceId"},
        {58, D<&FSP_SRV::ReadSaveDataFileSystemExtraData>, "ReadSaveDataFileSystemExtraData"},
        {59, D<&FSP_SRV::WriteSaveDataFileSystemExtraData>, "WriteSaveDataFileSystemExtraData"},
        {60, D<&FSP_SRV::OpenSaveDataInfoReader>, "OpenSaveDataInfoReader"},
        {61, D<&FSP_SRV::OpenSaveDataInfoReaderBySaveDataSpaceId>, "OpenSaveDataInfoReaderBySaveDataSpaceId"},
        {62, D<&FSP_SRV::OpenSaveDataInfoReaderOnlyCacheStorage>, "OpenSaveDataInfoReaderOnlyCacheStorage"},
        {64, nullptr, "OpenSaveDataInternalStorageFileSystem"},
        {65, &FSP_SRV::UnsupportedDebugCommand, "UpdateSaveDataMacForDebug"},
        {66, D<&FSP_SRV::WriteSaveDataFileSystemExtraDataWithMask>, "WriteSaveDataFileSystemExtraDataWithMask"},
        {67, D<&FSP_SRV::FindSaveDataWithFilter>, "FindSaveDataWithFilter"},
        {68, D<&FSP_SRV::OpenSaveDataInfoReaderWithFilter>, "OpenSaveDataInfoReaderWithFilter"},
        {69, D<&FSP_SRV::ReadSaveDataFileSystemExtraDataBySaveDataAttribute>, "ReadSaveDataFileSystemExtraDataBySaveDataAttribute"},
        {70, D<&FSP_SRV::WriteSaveDataFileSystemExtraDataWithMaskBySaveDataAttribute>, "WriteSaveDataFileSystemExtraDataWithMaskBySaveDataAttribute"},
        {71, D<&FSP_SRV::ReadSaveDataFileSystemExtraDataWithMaskBySaveDataAttribute>, "ReadSaveDataFileSystemExtraDataWithMaskBySaveDataAttribute"},
        {80, nullptr, "OpenSaveDataMetaFile"},
        {81, &FSP_SRV::UnsupportedDebugCommand, "OpenSaveDataTransferManager"},
        {82, &FSP_SRV::UnsupportedDebugCommand, "OpenSaveDataTransferManagerVersion2"},
        {83, D<&FSP_SRV::OpenSaveDataTransferProhibiter>, "OpenSaveDataTransferProhibiter"},
        {84, nullptr, "ListApplicationAccessibleSaveDataOwnerId"},
        {85, &FSP_SRV::UnsupportedDebugCommand, "OpenSaveDataTransferManagerForSaveDataRepair"},
        {86, &FSP_SRV::UnsupportedDebugCommand, "OpenSaveDataMover"},
        {87, &FSP_SRV::UnsupportedDebugCommand, "OpenSaveDataTransferManagerForRepair"},
        {100, D<&FSP_SRV::OpenImageDirectoryFileSystem>, "OpenImageDirectoryFileSystem"},
        {101, nullptr, "OpenBaseFileSystem"},
        {102, nullptr, "FormatBaseFileSystem"},
        {110, D<&FSP_SRV::OpenContentStorageFileSystem>, "OpenContentStorageFileSystem"},
        {120, &FSP_SRV::UnsupportedDebugCommand, "OpenCloudBackupWorkStorageFileSystem"},
        {130, nullptr, "OpenCustomStorageFileSystem"},
        {200, D<&FSP_SRV::OpenDataStorageByCurrentProcess>, "OpenDataStorageByCurrentProcess"},
        {201, D<&FSP_SRV::OpenDataStorageByProgramId>, "OpenDataStorageByProgramId"},
        {202, D<&FSP_SRV::OpenDataStorageByDataId>, "OpenDataStorageByDataId"},
        {203, D<&FSP_SRV::OpenPatchDataStorageByCurrentProcess>, "OpenPatchDataStorageByCurrentProcess"},
        {204, D<&FSP_SRV::OpenDataFileSystemWithProgramIndex>, "OpenDataFileSystemWithProgramIndex"},
        {205, D<&FSP_SRV::OpenDataStorageWithProgramIndex>, "OpenDataStorageWithProgramIndex"},
        {206, nullptr, "OpenDataStorageByPath"},
        {210, D<&FSP_SRV::SetCurrentProcess>, "SetCurrentProcess"},
        {400, &FSP_SRV::UnsupportedDebugCommand, "OpenDeviceOperator"},
        {500, &FSP_SRV::UnsupportedDebugCommand, "OpenSdCardDetectionEventNotifier"},
        {501, &FSP_SRV::UnsupportedDebugCommand, "OpenGameCardDetectionEventNotifier"},
        {510, &FSP_SRV::UnsupportedDebugCommand, "OpenSystemDataUpdateEventNotifier"},
        {511, &FSP_SRV::UnsupportedDebugCommand, "NotifySystemDataUpdateEvent"},
        {520, &FSP_SRV::UnsupportedDebugCommand, "SimulateGameCardDetectionEvent"},
        {600, nullptr, "SetCurrentPosixTime"},
        {601, nullptr, "QuerySaveDataTotalSize"},
        {602, &FSP_SRV::UnsupportedDebugCommand, "VerifySaveDataFileSystem"},
        {603, &FSP_SRV::UnsupportedDebugCommand, "CorruptSaveDataFileSystem"},
        {604, &FSP_SRV::UnsupportedDebugCommand, "CreatePaddingFile"},
        {605, &FSP_SRV::UnsupportedDebugCommand, "DeleteAllPaddingFiles"},
        {606, D<&FSP_SRV::GetRightsId>, "GetRightsId"},
        {607, D<&FSP_SRV::RegisterExternalKey>, "RegisterExternalKey"},
        {608, D<&FSP_SRV::UnregisterAllExternalKey>, "UnregisterAllExternalKey"},
        {609, D<&FSP_SRV::GetRightsIdByPath>, "GetRightsIdByPath"},
        {610, D<&FSP_SRV::GetRightsIdAndKeyGenerationByPath>, "GetRightsIdAndKeyGenerationByPath"},
        {611, nullptr, "SetCurrentPosixTimeWithTimeDifference"},
        {612, D<&FSP_SRV::GetFreeSpaceSizeForSaveData>, "GetFreeSpaceSizeForSaveData"},
        {613, &FSP_SRV::UnsupportedDebugCommand, "VerifySaveDataFileSystemBySaveDataSpaceId"},
        {614, &FSP_SRV::UnsupportedDebugCommand, "CorruptSaveDataFileSystemBySaveDataSpaceId"},
        {615, nullptr, "QuerySaveDataInternalStorageTotalSize"},
        {616, nullptr, "GetSaveDataCommitId"},
        {617, D<&FSP_SRV::UnregisterExternalKey>, "UnregisterExternalKey"},
        {620, &FSP_SRV::UnsupportedDebugCommand, "SetSdCardEncryptionSeed"},
        {630, D<&FSP_SRV::SetSdCardAccessibility>, "SetSdCardAccessibility"},
        {631, D<&FSP_SRV::IsSdCardAccessible>, "IsSdCardAccessible"},
        {640, nullptr, "IsSignedSystemPartitionOnSdCardValid"},
        {700, &FSP_SRV::UnsupportedDebugCommand, "OpenAccessFailureResolver"},
        {701, &FSP_SRV::UnsupportedDebugCommand, "GetAccessFailureDetectionEvent"},
        {702, &FSP_SRV::UnsupportedDebugCommand, "IsAccessFailureDetected"},
        {710, &FSP_SRV::UnsupportedDebugCommand, "ResolveAccessFailure"},
        {720, &FSP_SRV::UnsupportedDebugCommand, "AbandonAccessFailure"},
        {800, &FSP_SRV::UnsupportedDebugCommand, "GetAndClearFileSystemProxyErrorInfo"},
        {810, D<&FSP_SRV::RegisterProgramIndexMapInfo>, "RegisterProgramIndexMapInfo"},
        {820, nullptr, "GetContentStorageInfoIndex"},
        {830, nullptr, "EncryptStreamPlaySaveData"},
        {831, nullptr, "DecryptStreamPlaySaveData"},
        {1000, &FSP_SRV::UnsupportedDebugCommand, "SetBisRootForHost"},
        {1001, &FSP_SRV::UnsupportedDebugCommand, "SetSaveDataSize"},
        {1002, &FSP_SRV::UnsupportedDebugCommand, "SetSaveDataRootPath"},
        {1003, D<&FSP_SRV::DisableAutoSaveDataCreation>, "DisableAutoSaveDataCreation"},
        {1004, D<&FSP_SRV::SetGlobalAccessLogMode>, "SetGlobalAccessLogMode"},
        {1005, D<&FSP_SRV::GetGlobalAccessLogMode>, "GetGlobalAccessLogMode"},
        {1006, D<&FSP_SRV::OutputAccessLogToSdCard>, "OutputAccessLogToSdCard"},
        {1007, &FSP_SRV::UnsupportedDebugCommand, "RegisterUpdatePartition"},
        {1008, &FSP_SRV::UnsupportedDebugCommand, "OpenRegisteredUpdatePartition"},
        {1009, &FSP_SRV::UnsupportedDebugCommand, "GetAndClearMemoryReportInfo"},
        {1010, nullptr, "SetDataStorageRedirectTarget"},
        {1011, D<&FSP_SRV::GetProgramIndexForAccessLog>, "GetProgramIndexForAccessLog"},
        {1012, &FSP_SRV::UnsupportedDebugCommand, "GetFsStackUsage"},
        {1013, &FSP_SRV::UnsupportedDebugCommand, "UnsetSaveDataRootPath"},
        {1014, &FSP_SRV::UnsupportedDebugCommand, "OutputMultiProgramTagAccessLog"},
        {1016, D<&FSP_SRV::FlushAccessLogOnSdCard>, "FlushAccessLogOnSdCard"},
        {1017, &FSP_SRV::UnsupportedDebugCommand, "OutputApplicationInfoAccessLog"},
        {1018, &FSP_SRV::UnsupportedDebugCommand, "SetDebugOption"},
        {1019, &FSP_SRV::UnsupportedDebugCommand, "UnsetDebugOption"},
        {1100, &FSP_SRV::UnsupportedDebugCommand, "OverrideSaveDataTransferTokenSignVerificationKey"},
        {1110, &FSP_SRV::UnsupportedDebugCommand, "CorruptSaveDataFileSystemBySaveDataSpaceId2"},
        {1200, D<&FSP_SRV::OpenMultiCommitManager>, "OpenMultiCommitManager"},
        {1300, &FSP_SRV::UnsupportedDebugCommand, "OpenBisWiper"},
    };
    // clang-format on
    RegisterHandlers(functions);

    if (Settings::values.enable_fs_access_log) {
        access_log_mode = AccessLogMode::SdCard;
    }
}

FSP_SRV::~FSP_SRV() = default;

void FSP_SRV::UnsupportedDebugCommand(HLERequestContext& ctx) {
    LOG_WARNING(Service_FS, "save-data debug mutation is not supported by the production runtime");
    IPC::ResponseBuilder rb{ctx, 2};
    rb.Push(IPC::ResultNotSupported);
}

Result FSP_SRV::SetCurrentProcess(ClientProcessId pid) {
    current_process_id = *pid;

    LOG_DEBUG(Service_FS, "called. current_process_id={:#016x}", current_process_id);

    R_RETURN(
        fsc.OpenProcess(&program_id, &save_data_controller, &romfs_controller, current_process_id));
}

Result FSP_SRV::OpenFileSystemWithPatch(OutInterface<IFileSystem> out_interface,
                                        FileSystemProxyType type, u64 open_program_id) {
    LOG_DEBUG(Service_FS, "called with type={}, program_id={:016X}", type, open_program_id);

    // A missing or unsupported manual RomFS is a guest FS error, not a reason
    // to terminate the entire PS5 emulator process with ASSERT().
    R_UNLESS(type == FileSystemProxyType::Manual, FileSys::ResultNotImplemented);
    R_UNLESS(romfs_controller != nullptr, FileSys::ResultTargetNotFound);

    const auto manual_romfs = romfs_controller->OpenPatchedRomFS(
        open_program_id, FileSys::ContentRecordType::HtmlDocument);
    R_UNLESS(manual_romfs != nullptr, FileSys::ResultTargetNotFound);

    const auto extracted_romfs = FileSys::ExtractRomFS(manual_romfs);
    R_UNLESS(extracted_romfs != nullptr, FileSys::ResultTargetNotFound);

    *out_interface = std::make_shared<IFileSystem>(
        system, extracted_romfs, SizeGetter::FromStorageId(fsc, FileSys::StorageId::NandUser));

    R_SUCCEED();
}

Result FSP_SRV::OpenBisFileSystem(
    OutInterface<IFileSystem> out_interface, FileSys::BisPartitionId partition_id,
    InLargeData<FileSys::Sf::Path, BufferAttr_HipcPointer> path) {
    LOG_DEBUG(Service_FS, "called, partition_id={}, path={}", partition_id, path->str);

    FileSys::VirtualDir bis_dir{};
    R_TRY(fsc.OpenBISPartition(&bis_dir, partition_id));

    const auto storage_id =
        partition_id == FileSys::BisPartitionId::User ||
                partition_id == FileSys::BisPartitionId::UserDataRoot
            ? FileSys::StorageId::NandUser
            : FileSys::StorageId::NandSystem;
    *out_interface =
        std::make_shared<IFileSystem>(system, bis_dir, SizeGetter::FromStorageId(fsc, storage_id));
    R_SUCCEED();
}

Result FSP_SRV::OpenBisStorage(OutInterface<IStorage> out_interface,
                               FileSys::BisPartitionId partition_id) {
    LOG_DEBUG(Service_FS, "called, partition_id={}", partition_id);

    FileSys::VirtualFile bis_storage{};
    R_TRY(fsc.OpenBISPartitionStorage(&bis_storage, partition_id));
    *out_interface = std::make_shared<IStorage>(system, std::move(bis_storage));
    R_SUCCEED();
}

Result FSP_SRV::InvalidateBisCache() {
    // Encore exposes BIS directly through the VFS/BIS factory and does not keep
    // the FS-module MBR/GPT cache that Horizon invalidates with this command.
    LOG_DEBUG(Service_FS, "called; no BIS metadata cache is modeled");
    R_SUCCEED();
}

Result FSP_SRV::GetFreeSpaceSizeForSaveData(Out<s64> out_free_space_size,
                                             FileSys::SaveDataSpaceId space_id) {
    FileSys::StorageId storage_id{};
    switch (space_id) {
    case FileSys::SaveDataSpaceId::System:
    case FileSys::SaveDataSpaceId::ProperSystem:
    case FileSys::SaveDataSpaceId::SafeMode:
        storage_id = FileSys::StorageId::NandSystem;
        break;
    case FileSys::SaveDataSpaceId::User:
    case FileSys::SaveDataSpaceId::Temporary:
        storage_id = FileSys::StorageId::NandUser;
        break;
    case FileSys::SaveDataSpaceId::SdSystem:
    case FileSys::SaveDataSpaceId::SdUser:
        storage_id = FileSys::StorageId::SdCard;
        break;
    default:
        R_RETURN(FileSys::ResultInvalidArgument);
    }

    const u64 free_space = fsc.GetFreeSpaceSize(storage_id);
    R_UNLESS(free_space <= static_cast<u64>((std::numeric_limits<s64>::max)()), ResultUnknown);
    *out_free_space_size = static_cast<s64>(free_space);
    LOG_DEBUG(Service_FS, "called, space_id={}, storage_id={}, free_space={:#x}", space_id,
              storage_id, free_space);
    R_SUCCEED();
}

Result FSP_SRV::OpenSdCardFileSystem(OutInterface<IFileSystem> out_interface) {
    LOG_DEBUG(Service_FS, "called");

    FileSys::VirtualDir sdmc_dir{};
    fsc.OpenSDMC(&sdmc_dir);

    *out_interface = std::make_shared<IFileSystem>(
        system, sdmc_dir, SizeGetter::FromStorageId(fsc, FileSys::StorageId::SdCard));

    R_SUCCEED();
}

Result FSP_SRV::CreateSaveDataFileSystem(FileSys::SaveDataCreationInfo save_create_struct,
                                         FileSys::SaveDataAttribute save_struct, u128 uid) {
    LOG_DEBUG(Service_FS, "called save_struct = {}, uid = {:016X}{:016X}", save_struct.DebugInfo(),
              uid[1], uid[0]);

    FileSys::VirtualDir save_data_dir{};
    R_RETURN(save_data_controller->CreateSaveData(&save_data_dir, FileSys::SaveDataSpaceId::User,
                                                  save_struct));
}

Result FSP_SRV::CreateSaveDataFileSystemBySystemSaveDataId(
    FileSys::SaveDataAttribute save_struct, FileSys::SaveDataCreationInfo save_create_struct) {
    LOG_DEBUG(Service_FS, "called save_struct = {}", save_struct.DebugInfo());

    FileSys::VirtualDir save_data_dir{};
    R_RETURN(save_data_controller->CreateSaveData(&save_data_dir, FileSys::SaveDataSpaceId::System,
                                                  save_struct));
}

Result FSP_SRV::CreateSaveDataFileSystemWithHashSalt(
    FileSys::SaveDataAttribute attribute, FileSys::SaveDataCreationInfo creation_info,
    FileSys::SaveDataMetaInfo meta_info, FileSys::HashSalt hash_salt) {
    LOG_DEBUG(Service_FS,
              "called, attribute={}, space_id={}, size={:#x}, journal={:#x}, meta_size={:#x}",
              attribute.DebugInfo(), creation_info.space_id, creation_info.size,
              creation_info.journal_size, meta_info.size);

    // The host-directory save backend has no Horizon integrity layer. Create the real save at
    // the requested space; hash salt and save-meta allocation remain intentionally unmodeled.
    FileSys::VirtualDir save_data_dir{};
    R_RETURN(save_data_controller->CreateSaveData(&save_data_dir, creation_info.space_id,
                                                  attribute));
}

Result FSP_SRV::CreateSaveDataFileSystemWithCreationInfo2(
    FileSys::SaveDataCreationInfo2 save_data_creation_info) {
    FileSys::VirtualDir save_data_dir{};
    R_RETURN(save_data_controller->CreateSaveData(&save_data_dir, save_data_creation_info.space_id,
                                                  save_data_creation_info.attribute));
}

Result FSP_SRV::IsExFatSupported(Out<bool> out_is_supported) {
    LOG_DEBUG(Service_FS, "called");

    // The virtual host-directory SD backend does not expose a FAT/exFAT
    // formatter distinction. LibHac uses the same conservative fallback.
    *out_is_supported = false;
    R_SUCCEED();
}

Result FSP_SRV::OpenSaveDataFileSystem(OutInterface<IFileSystem> out_interface,
                                       FileSys::SaveDataSpaceId space_id,
                                       FileSys::SaveDataAttribute attribute) {
    LOG_INFO(Service_FS, "called.");

    FileSys::VirtualDir dir{};
    R_TRY(save_data_controller->OpenSaveData(&dir, space_id, attribute));

    FileSys::StorageId id{};
    switch (space_id) {
    case FileSys::SaveDataSpaceId::User:
        id = FileSys::StorageId::NandUser;
        break;
    case FileSys::SaveDataSpaceId::SdSystem:
    case FileSys::SaveDataSpaceId::SdUser:
        id = FileSys::StorageId::SdCard;
        break;
    case FileSys::SaveDataSpaceId::System:
        id = FileSys::StorageId::NandSystem;
        break;
    case FileSys::SaveDataSpaceId::Temporary:
        // ok this is definitely wrong. ASSERT(false) here just kills the whole game the first
        // time it opens cache storage, and plenty of games do that (TOTK for one). there is
        // user-space scratch storage so it belongs on user nand. map it, do not crash.
        id = FileSys::StorageId::NandUser;
        break;
    case FileSys::SaveDataSpaceId::ProperSystem:
    case FileSys::SaveDataSpaceId::SafeMode:
        // same deal for these two. they are system-level spaces so they go on system nand.
        // way better than nuking the title over a save-space id we just did not list out.
        id = FileSys::StorageId::NandSystem;
        break;
    }

    *out_interface =
        std::make_shared<IFileSystem>(system, std::move(dir), SizeGetter::FromStorageId(fsc, id));

    R_SUCCEED();
}

Result FSP_SRV::OpenSaveDataFileSystemBySystemSaveDataId(OutInterface<IFileSystem> out_interface,
                                                         FileSys::SaveDataSpaceId space_id,
                                                         FileSys::SaveDataAttribute attribute) {
    LOG_INFO(Service_FS, "called, space_id={}, {}",
             space_id, attribute.DebugInfo());

    R_UNLESS(attribute.system_save_data_id != FileSys::InvalidSystemSaveDataId,
             FileSys::ResultInvalidArgument);

    if (attribute.program_id == 0) {
        attribute.program_id = program_id;
    }

    FileSys::VirtualDir dir{};
    R_TRY(save_data_controller->OpenSaveData(&dir, space_id, attribute));

    FileSys::StorageId id{};
    switch (space_id) {
    case FileSys::SaveDataSpaceId::User:
        id = FileSys::StorageId::NandUser;
        break;
    case FileSys::SaveDataSpaceId::SdSystem:
    case FileSys::SaveDataSpaceId::SdUser:
        id = FileSys::StorageId::SdCard;
        break;
    case FileSys::SaveDataSpaceId::System:
        id = FileSys::StorageId::NandSystem;
        break;
    case FileSys::SaveDataSpaceId::Temporary:
        // same broken switch as OpenSaveDataFileSystem above. do not ASSERT(false) and kill the
        // game over a save-space id, just map Temporary to user nand like it should be.
        id = FileSys::StorageId::NandUser;
        break;
    case FileSys::SaveDataSpaceId::ProperSystem:
    case FileSys::SaveDataSpaceId::SafeMode:
        // system spaces -> system nand. handled, not crashed.
        id = FileSys::StorageId::NandSystem;
        break;
    }

    *out_interface =
        std::make_shared<IFileSystem>(system, std::move(dir), SizeGetter::FromStorageId(fsc, id));

    R_SUCCEED();
}

Result FSP_SRV::OpenReadOnlySaveDataFileSystem(OutInterface<IFileSystem> out_interface,
                                               FileSys::SaveDataSpaceId space_id,
                                               FileSys::SaveDataAttribute attribute) {
    LOG_INFO(Service_FS, "called, read-only, space_id={}, {}", space_id, attribute.DebugInfo());

    FileSys::VirtualDir dir{};
    R_TRY(save_data_controller->OpenSaveData(&dir, space_id, attribute));

    FileSys::StorageId id{};
    switch (space_id) {
    case FileSys::SaveDataSpaceId::User:
        id = FileSys::StorageId::NandUser;
        break;
    case FileSys::SaveDataSpaceId::SdSystem:
    case FileSys::SaveDataSpaceId::SdUser:
        id = FileSys::StorageId::SdCard;
        break;
    case FileSys::SaveDataSpaceId::System:
    case FileSys::SaveDataSpaceId::ProperSystem:
    case FileSys::SaveDataSpaceId::SafeMode:
        id = FileSys::StorageId::NandSystem;
        break;
    case FileSys::SaveDataSpaceId::Temporary:
        id = FileSys::StorageId::NandUser;
        break;
    }

    *out_interface = std::make_shared<IFileSystem>(
        system, std::move(dir), SizeGetter::FromStorageId(fsc, id), true);
    R_SUCCEED();
}

Result FSP_SRV::OpenSaveDataInfoReader(OutInterface<ISaveDataInfoReader> out_interface) {
    LOG_DEBUG(Service_FS, "called; enumerating all save-data spaces represented by Encore");
    *out_interface = std::make_shared<ISaveDataInfoReader>(system, save_data_controller);
    R_SUCCEED();
}

Result FSP_SRV::OpenSaveDataInfoReaderBySaveDataSpaceId(
    OutInterface<ISaveDataInfoReader> out_interface, FileSys::SaveDataSpaceId space) {
    LOG_INFO(Service_FS, "called, space={}", space);

    *out_interface = std::make_shared<ISaveDataInfoReader>(system, save_data_controller, space);

    R_SUCCEED();
}

Result FSP_SRV::OpenSaveDataInfoReaderOnlyCacheStorage(
    OutInterface<ISaveDataInfoReader> out_interface) {
    LOG_DEBUG(Service_FS, "called, program_id={:016X}", program_id);

    *out_interface = std::make_shared<ISaveDataInfoReader>(
        system, save_data_controller, FileSys::SaveDataSpaceId::User, true, program_id);

    R_SUCCEED();
}

Result FSP_SRV::WriteSaveDataFileSystemExtraDataWithMask(
    InBuffer<BufferAttr_HipcMapAlias> buffer, InBuffer<BufferAttr_HipcMapAlias> mask_buffer,
    FileSys::SaveDataSpaceId space_id, u64 save_data_id) {
    LOG_DEBUG(Service_FS, "called, directory-save semantics, space_id={}, save_data_id={:016X}",
              space_id, save_data_id);
    R_UNLESS(buffer.size() == sizeof(FileSys::SaveDataExtraData) &&
                 mask_buffer.size() == sizeof(FileSys::SaveDataExtraData),
             FileSys::ResultInvalidArgument);
    R_UNLESS(FindDirectorySaveById(system, save_data_controller, space_id, save_data_id).has_value(),
             FileSys::ResultTargetNotFound);

    // Directory-backed saves have no physical ExtraData accessor. After
    // validating both buffers and the real target, LibHac semantics reduce
    // this masked metadata write to a no-op.
    R_SUCCEED();
}

Result FSP_SRV::FindSaveDataWithFilter(Out<s64> out_count,
                                       OutBuffer<BufferAttr_HipcMapAlias> out_buffer,
                                       FileSys::SaveDataSpaceId space_id,
                                       FileSys::SaveDataFilter filter) {
    LOG_DEBUG(Service_FS, "called, space_id={}, {}", space_id, filter.attribute.DebugInfo());
    R_UNLESS(out_buffer.size() == sizeof(ISaveDataInfoReader::SaveDataInfo),
             FileSys::ResultInvalidArgument);

    const auto reader =
        std::make_shared<ISaveDataInfoReader>(system, save_data_controller, space_id);
    const auto match = reader->FindSaveDataInfo(filter, program_id);
    *out_count = match.has_value() ? 1 : 0;
    if (match.has_value()) {
        std::memcpy(out_buffer.data(), &*match, sizeof(*match));
    }
    R_SUCCEED();
}

Result FSP_SRV::OpenSaveDataInfoReaderWithFilter(
    OutInterface<ISaveDataInfoReader> out_interface, FileSys::SaveDataSpaceId space_id,
    FileSys::SaveDataFilter filter) {
    LOG_DEBUG(Service_FS, "called, space_id={}, {}", space_id, filter.attribute.DebugInfo());

    *out_interface = std::make_shared<ISaveDataInfoReader>(
        system, save_data_controller, space_id, filter, program_id);

    R_SUCCEED();
}

Result FSP_SRV::WriteSaveDataFileSystemExtraData(InBuffer<BufferAttr_HipcMapAlias> buffer,
                                                 FileSys::SaveDataSpaceId space_id,
                                                 u64 save_data_id) {
    LOG_DEBUG(Service_FS, "called, directory-save semantics, space_id={}, save_data_id={:016X}",
              space_id, save_data_id);
    R_UNLESS(buffer.size() == sizeof(FileSys::SaveDataExtraData), FileSys::ResultInvalidArgument);
    R_UNLESS(FindDirectorySaveById(system, save_data_controller, space_id, save_data_id).has_value(),
             FileSys::ResultTargetNotFound);

    // Eden stores saves as host directories. LibHac's directory-save backend has
    // no physical ExtraData accessor and treats a validated write as a no-op.
    R_SUCCEED();
}

Result FSP_SRV::WriteSaveDataFileSystemExtraDataWithMaskBySaveDataAttribute(
    InBuffer<BufferAttr_HipcMapAlias> buffer, InBuffer<BufferAttr_HipcMapAlias> mask_buffer,
    FileSys::SaveDataSpaceId space_id, FileSys::SaveDataAttribute attribute) {
    LOG_DEBUG(Service_FS, "called, directory-save semantics, space_id={}, {}", space_id,
              attribute.DebugInfo());
    R_UNLESS(buffer.size() == sizeof(FileSys::SaveDataExtraData) &&
                 mask_buffer.size() == sizeof(FileSys::SaveDataExtraData),
             FileSys::ResultInvalidArgument);
    R_UNLESS(FindDirectorySaveByAttribute(system, save_data_controller, space_id, attribute,
                                          program_id)
                 .has_value(),
             FileSys::ResultTargetNotFound);

    // LibHac's host-directory save implementation has no physical ExtraData
    // accessor; after validating the target, masked writes are intentionally no-op.
    R_SUCCEED();
}

Result FSP_SRV::ReadSaveDataFileSystemExtraDataWithMaskBySaveDataAttribute(
    FileSys::SaveDataSpaceId space_id, FileSys::SaveDataAttribute attribute,
    InBuffer<BufferAttr_HipcMapAlias> mask_buffer, OutBuffer<BufferAttr_HipcMapAlias> out_buffer) {
    LOG_DEBUG(Service_FS, "called, directory-save semantics, space_id={}, {}", space_id,
              attribute.DebugInfo());
    R_UNLESS(mask_buffer.size() == sizeof(FileSys::SaveDataExtraData) &&
                 out_buffer.size() == sizeof(FileSys::SaveDataExtraData),
             FileSys::ResultInvalidArgument);
    R_UNLESS(FindDirectorySaveByAttribute(system, save_data_controller, space_id, attribute,
                                          program_id)
                 .has_value(),
             FileSys::ResultTargetNotFound);

    // Directory saves have default ExtraData in LibHac; masking an all-zero
    // structure remains all zero.
    std::memset(out_buffer.data(), 0, out_buffer.size());
    R_SUCCEED();
}

Result FSP_SRV::ReadSaveDataFileSystemExtraData(OutBuffer<BufferAttr_HipcMapAlias> out_buffer,
                                                u64 save_data_id) {
    LOG_DEBUG(Service_FS, "called, directory-save semantics, save_data_id={:016X}", save_data_id);
    R_UNLESS(out_buffer.size() == sizeof(FileSys::SaveDataExtraData), FileSys::ResultInvalidArgument);

    // LibHac resolves static (signed-negative) IDs from System and ordinary IDs
    // from the User indexer. Eden has no indexer, so resolve against the real
    // directory scan; SdUser is a fallback for non-static saves.
    const bool is_static = (save_data_id & (u64{1} << 63)) != 0;
    auto match = FindDirectorySaveById(system, save_data_controller,
                                       is_static ? FileSys::SaveDataSpaceId::System
                                                 : FileSys::SaveDataSpaceId::User,
                                       save_data_id);
    if (!match.has_value() && !is_static) {
        match = FindDirectorySaveById(system, save_data_controller, FileSys::SaveDataSpaceId::SdUser,
                                      save_data_id);
    }
    R_UNLESS(match.has_value(), FileSys::ResultTargetNotFound);
    std::memset(out_buffer.data(), 0, out_buffer.size());
    R_SUCCEED();
}

Result FSP_SRV::ReadSaveDataFileSystemExtraDataBySaveDataAttribute(
    OutBuffer<BufferAttr_HipcMapAlias> out_buffer, FileSys::SaveDataSpaceId space_id,
    FileSys::SaveDataAttribute attribute) {
    LOG_DEBUG(Service_FS, "called, directory-save semantics, space_id={}, {}", space_id,
              attribute.DebugInfo());
    R_UNLESS(out_buffer.size() == sizeof(FileSys::SaveDataExtraData), FileSys::ResultInvalidArgument);
    R_UNLESS(FindDirectorySaveByAttribute(system, save_data_controller, space_id, attribute,
                                          program_id)
                 .has_value(),
             FileSys::ResultTargetNotFound);

    std::memset(out_buffer.data(), 0, out_buffer.size());
    R_SUCCEED();
}

Result FSP_SRV::ReadSaveDataFileSystemExtraDataBySaveDataSpaceId(
    OutBuffer<BufferAttr_HipcMapAlias> out_buffer, FileSys::SaveDataSpaceId space_id,
    u64 save_data_id) {
    LOG_DEBUG(Service_FS, "called, directory-save semantics, space_id={}, save_data_id={:016X}",
              space_id, save_data_id);
    R_UNLESS(out_buffer.size() == sizeof(FileSys::SaveDataExtraData), FileSys::ResultInvalidArgument);
    R_UNLESS(FindDirectorySaveById(system, save_data_controller, space_id, save_data_id).has_value(),
             FileSys::ResultTargetNotFound);
    std::memset(out_buffer.data(), 0, out_buffer.size());
    R_SUCCEED();
}

Result FSP_SRV::OpenSaveDataTransferProhibiter(
    OutInterface<ISaveDataTransferProhibiter> out_prohibiter, u64 id) {
    LOG_DEBUG(Service_FS,
              "called, id={:016X}; save-data transfer is not implemented, prohibiter is a no-op token",
              id);

    // Encore has no save-data transfer manager. There is therefore no transfer
    // operation for this lifetime token to block; do not add dormant lock state.
    *out_prohibiter = std::make_shared<ISaveDataTransferProhibiter>(system);
    R_SUCCEED();
}

Result FSP_SRV::OpenFileSystemWithIdObsolete(
    OutInterface<IFileSystem> out_interface, FileSystemProxyType type, u64 open_program_id,
    InLargeData<FileSys::Sf::Path, BufferAttr_HipcPointer> path) {
    R_RETURN(OpenFileSystemWithIdImpl(out_interface, ContentAttributes::None, type,
                                     open_program_id, *path));
}

Result FSP_SRV::OpenFileSystemWithId(
    OutInterface<IFileSystem> out_interface, ContentAttributes attributes,
    FileSystemProxyType type, u64 open_program_id,
    InLargeData<FileSys::Sf::Path, BufferAttr_HipcPointer> path) {
    R_RETURN(OpenFileSystemWithIdImpl(out_interface, attributes, type, open_program_id, *path));
}

Result FSP_SRV::OpenFileSystemWithIdImpl(OutInterface<IFileSystem> out_interface,
                                         ContentAttributes attributes,
                                         FileSystemProxyType type, u64 open_program_id,
                                         const FileSys::Sf::Path& path) {
    R_UNLESS(attributes == ContentAttributes::None || attributes == ContentAttributes::All,
             FileSys::ResultInvalidArgument);

    FileSys::StorageId storage_id{FileSys::StorageId::None};
    std::shared_ptr<FileSys::NCA> nca;
    R_TRY(ResolveRegisteredNcaPath(fsc, path, &storage_id, &nca));

    const auto nca_type = nca->GetType();
    if (type != FileSystemProxyType::Meta) {
        R_UNLESS(open_program_id != std::numeric_limits<u64>::max(),
                 FileSys::ResultInvalidArgument);
        R_UNLESS(nca->GetTitleId() == open_program_id, FileSys::ResultInvalidArgument);
    }

    FileSys::VirtualDir mounted_dir{};
    FileSys::VirtualFile romfs{};
    switch (type) {
    case FileSystemProxyType::Code:
        R_UNLESS(nca_type == FileSys::NCAContentType::Program, FileSys::ResultInvalidArgument);
        mounted_dir = nca->GetExeFS();
        break;
    case FileSystemProxyType::Rom:
        R_UNLESS(nca_type == FileSys::NCAContentType::Program, FileSys::ResultInvalidArgument);
        romfs = nca->GetRomFS();
        if (!romfs && romfs_controller) {
            romfs = romfs_controller->OpenPatchedRomFS(open_program_id,
                                                       FileSys::ContentRecordType::Program);
        }
        mounted_dir = romfs ? FileSys::ExtractRomFS(romfs) : nullptr;
        break;
    case FileSystemProxyType::Logo:
        R_UNLESS(nca_type == FileSys::NCAContentType::Program, FileSys::ResultInvalidArgument);
        mounted_dir = nca->GetLogoPartition();
        break;
    case FileSystemProxyType::Control:
        R_UNLESS(nca_type == FileSys::NCAContentType::Control, FileSys::ResultInvalidArgument);
        romfs = nca->GetRomFS();
        if (!romfs && romfs_controller) {
            romfs = romfs_controller->OpenPatchedRomFS(open_program_id,
                                                       FileSys::ContentRecordType::Control);
        }
        mounted_dir = romfs ? FileSys::ExtractRomFS(romfs) : nullptr;
        break;
    case FileSystemProxyType::Manual:
        R_UNLESS(nca_type == FileSys::NCAContentType::Manual, FileSys::ResultInvalidArgument);
        romfs = nca->GetRomFS();
        if (!romfs && romfs_controller) {
            romfs = romfs_controller->OpenPatchedRomFS(
                open_program_id, FileSys::ContentRecordType::HtmlDocument);
        }
        mounted_dir = romfs ? FileSys::ExtractRomFS(romfs) : nullptr;
        break;
    case FileSystemProxyType::Meta:
        R_UNLESS(nca_type == FileSys::NCAContentType::Meta, FileSys::ResultInvalidArgument);
        mounted_dir = nca;
        break;
    case FileSystemProxyType::Data:
        R_UNLESS(nca_type == FileSys::NCAContentType::Data ||
                     nca_type == FileSys::NCAContentType::PublicData,
                 FileSys::ResultInvalidArgument);
        romfs = nca->GetRomFS();
        if (!romfs && romfs_controller) {
            romfs = romfs_controller->OpenPatchedRomFS(open_program_id,
                                                       FileSys::ContentRecordType::Data);
        }
        mounted_dir = romfs ? FileSys::ExtractRomFS(romfs) : nullptr;
        break;
    case FileSystemProxyType::Package:
    case FileSystemProxyType::RegisteredUpdate:
    default:
        R_RETURN(FileSys::ResultNotImplemented);
    }

    R_UNLESS(mounted_dir != nullptr, FileSys::ResultTargetNotFound);
    *out_interface = std::make_shared<IFileSystem>(
        system, std::move(mounted_dir), SizeGetter::FromStorageId(fsc, storage_id));
    R_SUCCEED();
}

Result FSP_SRV::OpenDataFileSystemByProgramId(OutInterface<IFileSystem> out_interface,
                                                   u64 open_program_id) {
    LOG_DEBUG(Service_FS, "called, program_id={:016X}", open_program_id);

    auto patched_romfs =
        romfs_controller->OpenPatchedRomFS(open_program_id, FileSys::ContentRecordType::Program);
    R_UNLESS(patched_romfs != nullptr, FileSys::ResultTargetNotFound);

    auto extracted_romfs = FileSys::ExtractRomFS(patched_romfs);
    R_UNLESS(extracted_romfs != nullptr, FileSys::ResultTargetNotFound);

    *out_interface = std::make_shared<IFileSystem>(
        system, std::move(extracted_romfs),
        SizeGetter::FromStorageId(fsc, FileSys::StorageId::NandUser));
    R_SUCCEED();
}

Result FSP_SRV::OpenDataFileSystemWithProgramIndex(OutInterface<IFileSystem> out_interface,
                                                    u8 program_index) {
    LOG_DEBUG(Service_FS, "called, program_id={:016X}, program_index={}", program_id,
              program_index);

    auto patched_romfs = romfs_controller->OpenPatchedRomFSWithProgramIndex(
        program_id, program_index, FileSys::ContentRecordType::Program);
    R_UNLESS(patched_romfs != nullptr, FileSys::ResultTargetNotFound);

    auto extracted_romfs = FileSys::ExtractRomFS(patched_romfs);
    R_UNLESS(extracted_romfs != nullptr, FileSys::ResultTargetNotFound);

    *out_interface = std::make_shared<IFileSystem>(
        system, std::move(extracted_romfs),
        SizeGetter::FromStorageId(fsc, FileSys::StorageId::NandUser));
    R_SUCCEED();
}

Result FSP_SRV::OpenDataFileSystemByCurrentProcess(OutInterface<IFileSystem> out_interface) {
    LOG_DEBUG(Service_FS, "called");

    if (!romfs) {
        auto current_romfs = romfs_controller->OpenRomFSCurrentProcess();
        if (!current_romfs) {
            LOG_CRITICAL(Service_FS, "No file system interface available!");
            R_RETURN(ResultUnknown);
        }

        romfs = current_romfs;
    }

    auto extracted_romfs = FileSys::ExtractRomFS(romfs);
    if (!extracted_romfs) {
        LOG_CRITICAL(Service_FS, "Failed to extract RomFS for the current process!");
        R_RETURN(ResultUnknown);
    }

    *out_interface = std::make_shared<IFileSystem>(
        system, extracted_romfs, SizeGetter::FromStorageId(fsc, FileSys::StorageId::NandUser));

    R_SUCCEED();
}

Result FSP_SRV::OpenImageDirectoryFileSystem(OutInterface<IFileSystem> out_interface,
                                                   ImageDirectoryId image_directory_id) {
    LOG_DEBUG(Service_FS, "called, image_directory_id={}", image_directory_id);
    auto dir = fsc.GetImageDirectory(image_directory_id);
    R_UNLESS(dir != nullptr, FileSys::ResultInvalidArgument);

    const auto storage_id = image_directory_id == ImageDirectoryId::SdCard
                                ? FileSys::StorageId::SdCard
                                : FileSys::StorageId::NandUser;
    *out_interface =
        std::make_shared<IFileSystem>(system, std::move(dir), SizeGetter::FromStorageId(fsc, storage_id));
    R_SUCCEED();
}

Result FSP_SRV::OpenContentStorageFileSystem(OutInterface<IFileSystem> out_interface,
                                                     ContentStorageId content_storage_id) {
    LOG_DEBUG(Service_FS, "called, content_storage_id={}", content_storage_id);
    auto dir = fsc.GetContentDirectory(content_storage_id);
    R_UNLESS(dir != nullptr, FileSys::ResultInvalidArgument);

    FileSys::StorageId storage_id{};
    switch (content_storage_id) {
    case ContentStorageId::System:
        storage_id = FileSys::StorageId::NandSystem;
        break;
    case ContentStorageId::User:
        storage_id = FileSys::StorageId::NandUser;
        break;
    case ContentStorageId::SdCard:
        storage_id = FileSys::StorageId::SdCard;
        break;
    default:
        R_RETURN(FileSys::ResultInvalidArgument);
    }
    *out_interface =
        std::make_shared<IFileSystem>(system, std::move(dir), SizeGetter::FromStorageId(fsc, storage_id));
    R_SUCCEED();
}

Result FSP_SRV::OpenDataStorageByCurrentProcess(OutInterface<IStorage> out_interface) {
    LOG_DEBUG(Service_FS, "called");

    if (!romfs) {
        auto current_romfs = romfs_controller->OpenRomFSCurrentProcess();
        if (!current_romfs) {
            // TODO (bunnei): Find the right error code to use here
            LOG_CRITICAL(Service_FS, "No file system interface available!");
            R_RETURN(ResultUnknown);
        }

        romfs = current_romfs;
    }

    *out_interface = std::make_shared<IStorage>(system, romfs);

    R_SUCCEED();
}

Result FSP_SRV::OpenDataStorageByProgramId(OutInterface<IStorage> out_interface,
                                             FileSys::StorageId storage_id, u64 title_id) {
    LOG_DEBUG(Service_FS, "called with storage_id={:02X}, title_id={:016X}", storage_id, title_id);
    R_RETURN(OpenDataStorageByDataId(out_interface, storage_id, 0, title_id));
}

Result FSP_SRV::OpenDataStorageByDataId(OutInterface<IStorage> out_interface,
                                        FileSys::StorageId storage_id, u32 unknown, u64 title_id) {
    LOG_DEBUG(Service_FS, "called with storage_id={:02X}, unknown={:08X}, title_id={:016X}",
              storage_id, unknown, title_id);

    auto data = romfs_controller->OpenRomFS(title_id, storage_id, FileSys::ContentRecordType::Data);

    if (!data) {
        const auto archive = FileSys::SystemArchive::SynthesizeSystemArchive(title_id);

        if (archive != nullptr) {
            *out_interface = std::make_shared<IStorage>(system, archive);
            R_SUCCEED();
        }

        // TODO(DarkLordZach): Find the right error code to use here
        LOG_ERROR(Service_FS,
                  "Could not open data storage with title_id={:016X}, storage_id={:02X}", title_id,
                  storage_id);
        R_RETURN(ResultUnknown);
    }

    const FileSys::PatchManager pm{title_id, fsc, content_provider};

    auto base =
        romfs_controller->OpenBaseNca(title_id, storage_id, FileSys::ContentRecordType::Data);
    auto storage = std::make_shared<IStorage>(
        system, pm.PatchRomFS(base.get(), std::move(data), FileSys::ContentRecordType::Data));

    *out_interface = std::move(storage);
    R_SUCCEED();
}

Result FSP_SRV::OpenPatchDataStorageByCurrentProcess(OutInterface<IStorage> out_interface) {
    LOG_DEBUG(Service_FS, "called");

    auto patched_romfs = romfs_controller->OpenRomFSCurrentProcess();
    R_UNLESS(patched_romfs != nullptr, FileSys::ResultTargetNotFound);

    *out_interface = std::make_shared<IStorage>(system, std::move(patched_romfs));
    R_SUCCEED();
}

Result FSP_SRV::OpenDataStorageWithProgramIndex(OutInterface<IStorage> out_interface,
                                                u8 program_index) {
    LOG_DEBUG(Service_FS, "called, program_index={}", program_index);

    auto patched_romfs = romfs_controller->OpenPatchedRomFSWithProgramIndex(
        program_id, program_index, FileSys::ContentRecordType::Program);

    if (!patched_romfs) {
        // TODO: Find the right error code to use here
        LOG_ERROR(Service_FS, "Could not open storage with program_index={}", program_index);
        R_RETURN(ResultUnknown);
    }

    *out_interface = std::make_shared<IStorage>(system, std::move(patched_romfs));

    R_SUCCEED();
}

Result FSP_SRV::GetRightsIdByPath(
    Out<FspRightsId> out_rights_id,
    InLargeData<FileSys::Sf::Path, BufferAttr_HipcPointer> path) {
    FileSys::StorageId storage_id{};
    std::shared_ptr<FileSys::NCA> nca;
    R_TRY(ResolveRegisteredNcaPath(fsc, *path, &storage_id, &nca));
    *out_rights_id = nca->GetRightsId();
    R_SUCCEED();
}

Result FSP_SRV::GetRightsIdAndKeyGenerationByPath(
    Out<RightsIdAndKeyGeneration> out_result, ContentAttributes attributes,
    InLargeData<FileSys::Sf::Path, BufferAttr_HipcPointer> path) {
    R_UNLESS(attributes == ContentAttributes::None || attributes == ContentAttributes::All,
             FileSys::ResultInvalidArgument);

    FileSys::StorageId storage_id{};
    std::shared_ptr<FileSys::NCA> nca;
    R_TRY(ResolveRegisteredNcaPath(fsc, *path, &storage_id, &nca));

    out_result->key_generation = nca->GetKeyGeneration();
    out_result->rights_id = nca->GetRightsId();
    R_SUCCEED();
}

Result FSP_SRV::GetRightsId(Out<FspRightsId> out_rights_id,
                               FileSys::StorageId storage_id, u64 program_id_) {
    LOG_DEBUG(Service_FS, "called, storage_id={:02X}, program_id={:016X}", storage_id, program_id_);

    const auto nca =
        romfs_controller->OpenBaseNca(program_id_, storage_id, FileSys::ContentRecordType::Program);
    R_UNLESS(nca != nullptr, FileSys::ResultTargetNotFound);

    *out_rights_id = nca->GetRightsId();
    R_SUCCEED();
}

Result FSP_SRV::RegisterExternalKey(FspRightsId rights_id, FspAccessKey access_key) {
    LOG_DEBUG(Service_FS, "called");

    const bool registered =
        Core::Crypto::KeyManager::Instance().RegisterExternalTitleKey(rights_id, access_key);
    R_UNLESS(registered, ResultNcaExternalKeyInconsistent);
    R_SUCCEED();
}

Result FSP_SRV::UnregisterAllExternalKey() {
    LOG_DEBUG(Service_FS, "called");

    Core::Crypto::KeyManager::Instance().ClearExternalTitleKeys();
    R_SUCCEED();
}

Result FSP_SRV::UnregisterExternalKey(FspRightsId rights_id) {
    LOG_DEBUG(Service_FS, "called");

    // Horizon treats unregister as idempotent for callers. Removing a missing
    // transient key therefore leaves persistent title.keys untouched.
    Core::Crypto::KeyManager::Instance().UnregisterExternalTitleKey(rights_id);
    R_SUCCEED();
}

Result FSP_SRV::SetSdCardAccessibility(bool is_accessible) {
    LOG_DEBUG(Service_FS, "called, is_accessible={}", is_accessible);
    sd_card_accessible = is_accessible;
    R_SUCCEED();
}

Result FSP_SRV::IsSdCardAccessible(Out<bool> out_is_accessible) {
    LOG_DEBUG(Service_FS, "called");

    FileSys::VirtualDir sdmc{};
    *out_is_accessible =
        sd_card_accessible && fsc.OpenSDMC(&sdmc) == ResultSuccess && sdmc != nullptr;
    R_SUCCEED();
}

Result FSP_SRV::DisableAutoSaveDataCreation() {
    LOG_DEBUG(Service_FS, "called");

    save_data_controller->SetAutoCreate(false);

    R_SUCCEED();
}

Result FSP_SRV::SetGlobalAccessLogMode(AccessLogMode access_log_mode_) {
    LOG_DEBUG(Service_FS, "called, access_log_mode={}", access_log_mode_);

    access_log_mode = access_log_mode_;

    R_SUCCEED();
}

Result FSP_SRV::GetGlobalAccessLogMode(Out<AccessLogMode> out_access_log_mode) {
    LOG_DEBUG(Service_FS, "called");

    *out_access_log_mode = access_log_mode;

    R_SUCCEED();
}

Result FSP_SRV::OutputAccessLogToSdCard(InBuffer<BufferAttr_HipcMapAlias> log_message_buffer) {
    LOG_DEBUG(Service_FS, "called");

    auto log = Common::StringFromFixedZeroTerminatedBuffer(
        reinterpret_cast<const char*>(log_message_buffer.data()), log_message_buffer.size());
    reporter.SaveFSAccessLog(log);

    R_SUCCEED();
}

Result FSP_SRV::RegisterProgramIndexMapInfo(
    InBuffer<BufferAttr_HipcMapAlias> map_info_buffer, s32 program_count) {
    LOG_DEBUG(Service_FS, "called, program_count={}, buffer_size={}", program_count,
              map_info_buffer.size());

    R_UNLESS(program_count >= 0, FileSys::ResultInvalidArgument);
    if (program_count == 0) {
        R_SUCCEED();
    }

    const auto count = static_cast<std::size_t>(program_count);
    R_UNLESS(count <= map_info_buffer.size() / sizeof(ProgramIndexMapInfo),
             FileSys::ResultInvalidSize);

    program_index_map.resize(count);
    std::memcpy(program_index_map.data(), map_info_buffer.data(),
                count * sizeof(ProgramIndexMapInfo));
    R_SUCCEED();
}

Result FSP_SRV::GetProgramIndexForAccessLog(Out<u32> out_program_index,
                                            Out<u32> out_program_count) {
    LOG_DEBUG(Service_FS, "called, program_id={:016X}, registered_programs={}", program_id,
              program_index_map.size());

    u32 index = 0;
    if (const auto it =
            std::find_if(program_index_map.begin(), program_index_map.end(),
                         [this](const ProgramIndexMapInfo& info) {
                             return info.program_id == program_id;
                         });
        it != program_index_map.end()) {
        index = it->program_index;
    }

    R_UNLESS(program_index_map.size() <=
                 static_cast<std::size_t>((std::numeric_limits<u32>::max)()),
             ResultUnknown);
    *out_program_index = index;
    *out_program_count = static_cast<u32>(program_index_map.size());
    R_SUCCEED();
}

Result FSP_SRV::FlushAccessLogOnSdCard() {
    LOG_DEBUG(Service_FS, "called; no buffered SD access-log sink is modeled");
    R_SUCCEED();
}

Result FSP_SRV::OpenGameCardStorage(OutInterface<IStorage> out_interface, u32 handle,
                                        u32 partition_id) {
    LOG_DEBUG(Service_FS, "called, handle={:#x}, raw_partition={}", handle, partition_id);

    auto* gamecard = fsc.GetGameCard();
    R_UNLESS(gamecard != nullptr, FileSys::ResultInvalidArgument);

    FileSys::VirtualFile storage{};
    switch (partition_id) {
    case 0: // GameCardPartitionRaw::NormalReadable
        storage = gamecard->GetStoragePartition0();
        break;
    case 1: // GameCardPartitionRaw::SecureReadable
        storage = gamecard->GetStoragePartition1();
        break;
    case 2: // GameCardPartitionRaw::RootWriteable
        // XCI is mounted read-only by Encore; never expose a fake writable root.
        R_RETURN(FileSys::ResultInvalidArgument);
    default:
        R_RETURN(FileSys::ResultInvalidArgument);
    }

    R_UNLESS(storage != nullptr, FileSys::ResultInvalidArgument);
    *out_interface = std::make_shared<IStorage>(system, std::move(storage));
    R_SUCCEED();
}

Result FSP_SRV::OpenGameCardFileSystem(OutInterface<IFileSystem> out_interface, u32 handle,
                                           u32 partition_id) {
    LOG_DEBUG(Service_FS, "called, handle={:#x}, partition={}", handle, partition_id);

    auto* gamecard = fsc.GetGameCard();
    R_UNLESS(gamecard != nullptr, FileSys::ResultInvalidArgument);
    R_UNLESS(partition_id <= static_cast<u32>(FileSys::XCIPartition::Logo),
             FileSys::ResultInvalidArgument);

    auto partition = gamecard->GetPartition(static_cast<FileSys::XCIPartition>(partition_id));
    R_UNLESS(partition != nullptr, FileSys::ResultInvalidArgument);

    *out_interface = std::make_shared<IFileSystem>(
        system, std::move(partition), SizeGetter::FromStorageId(fsc, FileSys::StorageId::GameCard));
    R_SUCCEED();
}

Result FSP_SRV::ExtendSaveDataFileSystem(FileSys::SaveDataSpaceId space_id, u64 save_data_id,
                                         s64 available_size, s64 journal_size) {
    // No save-data-id index exists in Encore. Never claim an extension that was not applied.
    LOG_WARNING(Service_FS,
                "unresolved save id, space_id={}, save_data_id={:016X}, available_size={:#x}, "
                "journal_size={:#x}",
                space_id, save_data_id, available_size, journal_size);
    R_RETURN(FileSys::ResultTargetNotFound);
}

Result FSP_SRV::DeleteCacheStorage(u16 index) {
    LOG_DEBUG(Service_FS, "called with index={}, program_id={:016X}", index, program_id);

    FileSys::VirtualDir save_root{};
    R_TRY(save_data_controller->OpenSaveDataSpace(&save_root, FileSys::SaveDataSpaceId::User));

    const auto cache_root = save_root->GetDirectoryRelative("save/cache");
    R_UNLESS(cache_root != nullptr, FileSys::ResultTargetNotFound);

    const auto cache_name = GetCacheStorageDirectoryName(program_id, index);
    R_UNLESS(cache_root->GetSubdirectory(cache_name) != nullptr, FileSys::ResultTargetNotFound);
    R_UNLESS(cache_root->DeleteSubdirectoryRecursive(cache_name), ResultUnknown);

    R_SUCCEED();
}

Result FSP_SRV::GetCacheStorageSize(s32 index, Out<s64> out_data_size,
                                    Out<s64> out_journal_size) {
    LOG_DEBUG(Service_FS, "called with index={}, program_id={:016X}", index, program_id);

    R_UNLESS(index >= 0 && static_cast<u64>(index) <= (std::numeric_limits<u16>::max)(),
             FileSys::ResultTargetNotFound);

    FileSys::VirtualDir save_root{};
    R_TRY(save_data_controller->OpenSaveDataSpace(&save_root, FileSys::SaveDataSpaceId::User));
    const auto cache_dir = save_root->GetDirectoryRelative(
        GetCacheStorageRelativePath(program_id, static_cast<u16>(index)));
    R_UNLESS(cache_dir != nullptr, FileSys::ResultTargetNotFound);

    const auto size_file = cache_dir->GetFile(FileSys::GetSaveDataSizeFileName());
    R_UNLESS(size_file != nullptr && size_file->GetSize() >= sizeof(FileSys::SaveDataSize),
             FileSys::ResultTargetNotFound);

    FileSys::SaveDataSize size{};
    R_UNLESS(size_file->ReadObject(&size) == sizeof(size), ResultUnknown);
    R_UNLESS(size.normal <= static_cast<u64>((std::numeric_limits<s64>::max)()) &&
                 size.journal <= static_cast<u64>((std::numeric_limits<s64>::max)()),
             ResultUnknown);
    *out_data_size = static_cast<s64>(size.normal);
    *out_journal_size = static_cast<s64>(size.journal);
    R_SUCCEED();
}

Result FSP_SRV::OpenMultiCommitManager(OutInterface<IMultiCommitManager> out_interface) {
    LOG_DEBUG(Service_FS, "called");

    *out_interface = std::make_shared<IMultiCommitManager>(system);

    R_SUCCEED();
}

} // namespace Service::FileSystem
