// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

// SPDX-FileCopyrightText: Copyright 2018 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <algorithm>
#include <array>
#include <cstring>
#include <limits>
#include <memory>
#include <optional>
#include <vector>

#include "core/core.h"
#include "core/file_sys/registered_cache.h"
#include "core/file_sys/romfs_factory.h"
#include "core/hle/api_version.h"
#include "core/hle/service/filesystem/filesystem.h"
#include "core/hle/service/ipc_helpers.h"
#include "core/hle/service/ncm/ncm.h"
#include "core/hle/service/server_manager.h"
#include "core/hle/service/service.h"

namespace Service::NCM {

class ILocationResolver final : public ServiceFramework<ILocationResolver> {
public:
    explicit ILocationResolver(Core::System& system_, FileSys::StorageId id)
        : ServiceFramework{system_, "ILocationResolver"}, storage{id} {
        // clang-format off
        static const FunctionInfo functions[] = {
            {0, nullptr, "ResolveProgramPath"},
            {1, nullptr, "RedirectProgramPath"},
            {2, nullptr, "ResolveApplicationControlPath"},
            {3, nullptr, "ResolveApplicationHtmlDocumentPath"},
            {4, nullptr, "ResolveDataPath"},
            {5, nullptr, "RedirectApplicationControlPath"},
            {6, nullptr, "RedirectApplicationHtmlDocumentPath"},
            {7, nullptr, "ResolveApplicationLegalInformationPath"},
            {8, nullptr, "RedirectApplicationLegalInformationPath"},
            {9, nullptr, "Refresh"},
            {10, nullptr, "RedirectApplicationProgramPath"},
            {11, nullptr, "ClearApplicationRedirection"},
            {12, nullptr, "EraseProgramRedirection"},
            {13, nullptr, "EraseApplicationControlRedirection"},
            {14, nullptr, "EraseApplicationHtmlDocumentRedirection"},
            {15, nullptr, "EraseApplicationLegalInformationRedirection"},
            {16, nullptr, "ResolveProgramPathForDebug"},
            {17, nullptr, "RedirectProgramPathForDebug"},
            {18, nullptr, "RedirectApplicationProgramPathForDebug"},
            {19, nullptr, "EraseProgramRedirectionForDebug"},
        };
        // clang-format on

        RegisterHandlers(functions);
    }

private:
    [[maybe_unused]] FileSys::StorageId storage;
};

class IRegisteredLocationResolver final : public ServiceFramework<IRegisteredLocationResolver> {
public:
    explicit IRegisteredLocationResolver(Core::System& system_)
        : ServiceFramework{system_, "IRegisteredLocationResolver"} {
        // clang-format off
        static const FunctionInfo functions[] = {
            {0, nullptr, "ResolveProgramPath"},
            {1, nullptr, "RegisterProgramPath"},
            {2, nullptr, "UnregisterProgramPath"},
            {3, nullptr, "RedirectProgramPath"},
            {4, nullptr, "ResolveHtmlDocumentPath"},
            {5, nullptr, "RegisterHtmlDocumentPath"},
            {6, nullptr, "UnregisterHtmlDocumentPath"},
            {7, nullptr, "RedirectHtmlDocumentPath"},
            {8, nullptr, "Refresh"},
            {9, nullptr, "RefreshExcluding"},
        };
        // clang-format on

        RegisterHandlers(functions);
    }
};

class IAddOnContentLocationResolver final : public ServiceFramework<IAddOnContentLocationResolver> {
public:
    explicit IAddOnContentLocationResolver(Core::System& system_)
        : ServiceFramework{system_, "IAddOnContentLocationResolver"} {
        // clang-format off
        static const FunctionInfo functions[] = {
            {0, nullptr, "ResolveAddOnContentPath"},
            {1, nullptr, "RegisterAddOnContentStorage"},
            {2, nullptr, "UnregisterAllAddOnContentPath"},
            {3, nullptr, "RefreshApplicationAddOnContent"},
            {4, nullptr, "UnregisterApplicationAddOnContent"},
        };
        // clang-format on

        RegisterHandlers(functions);
    }
};

class IContentStorage final : public ServiceFramework<IContentStorage> {
public:
    explicit IContentStorage(Core::System& system_, FileSys::StorageId id)
        : ServiceFramework{system_, "IContentStorage"}, storage{id} {
        // clang-format off
        static const FunctionInfo functions[] = {
            {0, &IContentStorage::GeneratePlaceHolderId, "GeneratePlaceHolderId"},
            {1, &IContentStorage::CreatePlaceHolder, "CreatePlaceHolder"},
            {2, &IContentStorage::DeletePlaceHolder, "DeletePlaceHolder"},
            {3, &IContentStorage::HasPlaceHolder, "HasPlaceHolder"},
            {4, &IContentStorage::WritePlaceHolder, "WritePlaceHolder"},
            {5, &IContentStorage::Register, "Register"},
            {6, &IContentStorage::Delete, "Delete"},
            {7, &IContentStorage::Has, "Has"},
            {8, &IContentStorage::GetPath, "GetPath"},
            {9, &IContentStorage::GetPlaceHolderPath, "GetPlaceHolderPath"},
            {10, &IContentStorage::CleanupAllPlaceHolder, "CleanupAllPlaceHolder"},
            {11, &IContentStorage::ListPlaceHolder, "ListPlaceHolder"},
            {12, &IContentStorage::GetContentCount, "GetContentCount"},
            {13, &IContentStorage::ListContentId, "ListContentId"},
            {14, &IContentStorage::GetSizeFromContentId, "GetSizeFromContentId"},
            {15, &IContentStorage::DisableForcibly, "DisableForcibly"},
            {16, &IContentStorage::RevertToPlaceHolder, "RevertToPlaceHolder"},
            {17, &IContentStorage::SetPlaceHolderSize, "SetPlaceHolderSize"},
            {18, &IContentStorage::ReadContentIdFile, "ReadContentIdFile"},
            {19, &IContentStorage::GetRightsIdFromPlaceHolderId, "GetRightsIdFromPlaceHolderId"},
            {20, &IContentStorage::GetRightsIdFromContentId, "GetRightsIdFromContentId"},
            {21, &IContentStorage::WriteContentForDebug, "WriteContentForDebug"},
            {22, &IContentStorage::GetFreeSpaceSize, "GetFreeSpaceSize"},
            {23, &IContentStorage::GetTotalSpaceSize, "GetTotalSpaceSize"},
            {24, &IContentStorage::FlushPlaceHolder, "FlushPlaceHolder"},
            {25, &IContentStorage::GetSizeFromPlaceHolderId, "GetSizeFromPlaceHolderId"},
            {26, &IContentStorage::RepairInvalidFileAttribute, "RepairInvalidFileAttribute"},
            {27, &IContentStorage::GetRightsIdFromPlaceHolderIdWithCache, "GetRightsIdFromPlaceHolderIdWithCache"},
        };
        // clang-format on

        RegisterHandlers(functions);
    }

private:
    void GeneratePlaceHolderId(HLERequestContext& ctx) {
        LOG_DEBUG(Service_NCM, "called");

        IPC::ResponseBuilder rb{ctx, 6};
        rb.Push(ResultSuccess);
        rb.PushRaw(FileSys::PlaceholderCache::Generate());
    }

    void CreatePlaceHolder(HLERequestContext& ctx) {
        IPC::RequestParser rp{ctx};
        [[maybe_unused]] FileSys::NcaID content_id{};
        FileSys::NcaID placeholder_id{};
        if constexpr (HLE::ApiVersion::HOS_VERSION_MAJOR >= 16) {
            placeholder_id = rp.PopRaw<FileSys::NcaID>();
            content_id = rp.PopRaw<FileSys::NcaID>();
        } else {
            content_id = rp.PopRaw<FileSys::NcaID>();
            placeholder_id = rp.PopRaw<FileSys::NcaID>();
        }
        const auto size = rp.Pop<s64>();

        auto* const placeholder_cache =
            system.GetFileSystemController().GetPlaceholderCacheForStorage(storage);
        const bool succeeded =
            placeholder_cache != nullptr && size >= 0 &&
            (placeholder_cache->Exists(placeholder_id) ||
             placeholder_cache->Create(placeholder_id, static_cast<u64>(size)));

        if (succeeded) {
            LOG_DEBUG(Service_NCM, "called, storage_id={}, size={}", static_cast<u32>(storage),
                      size);
        } else {
            LOG_WARNING(Service_NCM, "failed, storage_id={}, size={}", static_cast<u32>(storage),
                        size);
        }

        IPC::ResponseBuilder rb{ctx, 2};
        rb.Push(succeeded ? ResultSuccess : ResultUnknown);
    }

    void DeletePlaceHolder(HLERequestContext& ctx) {
        IPC::RequestParser rp{ctx};
        const auto placeholder_id = rp.PopRaw<FileSys::NcaID>();

        auto* const placeholder_cache =
            system.GetFileSystemController().GetPlaceholderCacheForStorage(storage);
        const bool succeeded =
            placeholder_cache != nullptr &&
            (!placeholder_cache->Exists(placeholder_id) || placeholder_cache->Delete(placeholder_id));

        if (succeeded) {
            LOG_DEBUG(Service_NCM, "called, storage_id={}", static_cast<u32>(storage));
        } else {
            LOG_WARNING(Service_NCM, "failed, storage_id={}", static_cast<u32>(storage));
        }

        IPC::ResponseBuilder rb{ctx, 2};
        rb.Push(succeeded ? ResultSuccess : ResultUnknown);
    }

    void HasPlaceHolder(HLERequestContext& ctx) {
        IPC::RequestParser rp{ctx};
        const auto placeholder_id = rp.PopRaw<FileSys::NcaID>();
        auto* const placeholder_cache =
            system.GetFileSystemController().GetPlaceholderCacheForStorage(storage);

        IPC::ResponseBuilder rb{ctx, 3};
        rb.Push(placeholder_cache != nullptr ? ResultSuccess : ResultUnknown);
        rb.Push(placeholder_cache != nullptr && placeholder_cache->Exists(placeholder_id));
    }

    void WritePlaceHolder(HLERequestContext& ctx) {
        IPC::RequestParser rp{ctx};
        const auto placeholder_id = rp.PopRaw<FileSys::NcaID>();
        const auto offset = rp.Pop<u64>();
        const auto data = ctx.ReadBuffer();

        auto* const placeholder_cache =
            system.GetFileSystemController().GetPlaceholderCacheForStorage(storage);
        const std::vector<u8> write_data{data.begin(), data.end()};
        const bool succeeded =
            placeholder_cache != nullptr &&
            placeholder_cache->Write(placeholder_id, offset, write_data);

        if (succeeded) {
            LOG_DEBUG(Service_NCM, "called, storage_id={}, offset={}, size={}",
                      static_cast<u32>(storage), offset, data.size());
        } else {
            LOG_WARNING(Service_NCM, "failed, storage_id={}, offset={}, size={}",
                        static_cast<u32>(storage), offset, data.size());
        }

        IPC::ResponseBuilder rb{ctx, 2};
        rb.Push(succeeded ? ResultSuccess : ResultUnknown);
    }

    void Register(HLERequestContext& ctx) {
        IPC::RequestParser rp{ctx};
        FileSys::NcaID content_id{};
        FileSys::NcaID placeholder_id{};
        if constexpr (HLE::ApiVersion::HOS_VERSION_MAJOR >= 16) {
            placeholder_id = rp.PopRaw<FileSys::NcaID>();
            content_id = rp.PopRaw<FileSys::NcaID>();
        } else {
            content_id = rp.PopRaw<FileSys::NcaID>();
            placeholder_id = rp.PopRaw<FileSys::NcaID>();
        }

        auto& fsc = system.GetFileSystemController();
        auto* const placeholder_cache = fsc.GetPlaceholderCacheForStorage(storage);
        auto* const registered_cache = fsc.GetRegisteredCacheForStorage(storage);
        const bool succeeded =
            placeholder_cache != nullptr && registered_cache != nullptr &&
            placeholder_cache->Register(registered_cache, placeholder_id, content_id);

        if (succeeded) {
            LOG_DEBUG(Service_NCM, "called, storage_id={}", static_cast<u32>(storage));
        } else {
            LOG_WARNING(Service_NCM, "failed, storage_id={}", static_cast<u32>(storage));
        }

        IPC::ResponseBuilder rb{ctx, 2};
        rb.Push(succeeded ? ResultSuccess : ResultUnknown);
    }

    void Delete(HLERequestContext& ctx) {
        IPC::RequestParser rp{ctx};
        const auto content_id = rp.PopRaw<FileSys::NcaID>();

        auto* const registered_cache =
            system.GetFileSystemController().GetRegisteredCacheForStorage(storage);
        const bool succeeded = registered_cache != nullptr && registered_cache->Delete(content_id);
        if (succeeded) {
            registered_cache->Refresh();
        }

        if (succeeded) {
            LOG_DEBUG(Service_NCM, "called, storage_id={}", static_cast<u32>(storage));
        } else {
            LOG_WARNING(Service_NCM, "failed, storage_id={}", static_cast<u32>(storage));
        }

        IPC::ResponseBuilder rb{ctx, 2};
        rb.Push(succeeded ? ResultSuccess : ResultUnknown);
    }

    struct NcmRightsId {
        FileSys::NcaID rights_id{};
        u8 key_generation{};
        std::array<u8, 7> padding{};
    };
    static_assert(sizeof(NcmRightsId) == 0x18);

    void Has(HLERequestContext& ctx) {
        IPC::RequestParser rp{ctx};
        const auto content_id = rp.PopRaw<FileSys::NcaID>();
        auto* const registered_cache =
            system.GetFileSystemController().GetRegisteredCacheForStorage(storage);

        IPC::ResponseBuilder rb{ctx, 3};
        rb.Push(registered_cache != nullptr ? ResultSuccess : ResultUnknown);
        rb.Push(registered_cache != nullptr && registered_cache->Exists(content_id));
    }

    void GetContentCount(HLERequestContext& ctx) {
        auto* const registered_cache =
            system.GetFileSystemController().GetRegisteredCacheForStorage(storage);
        if (registered_cache == nullptr) {
            IPC::ResponseBuilder rb{ctx, 2};
            rb.Push(ResultUnknown);
            return;
        }
        const auto ids = registered_cache->ListContentIds();
        const auto count = std::min<std::size_t>(
            ids.size(), static_cast<std::size_t>((std::numeric_limits<s32>::max)()));
        IPC::ResponseBuilder rb{ctx, 3};
        rb.Push(ResultSuccess);
        rb.Push(static_cast<s32>(count));
    }

    void ListContentId(HLERequestContext& ctx) {
        IPC::RequestParser rp{ctx};
        const auto start_offset = rp.Pop<s32>();
        auto* const registered_cache =
            system.GetFileSystemController().GetRegisteredCacheForStorage(storage);
        if (registered_cache == nullptr || start_offset < 0) {
            IPC::ResponseBuilder rb{ctx, 2};
            rb.Push(ResultUnknown);
            return;
        }

        const auto ids = registered_cache->ListContentIds();
        const auto start = static_cast<std::size_t>(start_offset);
        const auto available = start < ids.size() ? ids.size() - start : 0;
        const auto max_entries = ctx.GetWriteBufferSize() / sizeof(FileSys::NcaID);
        const auto copied = std::min(available, max_entries);
        if (copied != 0) {
            ctx.WriteBuffer(ids.data() + start, copied * sizeof(FileSys::NcaID));
        }

        IPC::ResponseBuilder rb{ctx, 3};
        rb.Push(ResultSuccess);
        rb.Push(static_cast<s32>(std::min<std::size_t>(
            copied, static_cast<std::size_t>((std::numeric_limits<s32>::max)()))));
    }

    void GetSizeFromContentId(HLERequestContext& ctx) {
        IPC::RequestParser rp{ctx};
        const auto content_id = rp.PopRaw<FileSys::NcaID>();
        auto* const registered_cache =
            system.GetFileSystemController().GetRegisteredCacheForStorage(storage);
        const auto size = registered_cache != nullptr ? registered_cache->Size(content_id)
                                                      : std::optional<u64>{};

        IPC::ResponseBuilder rb{ctx, 4};
        rb.Push(size.has_value() ? ResultSuccess : ResultUnknown);
        rb.Push(size.value_or(0));
    }

    void GetPath(HLERequestContext& ctx) {
        IPC::RequestParser rp{ctx};
        [[maybe_unused]] const auto content_id = rp.PopRaw<FileSys::NcaID>();
        // Horizon returns a fixed-size fs-common path. Exposing Encore's host
        // VFS path would leak a host implementation detail and is not ABI-correct.
        LOG_WARNING(Service_NCM, "GetPath rejected: no Horizon fs-common mount path is modeled");
        IPC::ResponseBuilder rb{ctx, 2};
        rb.Push(ResultUnknown);
    }

    void GetPlaceHolderPath(HLERequestContext& ctx) {
        IPC::RequestParser rp{ctx};
        [[maybe_unused]] const auto placeholder_id = rp.PopRaw<FileSys::NcaID>();
        // Same contract as GetPath: never substitute a PS5 host path for the
        // guest-visible 0x300-byte fs-common path.
        LOG_WARNING(Service_NCM,
                    "GetPlaceHolderPath rejected: no Horizon fs-common mount path is modeled");
        IPC::ResponseBuilder rb{ctx, 2};
        rb.Push(ResultUnknown);
    }

    void CleanupAllPlaceHolder(HLERequestContext& ctx) {
        auto* const placeholder_cache =
            system.GetFileSystemController().GetPlaceholderCacheForStorage(storage);
        const bool succeeded = placeholder_cache != nullptr && placeholder_cache->CleanAll();
        IPC::ResponseBuilder rb{ctx, 2};
        rb.Push(succeeded ? ResultSuccess : ResultUnknown);
    }

    void ListPlaceHolder(HLERequestContext& ctx) {
        auto* const placeholder_cache =
            system.GetFileSystemController().GetPlaceholderCacheForStorage(storage);

        if (placeholder_cache == nullptr) {
            IPC::ResponseBuilder rb{ctx, 2};
            rb.Push(ResultUnknown);
            return;
        }

        const auto placeholders = placeholder_cache->List();
        const auto max_entries = ctx.GetWriteBufferSize() / sizeof(FileSys::NcaID);
        const auto copied = std::min(placeholders.size(), max_entries);
        if (copied != 0) {
            ctx.WriteBuffer(placeholders.data(), copied * sizeof(FileSys::NcaID));
        }

        IPC::ResponseBuilder rb{ctx, 3};
        rb.Push(ResultSuccess);
        rb.Push(static_cast<u32>(copied));
    }

    void DisableForcibly(HLERequestContext& ctx) {
        // This service object does not maintain a separate enabled/disabled backing state.
        // Closing or reopening the IPC object is therefore sufficient for Encore.
        IPC::ResponseBuilder rb{ctx, 2};
        rb.Push(ResultSuccess);
    }

    void RevertToPlaceHolder(HLERequestContext& ctx) {
        IPC::RequestParser rp{ctx};
        FileSys::NcaID placeholder_id{};
        FileSys::NcaID old_content_id{};
        FileSys::NcaID new_content_id{};
        if constexpr (HLE::ApiVersion::HOS_VERSION_MAJOR >= 16) {
            placeholder_id = rp.PopRaw<FileSys::NcaID>();
            old_content_id = rp.PopRaw<FileSys::NcaID>();
            new_content_id = rp.PopRaw<FileSys::NcaID>();
        } else {
            old_content_id = rp.PopRaw<FileSys::NcaID>();
            new_content_id = rp.PopRaw<FileSys::NcaID>();
            placeholder_id = rp.PopRaw<FileSys::NcaID>();
        }

        auto& fsc = system.GetFileSystemController();
        auto* const placeholder_cache = fsc.GetPlaceholderCacheForStorage(storage);
        auto* const registered_cache = fsc.GetRegisteredCacheForStorage(storage);
        const bool succeeded =
            placeholder_cache != nullptr && registered_cache != nullptr &&
            placeholder_cache->Revert(registered_cache, placeholder_id, old_content_id,
                                      new_content_id);
        IPC::ResponseBuilder rb{ctx, 2};
        rb.Push(succeeded ? ResultSuccess : ResultUnknown);
    }

    void ReadContentIdFile(HLERequestContext& ctx) {
        IPC::RequestParser rp{ctx};
        const auto content_id = rp.PopRaw<FileSys::NcaID>();
        const auto offset = rp.Pop<s64>();
        auto* const registered_cache =
            system.GetFileSystemController().GetRegisteredCacheForStorage(storage);
        const auto data =
            registered_cache != nullptr && offset >= 0
                ? registered_cache->ReadContent(content_id, static_cast<u64>(offset),
                                                ctx.GetWriteBufferSize())
                : std::optional<std::vector<u8>>{};
        if (data.has_value() && !data->empty()) {
            ctx.WriteBuffer(*data);
        }
        IPC::ResponseBuilder rb{ctx, 2};
        rb.Push(data.has_value() ? ResultSuccess : ResultUnknown);
    }

    void SetPlaceHolderSize(HLERequestContext& ctx) {
        IPC::RequestParser rp{ctx};
        const auto placeholder_id = rp.PopRaw<FileSys::NcaID>();
        const auto size = rp.Pop<s64>();
        auto* const placeholder_cache =
            system.GetFileSystemController().GetPlaceholderCacheForStorage(storage);
        const bool succeeded =
            placeholder_cache != nullptr && size >= 0 &&
            placeholder_cache->SetSize(placeholder_id, static_cast<u64>(size));

        IPC::ResponseBuilder rb{ctx, 2};
        rb.Push(succeeded ? ResultSuccess : ResultUnknown);
    }

    void GetRightsIdFromPlaceHolderId(HLERequestContext& ctx) {
        IPC::RequestParser rp{ctx};
        const auto placeholder_id = rp.PopRaw<FileSys::NcaID>();
        [[maybe_unused]] const auto attr = rp.Pop<u8>();
        auto* const placeholder_cache =
            system.GetFileSystemController().GetPlaceholderCacheForStorage(storage);
        const auto rights = placeholder_cache != nullptr
                                ? placeholder_cache->GetRightsIDWithKeyGeneration(placeholder_id)
                                : std::optional<std::pair<FileSys::NcaID, u8>>{};

        NcmRightsId out{};
        if (rights.has_value()) {
            out.rights_id = rights->first;
            out.key_generation = rights->second;
        }
        IPC::ResponseBuilder rb{ctx, 2 + (sizeof(NcmRightsId) + 3) / sizeof(u32)};
        rb.Push(rights.has_value() ? ResultSuccess : ResultUnknown);
        rb.PushRaw(out);
    }

    void GetRightsIdFromContentId(HLERequestContext& ctx) {
        IPC::RequestParser rp{ctx};
        const auto content_id = rp.PopRaw<FileSys::NcaID>();
        [[maybe_unused]] const auto attr = rp.Pop<u8>();
        auto* const registered_cache =
            system.GetFileSystemController().GetRegisteredCacheForStorage(storage);
        const auto rights = registered_cache != nullptr ? registered_cache->GetRightsId(content_id)
                                                        : std::optional<std::pair<FileSys::NcaID, u8>>{};

        NcmRightsId out{};
        if (rights.has_value()) {
            out.rights_id = rights->first;
            out.key_generation = rights->second;
        }
        IPC::ResponseBuilder rb{ctx, 2 + (sizeof(NcmRightsId) + 3) / sizeof(u32)};
        rb.Push(rights.has_value() ? ResultSuccess : ResultUnknown);
        rb.PushRaw(out);
    }

    void WriteContentForDebug(HLERequestContext& ctx) {
        LOG_WARNING(Service_NCM, "WriteContentForDebug rejected: development-hardware-only command");
        IPC::ResponseBuilder rb{ctx, 2};
        rb.Push(ResultUnknown);
    }

    void GetFreeSpaceSize(HLERequestContext& ctx) {
        const auto size = system.GetFileSystemController().GetFreeSpaceSize(storage);
        IPC::ResponseBuilder rb{ctx, 4};
        rb.Push(ResultSuccess);
        rb.Push(size);
    }

    void GetTotalSpaceSize(HLERequestContext& ctx) {
        const auto size = system.GetFileSystemController().GetTotalSpaceSize(storage);
        IPC::ResponseBuilder rb{ctx, 4};
        rb.Push(ResultSuccess);
        rb.Push(size);
    }

    void FlushPlaceHolder(HLERequestContext& ctx) {
        // Eden VFS writes are synchronous and expose no delayed flush primitive.
        IPC::ResponseBuilder rb{ctx, 2};
        rb.Push(ResultSuccess);
    }


    void GetRightsIdFromPlaceHolderIdWithCache(HLERequestContext& ctx) {
        IPC::RequestParser rp{ctx};
        const auto placeholder_id = rp.PopRaw<FileSys::NcaID>();
        [[maybe_unused]] const auto cache_content_id = rp.PopRaw<FileSys::NcaID>();
        [[maybe_unused]] const auto attr = rp.Pop<u8>();
        auto* const placeholder_cache =
            system.GetFileSystemController().GetPlaceholderCacheForStorage(storage);
        const auto rights = placeholder_cache != nullptr
                                ? placeholder_cache->GetRightsIDWithKeyGeneration(placeholder_id)
                                : std::optional<std::pair<FileSys::NcaID, u8>>{};

        NcmRightsId out{};
        if (rights.has_value()) {
            out.rights_id = rights->first;
            out.key_generation = rights->second;
        }
        IPC::ResponseBuilder rb{ctx, 2 + (sizeof(NcmRightsId) + 3) / sizeof(u32)};
        rb.Push(rights.has_value() ? ResultSuccess : ResultUnknown);
        rb.PushRaw(out);
    }

    void RepairInvalidFileAttribute(HLERequestContext& ctx) {
        // The VFS layer does not model the Horizon invalid-file-attribute bit.
        // There is therefore no represented attribute state to repair.
        IPC::ResponseBuilder rb{ctx, 2};
        rb.Push(ResultSuccess);
    }

    void GetSizeFromPlaceHolderId(HLERequestContext& ctx) {
        IPC::RequestParser rp{ctx};
        const auto placeholder_id = rp.PopRaw<FileSys::NcaID>();
        auto* const placeholder_cache =
            system.GetFileSystemController().GetPlaceholderCacheForStorage(storage);
        const bool exists =
            placeholder_cache != nullptr && placeholder_cache->Exists(placeholder_id);

        IPC::ResponseBuilder rb{ctx, 4};
        rb.Push(exists ? ResultSuccess : ResultUnknown);
        rb.Push(exists ? placeholder_cache->Size(placeholder_id) : u64{0});
    }

    FileSys::StorageId storage;
};

class IContentMetaDatabase final : public ServiceFramework<IContentMetaDatabase> {
public:
    explicit IContentMetaDatabase(Core::System& system_, FileSys::StorageId id)
        : ServiceFramework{system_, "IContentMetaDatabase"}, storage{id} {
        // clang-format off
        static const FunctionInfo functions[] = {
            {0, &IContentMetaDatabase::Set, "Set"},
            {1, &IContentMetaDatabase::Get, "Get"},
            {2, &IContentMetaDatabase::Remove, "Remove"},
            {3, &IContentMetaDatabase::GetContentIdByType, "GetContentIdByType"},
            {4, &IContentMetaDatabase::ListContentInfo, "ListContentInfo"},
            {5, &IContentMetaDatabase::List, "List"},
            {6, &IContentMetaDatabase::GetLatestContentMetaKey, "GetLatestContentMetaKey"},
            {7, &IContentMetaDatabase::ListApplication, "ListApplication"},
            {8, &IContentMetaDatabase::Has, "Has"},
            {9, &IContentMetaDatabase::HasAll, "HasAll"},
            {10, &IContentMetaDatabase::GetSize, "GetSize"},
            {11, &IContentMetaDatabase::GetRequiredSystemVersion, "GetRequiredSystemVersion"},
            {12, &IContentMetaDatabase::GetPatchContentMetaId, "GetPatchContentMetaId"},
            {13, &IContentMetaDatabase::DisableForcibly, "DisableForcibly"},
            {14, &IContentMetaDatabase::LookupOrphanContent, "LookupOrphanContent"},
            {15, &IContentMetaDatabase::Commit, "Commit"},
            {16, &IContentMetaDatabase::HasContent, "HasContent"},
            {17, &IContentMetaDatabase::ListContentMetaInfo, "ListContentMetaInfo"},
            {18, &IContentMetaDatabase::GetAttributes, "GetAttributes"},
            {19, &IContentMetaDatabase::GetRequiredApplicationVersion, "GetRequiredApplicationVersion"},
            {20, &IContentMetaDatabase::GetContentIdByTypeAndIdOffset, "GetContentIdByTypeAndIdOffset"},
        };
        // clang-format on

        RegisterHandlers(functions);
    }

private:
    struct ContentMetaKey {
        u64 id;
        u32 version;
        FileSys::TitleType type;
        u8 install_type;
        std::array<u8, 2> padding;
    };
    static_assert(sizeof(ContentMetaKey) == 0x10);

    struct ContentInfo {
        FileSys::NcaID content_id{};
        u32 size_low{};
        u8 size_high{};
        u8 attributes{};
        FileSys::ContentRecordType type{};
        u8 id_offset{};
    };
    static_assert(sizeof(ContentInfo) == 0x18);

    struct ContentMetaInfo {
        u64 id{};
        u32 version{};
        FileSys::TitleType type{};
        u8 attributes{};
        std::array<u8, 2> padding{};
    };
    static_assert(sizeof(ContentMetaInfo) == 0x10);

    struct ContentMetaHeader {
        u16 extended_header_size{};
        u16 content_count{};
        u16 content_meta_count{};
        u8 attributes{};
        u8 platform{};
    };
    static_assert(sizeof(ContentMetaHeader) == 0x8);

    struct ApplicationContentMetaKey {
        ContentMetaKey key{};
        u64 application_id{};
    };
    static_assert(sizeof(ApplicationContentMetaKey) == 0x18);

    struct ListRequest {
        u8 meta_type{};
        u8 install_type{};
        std::array<u8, 6> padding{};
        u64 application_id{};
        u64 min_id{};
        u64 max_id{};
    };
    static_assert(sizeof(ListRequest) == 0x20);

    struct PendingMeta {
        ContentMetaKey key{};
        std::vector<u8> value;
    };

    struct GetContentIdRequest {
        u8 type{};
        std::array<u8, 7> padding{};
        ContentMetaKey key{};
    };
    static_assert(sizeof(GetContentIdRequest) == 0x18);

    struct ListInfoRequest {
        s32 offset{};
        u32 padding{};
        ContentMetaKey key{};
    };
    static_assert(sizeof(ListInfoRequest) == 0x18);

    struct HasContentRequest {
        FileSys::NcaID content_id{};
        ContentMetaKey key{};
    };
    static_assert(sizeof(HasContentRequest) == 0x20);

    struct GetContentIdOffsetRequest {
        u8 type{};
        u8 id_offset{};
        std::array<u8, 6> padding{};
        ContentMetaKey key{};
    };
    static_assert(sizeof(GetContentIdOffsetRequest) == 0x18);

    static bool SameKey(const ContentMetaKey& lhs, const ContentMetaKey& rhs) {
        return lhs.id == rhs.id && lhs.version == rhs.version && lhs.type == rhs.type &&
               lhs.install_type == rhs.install_type;
    }

    static bool KeyLess(const ContentMetaKey& lhs, const ContentMetaKey& rhs) {
        if (lhs.id != rhs.id) {
            return lhs.id < rhs.id;
        }
        if (lhs.version != rhs.version) {
            return lhs.version < rhs.version;
        }
        if (lhs.type != rhs.type) {
            return static_cast<u8>(lhs.type) < static_cast<u8>(rhs.type);
        }
        return lhs.install_type < rhs.install_type;
    }

    static ContentMetaKey KeyFromCnmt(const FileSys::CNMT& cnmt) {
        const auto& header = cnmt.GetHeader();
        return ContentMetaKey{cnmt.GetTitleID(), cnmt.GetTitleVersion(), cnmt.GetType(),
                              header.reserved2[1], {}};
    }

    static bool MatchesKey(const FileSys::CNMT& cnmt, const ContentMetaKey& key) {
        return SameKey(KeyFromCnmt(cnmt), key);
    }

    PendingMeta* FindPending(const ContentMetaKey& key) {
        const auto it = std::find_if(entries.begin(), entries.end(), [&key](const PendingMeta& e) {
            return SameKey(e.key, key);
        });
        return it == entries.end() ? nullptr : &*it;
    }

    const PendingMeta* FindPending(const ContentMetaKey& key) const {
        const auto it = std::find_if(entries.begin(), entries.end(), [&key](const PendingMeta& e) {
            return SameKey(e.key, key);
        });
        return it == entries.end() ? nullptr : &*it;
    }

    const FileSys::CNMT* FindRegisteredMeta(const ContentMetaKey& key) const {
        auto* const cache = system.GetFileSystemController().GetRegisteredCacheForStorage(storage);
        if (cache == nullptr) {
            return nullptr;
        }
        const auto* const cnmt = cache->GetContentMeta(key.id);
        return cnmt != nullptr && MatchesKey(*cnmt, key) ? cnmt : nullptr;
    }

    bool HasKey(const ContentMetaKey& key) const {
        return FindPending(key) != nullptr || FindRegisteredMeta(key) != nullptr;
    }

    std::vector<ContentMetaKey> ListKeys() const {
        std::vector<ContentMetaKey> keys;
        keys.reserve(entries.size() + 16);
        for (const auto& entry : entries) {
            keys.push_back(entry.key);
        }
        if (auto* const cache =
                system.GetFileSystemController().GetRegisteredCacheForStorage(storage);
            cache != nullptr) {
            for (const auto* const cnmt : cache->ListContentMetas()) {
                if (cnmt != nullptr) {
                    keys.push_back(KeyFromCnmt(*cnmt));
                }
            }
        }
        std::sort(keys.begin(), keys.end(), KeyLess);
        keys.erase(std::unique(keys.begin(), keys.end(), SameKey), keys.end());
        return keys;
    }

    static std::optional<u64> ReadExtendedU64(const std::vector<u8>& value,
                                               std::size_t offset) {
        if (value.size() < sizeof(ContentMetaHeader)) {
            return std::nullopt;
        }
        ContentMetaHeader header{};
        std::memcpy(&header, value.data(), sizeof(header));
        if (offset + sizeof(u64) > header.extended_header_size ||
            sizeof(ContentMetaHeader) + offset + sizeof(u64) > value.size()) {
            return std::nullopt;
        }
        u64 result{};
        std::memcpy(&result, value.data() + sizeof(ContentMetaHeader) + offset, sizeof(result));
        return result;
    }

    static std::optional<u32> ReadExtendedU32(const std::vector<u8>& value,
                                               std::size_t offset) {
        if (value.size() < sizeof(ContentMetaHeader)) {
            return std::nullopt;
        }
        ContentMetaHeader header{};
        std::memcpy(&header, value.data(), sizeof(header));
        if (offset + sizeof(u32) > header.extended_header_size ||
            sizeof(ContentMetaHeader) + offset + sizeof(u32) > value.size()) {
            return std::nullopt;
        }
        u32 result{};
        std::memcpy(&result, value.data() + sizeof(ContentMetaHeader) + offset, sizeof(result));
        return result;
    }

    static std::optional<u64> GetApplicationId(const ContentMetaKey& key,
                                                const std::vector<u8>& value) {
        switch (key.type) {
        case FileSys::TitleType::Application:
            return key.id;
        case FileSys::TitleType::Update:
        case FileSys::TitleType::AOC:
        case FileSys::TitleType::DeltaTitle:
            return ReadExtendedU64(value, 0);
        default:
            return std::nullopt;
        }
    }

    std::optional<std::vector<u8>> BuildRegisteredValue(const ContentMetaKey& key) const {
        auto* const cache = system.GetFileSystemController().GetRegisteredCacheForStorage(storage);
        const auto* const cnmt = FindRegisteredMeta(key);
        if (cache == nullptr || cnmt == nullptr) {
            return std::nullopt;
        }

        const auto meta_id = cache->GetMetaNcaId(key.id);
        if (!meta_id.has_value()) {
            return std::nullopt;
        }
        const auto meta_size = cache->Size(*meta_id);
        if (!meta_size.has_value() || *meta_size > 0xFFFFFFFFFFULL) {
            return std::nullopt;
        }

        const auto& extended = cnmt->GetExtendedHeader();
        const auto& records = cnmt->GetContentRecords();
        const auto& meta_records = cnmt->GetMetaRecords();
        if (extended.size() > std::numeric_limits<u16>::max() ||
            meta_records.size() > std::numeric_limits<u16>::max()) {
            return std::nullopt;
        }

        std::vector<ContentInfo> infos;
        infos.reserve(records.size() + 1);
        ContentInfo meta_info{};
        meta_info.content_id = *meta_id;
        meta_info.size_low = static_cast<u32>(*meta_size);
        meta_info.size_high = static_cast<u8>(*meta_size >> 32);
        meta_info.type = FileSys::ContentRecordType::Meta;
        infos.push_back(meta_info);
        for (const auto& record : records) {
            if (key.type == FileSys::TitleType::Update &&
                record.type == FileSys::ContentRecordType::DeltaFragment) {
                continue;
            }
            infos.push_back(ConvertContentInfo(record));
        }
        if (infos.size() > std::numeric_limits<u16>::max()) {
            return std::nullopt;
        }

        const ContentMetaHeader normal_header{
            static_cast<u16>(extended.size()), static_cast<u16>(infos.size()),
            static_cast<u16>(meta_records.size()), cnmt->GetHeader().attributes,
            cnmt->GetHeader().reserved};

        const auto total_size =
            sizeof(ContentMetaHeader) + extended.size() + infos.size() * sizeof(ContentInfo) +
            meta_records.size() * sizeof(ContentMetaInfo);
        std::vector<u8> value(total_size);
        std::size_t offset = 0;
        const auto append = [&value, &offset](const void* src, std::size_t size) {
            if (size != 0) {
                std::memcpy(value.data() + offset, src, size);
            }
            offset += size;
        };
        append(&normal_header, sizeof(normal_header));
        append(extended.data(), extended.size());
        append(infos.data(), infos.size() * sizeof(ContentInfo));
        for (const auto& record : meta_records) {
            const ContentMetaInfo info{record.title_id, record.title_version, record.type,
                                       record.install_byte, {}};
            append(&info, sizeof(info));
        }
        return value;
    }

    std::optional<std::vector<u8>> GetValue(const ContentMetaKey& key) const {
        if (const auto* const pending = FindPending(key); pending != nullptr) {
            return pending->value;
        }
        return BuildRegisteredValue(key);
    }

    static ContentInfo ConvertContentInfo(const FileSys::ContentRecord& record) {
        ContentInfo info{};
        info.content_id = record.nca_id;
        info.size_low = static_cast<u32>(record.size[0]) |
                        (static_cast<u32>(record.size[1]) << 8) |
                        (static_cast<u32>(record.size[2]) << 16) |
                        (static_cast<u32>(record.size[3]) << 24);
        info.size_high = record.size[4];
        info.attributes = record.size[5];
        info.type = record.type;
        info.id_offset = record.id_offset;
        return info;
    }

    void Set(HLERequestContext& ctx) {
        IPC::RequestParser rp{ctx};
        const auto key = rp.PopRaw<ContentMetaKey>();
        auto value = ctx.ReadBufferCopy();

        if (auto* const existing = FindPending(key); existing != nullptr) {
            existing->value = std::move(value);
        } else {
            entries.push_back(PendingMeta{key, std::move(value)});
        }

        LOG_DEBUG(Service_NCM,
                  "called, storage_id={}, title_id={:016X}, version={}, type={}, size={}",
                  static_cast<u32>(storage), key.id, key.version, static_cast<u8>(key.type),
                  ctx.GetReadBufferSize());

        IPC::ResponseBuilder rb{ctx, 2};
        rb.Push(ResultSuccess);
    }

    void Get(HLERequestContext& ctx) {
        IPC::RequestParser rp{ctx};
        const auto key = rp.PopRaw<ContentMetaKey>();
        const auto value = GetValue(key);
        const bool valid = value.has_value() && ctx.GetWriteBufferSize() >= value->size();
        if (valid && !value->empty()) {
            ctx.WriteBuffer(*value);
        }

        IPC::ResponseBuilder rb{ctx, 4};
        rb.Push(valid ? ResultSuccess : ResultUnknown);
        rb.Push(valid ? static_cast<u64>(value->size()) : u64{0});
    }

    void Remove(HLERequestContext& ctx) {
        IPC::RequestParser rp{ctx};
        const auto key = rp.PopRaw<ContentMetaKey>();

        std::erase_if(entries, [&key](const PendingMeta& entry) {
            return SameKey(entry.key, key);
        });

        LOG_DEBUG(Service_NCM, "called, storage_id={}, title_id={:016X}, version={}, type={}",
                  static_cast<u32>(storage), key.id, key.version, static_cast<u8>(key.type));

        IPC::ResponseBuilder rb{ctx, 2};
        rb.Push(ResultSuccess);
    }

    void GetContentIdByType(HLERequestContext& ctx) {
        IPC::RequestParser rp{ctx};
        const auto request = rp.PopRaw<GetContentIdRequest>();
        const auto* const cnmt = FindRegisteredMeta(request.key);
        std::optional<FileSys::NcaID> found{};
        if (cnmt != nullptr) {
            const auto type = static_cast<FileSys::ContentRecordType>(request.type);
            const auto& records = cnmt->GetContentRecords();
            const auto it = std::find_if(records.begin(), records.end(), [type](const auto& record) {
                return record.type == type && record.id_offset == 0;
            });
            if (it != records.end()) {
                found = it->nca_id;
            }
        }
        IPC::ResponseBuilder rb{ctx, 6};
        rb.Push(found.has_value() ? ResultSuccess : ResultUnknown);
        rb.PushRaw(found.value_or(FileSys::NcaID{}));
    }

    void ListContentInfo(HLERequestContext& ctx) {
        IPC::RequestParser rp{ctx};
        const auto request = rp.PopRaw<ListInfoRequest>();
        const auto* const cnmt = request.offset >= 0 ? FindRegisteredMeta(request.key) : nullptr;
        if (cnmt == nullptr) {
            IPC::ResponseBuilder rb{ctx, 2};
            rb.Push(ResultUnknown);
            return;
        }

        const auto& records = cnmt->GetContentRecords();
        const auto start = static_cast<std::size_t>(request.offset);
        const auto available = start < records.size() ? records.size() - start : 0;
        const auto max_entries = ctx.GetWriteBufferSize() / sizeof(ContentInfo);
        const auto copied = std::min(available, max_entries);
        std::vector<ContentInfo> out;
        out.reserve(copied);
        for (std::size_t index = 0; index < copied; ++index) {
            out.push_back(ConvertContentInfo(records[start + index]));
        }
        if (!out.empty()) {
            ctx.WriteBuffer(out);
        }
        IPC::ResponseBuilder rb{ctx, 3};
        rb.Push(ResultSuccess);
        rb.Push(static_cast<s32>(copied));
    }

    void List(HLERequestContext& ctx) {
        IPC::RequestParser rp{ctx};
        const auto request = rp.PopRaw<ListRequest>();
        const auto keys = ListKeys();
        const auto max_entries = ctx.GetWriteBufferSize() / sizeof(ContentMetaKey);
        std::vector<ContentMetaKey> out;
        std::size_t total = 0;
        for (const auto& key : keys) {
            if ((request.meta_type != 0 && static_cast<u8>(key.type) != request.meta_type) ||
                key.id < request.min_id || key.id > request.max_id ||
                (request.install_type != 7 && key.install_type != request.install_type)) {
                continue;
            }
            if (request.application_id != 0) {
                const auto value = GetValue(key);
                if (!value.has_value()) {
                    continue;
                }
                const auto owner = GetApplicationId(key, *value);
                if (owner.has_value() && *owner != request.application_id) {
                    continue;
                }
            }
            ++total;
            if (out.size() < max_entries) {
                out.push_back(key);
            }
        }
        if (!out.empty()) {
            ctx.WriteBuffer(out);
        }

        IPC::ResponseBuilder rb{ctx, 4};
        rb.Push(ResultSuccess);
        rb.Push(static_cast<s32>((std::min)(total,
            static_cast<std::size_t>((std::numeric_limits<s32>::max)()))));
        rb.Push(static_cast<s32>(out.size()));
    }

    void GetLatestContentMetaKey(HLERequestContext& ctx) {
        IPC::RequestParser rp{ctx};
        const auto id = rp.Pop<u64>();
        const auto keys = ListKeys();
        std::optional<ContentMetaKey> found{};
        for (const auto& key : keys) {
            if (key.id == id && key.install_type == 0 &&
                (!found.has_value() || KeyLess(*found, key))) {
                found = key;
            }
        }

        IPC::ResponseBuilder rb{ctx, 6};
        rb.Push(found.has_value() ? ResultSuccess : ResultUnknown);
        rb.PushRaw(found.value_or(ContentMetaKey{}));
    }

    void ListApplication(HLERequestContext& ctx) {
        IPC::RequestParser rp{ctx};
        const auto meta_type = rp.Pop<u8>();
        const auto keys = ListKeys();
        const auto max_entries = ctx.GetWriteBufferSize() / sizeof(ApplicationContentMetaKey);
        std::vector<ApplicationContentMetaKey> out;
        std::size_t total = 0;
        for (const auto& key : keys) {
            if (meta_type != 0 && static_cast<u8>(key.type) != meta_type) {
                continue;
            }
            const auto value = GetValue(key);
            if (!value.has_value()) {
                continue;
            }
            const auto owner = GetApplicationId(key, *value);
            if (!owner.has_value()) {
                continue;
            }
            ++total;
            if (out.size() < max_entries) {
                out.push_back(ApplicationContentMetaKey{key, *owner});
            }
        }
        if (!out.empty()) {
            ctx.WriteBuffer(out);
        }

        IPC::ResponseBuilder rb{ctx, 4};
        rb.Push(ResultSuccess);
        rb.Push(static_cast<s32>((std::min)(total,
            static_cast<std::size_t>((std::numeric_limits<s32>::max)()))));
        rb.Push(static_cast<s32>(out.size()));
    }

    void Has(HLERequestContext& ctx) {
        IPC::RequestParser rp{ctx};
        const auto key = rp.PopRaw<ContentMetaKey>();

        const bool has = HasKey(key);

        LOG_DEBUG(Service_NCM, "called, storage_id={}, title_id={:016X}, version={}, type={}, has={}",
                  static_cast<u32>(storage), key.id, key.version, static_cast<u8>(key.type), has);

        IPC::ResponseBuilder rb{ctx, 3};
        rb.Push(ResultSuccess);
        rb.Push(has);
    }

    void HasAll(HLERequestContext& ctx) {
        const auto data = ctx.ReadBuffer();
        bool has_all = data.size() % sizeof(ContentMetaKey) == 0;
        for (std::size_t offset = 0; has_all && offset < data.size();
             offset += sizeof(ContentMetaKey)) {
            ContentMetaKey key{};
            std::memcpy(&key, data.data() + offset, sizeof(key));
            has_all = HasKey(key);
        }
        IPC::ResponseBuilder rb{ctx, 3};
        rb.Push(ResultSuccess);
        rb.Push(has_all);
    }

    void GetSize(HLERequestContext& ctx) {
        IPC::RequestParser rp{ctx};
        const auto key = rp.PopRaw<ContentMetaKey>();
        const auto value = GetValue(key);

        IPC::ResponseBuilder rb{ctx, 4};
        rb.Push(value.has_value() ? ResultSuccess : ResultUnknown);
        rb.Push(value.has_value() ? static_cast<u64>(value->size()) : u64{0});
    }

    void GetRequiredSystemVersion(HLERequestContext& ctx) {
        IPC::RequestParser rp{ctx};
        const auto key = rp.PopRaw<ContentMetaKey>();
        const auto value = GetValue(key);
        const auto version =
            value.has_value() &&
                    (key.type == FileSys::TitleType::Application ||
                     key.type == FileSys::TitleType::Update)
                ? ReadExtendedU32(*value, 8)
                : std::optional<u32>{};

        IPC::ResponseBuilder rb{ctx, 3};
        rb.Push(version.has_value() ? ResultSuccess : ResultUnknown);
        rb.Push(version.value_or(0));
    }

    void GetPatchContentMetaId(HLERequestContext& ctx) {
        IPC::RequestParser rp{ctx};
        const auto key = rp.PopRaw<ContentMetaKey>();
        const auto value = GetValue(key);
        std::optional<u64> patch_id{};
        if (value.has_value()) {
            if (key.type == FileSys::TitleType::Application) {
                patch_id = ReadExtendedU64(*value, 0);
            } else if (key.type == FileSys::TitleType::AOC) {
                patch_id = ReadExtendedU64(*value, 16);
            }
        }

        IPC::ResponseBuilder rb{ctx, 4};
        rb.Push(patch_id.has_value() ? ResultSuccess : ResultUnknown);
        rb.Push(patch_id.value_or(0));
    }

    void DisableForcibly(HLERequestContext& ctx) {
        IPC::ResponseBuilder rb{ctx, 2};
        rb.Push(ResultSuccess);
    }

    void LookupOrphanContent(HLERequestContext& ctx) {
        const auto data = ctx.ReadBuffer();
        auto* const cache = system.GetFileSystemController().GetRegisteredCacheForStorage(storage);
        if (cache == nullptr || data.size() % sizeof(FileSys::NcaID) != 0) {
            IPC::ResponseBuilder rb{ctx, 2};
            rb.Push(ResultUnknown);
            return;
        }
        const auto count = data.size() / sizeof(FileSys::NcaID);
        if (ctx.GetWriteBufferSize() < count) {
            IPC::ResponseBuilder rb{ctx, 2};
            rb.Push(ResultUnknown);
            return;
        }
        std::vector<u8> orphaned(count, 1);
        for (std::size_t index = 0; index < count; ++index) {
            FileSys::NcaID id{};
            std::memcpy(&id, data.data() + index * sizeof(id), sizeof(id));
            orphaned[index] = cache->ReferencesContent(id) ? 0 : 1;
        }
        if (!orphaned.empty()) {
            ctx.WriteBuffer(orphaned);
        }
        IPC::ResponseBuilder rb{ctx, 2};
        rb.Push(ResultSuccess);
    }

    void HasContent(HLERequestContext& ctx) {
        IPC::RequestParser rp{ctx};
        const auto request = rp.PopRaw<HasContentRequest>();
        const auto* const cnmt = FindRegisteredMeta(request.key);
        bool found = false;
        if (cnmt != nullptr) {
            const auto& records = cnmt->GetContentRecords();
            found = std::any_of(records.begin(), records.end(), [&request](const auto& record) {
                return record.nca_id == request.content_id;
            });
        }
        IPC::ResponseBuilder rb{ctx, 3};
        rb.Push(cnmt != nullptr ? ResultSuccess : ResultUnknown);
        rb.Push(found);
    }

    void ListContentMetaInfo(HLERequestContext& ctx) {
        IPC::RequestParser rp{ctx};
        const auto request = rp.PopRaw<ListInfoRequest>();
        const auto* const cnmt = request.offset >= 0 ? FindRegisteredMeta(request.key) : nullptr;
        if (cnmt == nullptr) {
            IPC::ResponseBuilder rb{ctx, 2};
            rb.Push(ResultUnknown);
            return;
        }
        const auto& records = cnmt->GetMetaRecords();
        const auto start = static_cast<std::size_t>(request.offset);
        const auto available = start < records.size() ? records.size() - start : 0;
        const auto max_entries = ctx.GetWriteBufferSize() / sizeof(ContentMetaInfo);
        const auto copied = std::min(available, max_entries);
        std::vector<ContentMetaInfo> out;
        out.reserve(copied);
        for (std::size_t index = 0; index < copied; ++index) {
            const auto& record = records[start + index];
            out.push_back(ContentMetaInfo{record.title_id, record.title_version, record.type,
                                          record.install_byte, {}});
        }
        if (!out.empty()) {
            ctx.WriteBuffer(out);
        }
        IPC::ResponseBuilder rb{ctx, 3};
        rb.Push(ResultSuccess);
        rb.Push(static_cast<s32>(copied));
    }

    void GetAttributes(HLERequestContext& ctx) {
        IPC::RequestParser rp{ctx};
        const auto key = rp.PopRaw<ContentMetaKey>();
        const auto* const cnmt = FindRegisteredMeta(key);
        IPC::ResponseBuilder rb{ctx, 3};
        rb.Push(cnmt != nullptr ? ResultSuccess : ResultUnknown);
        rb.Push(cnmt != nullptr ? cnmt->GetHeader().attributes : u8{0});
    }

    void GetRequiredApplicationVersion(HLERequestContext& ctx) {
        IPC::RequestParser rp{ctx};
        const auto key = rp.PopRaw<ContentMetaKey>();
        const auto value = GetValue(key);
        std::optional<u32> version{};
        if (value.has_value()) {
            if (key.type == FileSys::TitleType::Application) {
                version = ReadExtendedU32(*value, 12);
            } else if (key.type == FileSys::TitleType::AOC) {
                version = ReadExtendedU32(*value, 8);
            }
        }

        IPC::ResponseBuilder rb{ctx, 3};
        rb.Push(version.has_value() ? ResultSuccess : ResultUnknown);
        rb.Push(version.value_or(0));
    }

    void GetContentIdByTypeAndIdOffset(HLERequestContext& ctx) {
        IPC::RequestParser rp{ctx};
        const auto request = rp.PopRaw<GetContentIdOffsetRequest>();
        const auto* const cnmt = FindRegisteredMeta(request.key);
        std::optional<FileSys::NcaID> found{};
        if (cnmt != nullptr) {
            const auto type = static_cast<FileSys::ContentRecordType>(request.type);
            const auto& records = cnmt->GetContentRecords();
            const auto it =
                std::find_if(records.begin(), records.end(), [&request, type](const auto& record) {
                    return record.type == type && record.id_offset == request.id_offset;
                });
            if (it != records.end()) {
                found = it->nca_id;
            }
        }
        IPC::ResponseBuilder rb{ctx, 6};
        rb.Push(found.has_value() ? ResultSuccess : ResultUnknown);
        rb.PushRaw(found.value_or(FileSys::NcaID{}));
    }

    void Commit(HLERequestContext& ctx) {
        auto* const registered_cache =
            system.GetFileSystemController().GetRegisteredCacheForStorage(storage);
        if (registered_cache != nullptr) {
            registered_cache->Refresh();
        }

        LOG_DEBUG(Service_NCM, "called, storage_id={}", static_cast<u32>(storage));

        IPC::ResponseBuilder rb{ctx, 2};
        rb.Push(ResultSuccess);
    }

    FileSys::StorageId storage;
    std::vector<PendingMeta> entries;
};

class LR final : public ServiceFramework<LR> {
public:
    explicit LR(Core::System& system_) : ServiceFramework{system_, "lr"} {
        // clang-format off
        static const FunctionInfo functions[] = {
            {0, &LR::OpenLocationResolver, "OpenLocationResolver"},
            {1, &LR::OpenRegisteredLocationResolver, "OpenRegisteredLocationResolver"},
            {2, &LR::RefreshLocationResolver, "RefreshLocationResolver"},
            {3, &LR::OpenAddOnContentLocationResolver, "OpenAddOnContentLocationResolver"},
        };
        // clang-format on

        RegisterHandlers(functions);
    }

private:
    void OpenLocationResolver(HLERequestContext& ctx) {
        IPC::RequestParser rp{ctx};
        const auto storage_id = rp.PopEnum<FileSys::StorageId>();

        IPC::ResponseBuilder rb{ctx, 2, 0, 1};
        rb.Push(ResultSuccess);
        rb.PushIpcInterface<ILocationResolver>(ctx, system, storage_id);
    }

    void OpenRegisteredLocationResolver(HLERequestContext& ctx) {
        IPC::ResponseBuilder rb{ctx, 2, 0, 1};
        rb.Push(ResultSuccess);
        rb.PushIpcInterface<IRegisteredLocationResolver>(ctx, system);
    }

    void RefreshLocationResolver(HLERequestContext& ctx) {
        IPC::RequestParser rp{ctx};
        const auto storage_id = rp.PopEnum<FileSys::StorageId>();
        // Location resolvers are currently stateless objects in Encore. There is no
        // cached redirection table to invalidate at manager scope, so refresh is a
        // documented no-op rather than a fabricated path mutation.
        LOG_DEBUG(Service_NCM, "Location resolver refresh, storage_id={}",
                  static_cast<u32>(storage_id));
        IPC::ResponseBuilder rb{ctx, 2};
        rb.Push(ResultSuccess);
    }

    void OpenAddOnContentLocationResolver(HLERequestContext& ctx) {
        IPC::ResponseBuilder rb{ctx, 2, 0, 1};
        rb.Push(ResultSuccess);
        rb.PushIpcInterface<IAddOnContentLocationResolver>(ctx, system);
    }
};

class NCM final : public ServiceFramework<NCM> {
public:
    explicit NCM(Core::System& system_) : ServiceFramework{system_, "ncm"} {
        // clang-format off
        static const FunctionInfo functions[] = {
            {0, &NCM::CreateContentStorageNoOp, "CreateContentStorage"},
            {1, &NCM::CreateContentMetaDatabaseNoOp, "CreateContentMetaDatabase"},
            {2, &NCM::VerifyContentStorage, "VerifyContentStorage"},
            {3, &NCM::VerifyContentMetaDatabase, "VerifyContentMetaDatabase"},
            {4, &NCM::OpenContentStorage, "OpenContentStorage"},
            {5, &NCM::OpenContentMetaDatabase, "OpenContentMetaDatabase"},
            {6, &NCM::ContentStorageLifecycleNoOp, "CloseContentStorageForcibly"},
            {7, &NCM::ContentMetaDatabaseLifecycleNoOp, "CloseContentMetaDatabaseForcibly"},
            {8, nullptr, "CleanupContentMetaDatabase"},
            {9, &NCM::ContentStorageLifecycleNoOp, "ActivateContentStorage"},
            {10, &NCM::ContentStorageLifecycleNoOp, "InactivateContentStorage"},
            {11, &NCM::ContentMetaDatabaseLifecycleNoOp, "ActivateContentMetaDatabase"},
            {12, &NCM::ContentMetaDatabaseLifecycleNoOp, "InactivateContentMetaDatabase"},
            {13, &NCM::InvalidateRightsIdCache, "InvalidateRightsIdCache"},
            {14, nullptr, "GetMemoryReport"},
            {15, nullptr, "ActivateFsContentStorage"},
        };
        // clang-format on

        RegisterHandlers(functions);
    }

private:
    void CreateContentStorageNoOp(HLERequestContext& ctx) {
        IPC::RequestParser rp{ctx};
        const auto storage_id = rp.PopEnum<FileSys::StorageId>();
        auto& fsc = system.GetFileSystemController();
        const bool provisioned = fsc.GetRegisteredCacheForStorage(storage_id) != nullptr &&
                                 fsc.GetPlaceholderCacheForStorage(storage_id) != nullptr;
        // FileSystemController provisions Encore's content-storage backends at
        // startup. If the requested storage already exists, the HOS Create
        // post-condition is satisfied without rebuilding host directories.
        IPC::ResponseBuilder rb{ctx, 2};
        rb.Push(provisioned ? ResultSuccess : ResultUnknown);
    }

    void CreateContentMetaDatabaseNoOp(HLERequestContext& ctx) {
        IPC::RequestParser rp{ctx};
        const auto storage_id = rp.PopEnum<FileSys::StorageId>();
        const bool provisioned =
            system.GetFileSystemController().GetRegisteredCacheForStorage(storage_id) != nullptr;
        // The registered cache is the backing database used by Encore's
        // IContentMetaDatabase. It is likewise provisioned before NCM starts.
        IPC::ResponseBuilder rb{ctx, 2};
        rb.Push(provisioned ? ResultSuccess : ResultUnknown);
    }

    void VerifyContentStorage(HLERequestContext& ctx) {
        IPC::RequestParser rp{ctx};
        const auto storage_id = rp.PopEnum<FileSys::StorageId>();
        auto& fsc = system.GetFileSystemController();
        const bool available = fsc.GetRegisteredCacheForStorage(storage_id) != nullptr &&
                               fsc.GetPlaceholderCacheForStorage(storage_id) != nullptr;

        IPC::ResponseBuilder rb{ctx, 2};
        rb.Push(available ? ResultSuccess : ResultUnknown);
    }

    void VerifyContentMetaDatabase(HLERequestContext& ctx) {
        IPC::RequestParser rp{ctx};
        const auto storage_id = rp.PopEnum<FileSys::StorageId>();
        const bool available =
            system.GetFileSystemController().GetRegisteredCacheForStorage(storage_id) != nullptr;

        IPC::ResponseBuilder rb{ctx, 2};
        rb.Push(available ? ResultSuccess : ResultUnknown);
    }

    void ContentStorageLifecycleNoOp(HLERequestContext& ctx) {
        IPC::RequestParser rp{ctx};
        const auto storage_id = rp.PopEnum<FileSys::StorageId>();
        auto& fsc = system.GetFileSystemController();
        const bool available = fsc.GetRegisteredCacheForStorage(storage_id) != nullptr &&
                               fsc.GetPlaceholderCacheForStorage(storage_id) != nullptr;
        // Encore owns these caches for the whole emulation session. There is no
        // separate NCM daemon activation state to toggle or forcibly close.
        IPC::ResponseBuilder rb{ctx, 2};
        rb.Push(available ? ResultSuccess : ResultUnknown);
    }

    void ContentMetaDatabaseLifecycleNoOp(HLERequestContext& ctx) {
        IPC::RequestParser rp{ctx};
        const auto storage_id = rp.PopEnum<FileSys::StorageId>();
        const bool available =
            system.GetFileSystemController().GetRegisteredCacheForStorage(storage_id) != nullptr;
        // Same ownership model as ContentStorage: the cache remains live while
        // FileSystemController is alive, so lifecycle transitions are neutral.
        IPC::ResponseBuilder rb{ctx, 2};
        rb.Push(available ? ResultSuccess : ResultUnknown);
    }

    void InvalidateRightsIdCache(HLERequestContext& ctx) {
        // PlaceholderCache::GetRightsID reads and parses the NCA on every call.
        // Encore has no memoized NCM rights-ID cache to invalidate.
        IPC::ResponseBuilder rb{ctx, 2};
        rb.Push(ResultSuccess);
    }

    void OpenContentStorage(HLERequestContext& ctx) {
        IPC::RequestParser rp{ctx};
        const auto storage_id = rp.PopEnum<FileSys::StorageId>();

        LOG_DEBUG(Service_NCM, "called, storage_id={}", static_cast<u32>(storage_id));

        IPC::ResponseBuilder rb{ctx, 2, 0, 1};
        rb.Push(ResultSuccess);
        rb.PushIpcInterface<IContentStorage>(ctx, system, storage_id);
    }

    void OpenContentMetaDatabase(HLERequestContext& ctx) {
        IPC::RequestParser rp{ctx};
        const auto storage_id = rp.PopEnum<FileSys::StorageId>();

        LOG_DEBUG(Service_NCM, "called, storage_id={}", static_cast<u32>(storage_id));

        IPC::ResponseBuilder rb{ctx, 2, 0, 1};
        rb.Push(ResultSuccess);
        rb.PushIpcInterface<IContentMetaDatabase>(ctx, system, storage_id);
    }
};

class NCM_V final : public ServiceFramework<NCM_V> {
public:
    explicit NCM_V(Core::System& system_)
        : ServiceFramework{system_, "ncm:v"}
    {
        static const FunctionInfo functions[] = {
            {0, nullptr, "GetSystemVersion"},
        };
        RegisterHandlers(functions);
    }
};

void LoopProcess(Core::System& system) {
    auto server_manager = std::make_unique<ServerManager>(system);

    server_manager->RegisterNamedService("lr", std::make_shared<LR>(system));
    server_manager->RegisterNamedService("ncm", std::make_shared<NCM>(system));
    if (1 /* not retail */) {
        server_manager->RegisterNamedService("ncm:v", std::make_shared<NCM_V>(system));
    }
    ServerManager::RunServer(std::move(server_manager));
}

} // namespace Service::NCM
