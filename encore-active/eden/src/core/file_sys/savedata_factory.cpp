// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

// SPDX-FileCopyrightText: Copyright 2018 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <limits>
#include <mutex>

#include "common/assert.h"
#include "common/common_types.h"
#include "common/logging.h"
#include "common/uuid.h"
#include "core/core.h"
#include "core/file_sys/registered_cache.h"
#include "core/file_sys/savedata_factory.h"
#include "core/file_sys/vfs/vfs.h"

namespace FileSys {

namespace {

constexpr std::string_view SaveDataIndexCounterFileName = ".eden_save_index_last_published_id";
constexpr std::string_view SaveDataIdFilePrefix = ".eden_save_id_";
std::mutex save_data_index_mutex;

std::string GetSaveDataIdFileName(const VirtualDir& save_directory) {
    return fmt::format("{}{}", SaveDataIdFilePrefix, save_directory->GetName());
}

std::optional<u64> ReadSaveDataIdUnlocked(const VirtualDir& save_directory) {
    if (save_directory == nullptr) {
        return std::nullopt;
    }
    const auto parent = save_directory->GetParentDirectory();
    if (parent == nullptr) {
        return std::nullopt;
    }
    const auto file = parent->GetFile(GetSaveDataIdFileName(save_directory));
    if (file == nullptr || file->GetSize() != sizeof(u64)) {
        return std::nullopt;
    }
    u64 value{};
    if (file->ReadObject(&value) != sizeof(value) || value == 0) {
        return std::nullopt;
    }
    return value;
}

bool ShouldSaveDataBeAutomaticallyCreated(SaveDataSpaceId space, const SaveDataAttribute& attr) {
    return attr.type == SaveDataType::Cache || attr.type == SaveDataType::Temporary ||
           (space == SaveDataSpaceId::User && ///< Normal Save Data -- Current Title & User
            (attr.type == SaveDataType::Account || attr.type == SaveDataType::Device) &&
            attr.program_id == 0 && attr.system_save_data_id == 0);
}

std::string GetFutureSaveDataPath(SaveDataSpaceId space_id, SaveDataType type, u64 title_id,
                                  u128 user_id) {
    // Only detect nand user saves.
    const auto space_id_path = [space_id]() -> std::string_view {
        switch (space_id) {
        case SaveDataSpaceId::User:
            return "/user/save";
        default:
            return "";
        }
    }();

    if (space_id_path.empty()) {
        return "";
    }

    Common::UUID uuid;
    std::memcpy(uuid.uuid.data(), user_id.data(), sizeof(Common::UUID));

    // Only detect account/device saves from the future location.
    switch (type) {
    case SaveDataType::Account:
        return fmt::format("{}/account/{}/{:016X}/0", space_id_path, uuid.RawString(), title_id);
    case SaveDataType::Device:
        return fmt::format("{}/device/{:016X}/0", space_id_path, title_id);
    default:
        return "";
    }
}

} // Anonymous namespace

SaveDataFactory::SaveDataFactory(Core::System& system_, ProgramId program_id_,
                                 VirtualDir save_directory_)
    : system{system_}, program_id{program_id_}, dir{std::move(save_directory_)} {
    // Temporary save storage is prepared once during application filesystem
    // initialization, not on every (possibly bundled) program registration.
}

SaveDataFactory::~SaveDataFactory() = default;

std::string SaveDataFactory::GetSaveDataPath(SaveDataSpaceId space, SaveDataType type, u64 title_id, u128 user_id, u64 save_id) const {
    if (type == SaveDataType::Account || type == SaveDataType::Device) {
        const auto requested_id = title_id != 0 ? title_id : program_id;
        const auto parent_id = system.GetContentProvider().GetParentApplicationId(requested_id);
        title_id = parent_id.value_or(requested_id);
    }
    return GetFullPath(program_id, dir, space, type, title_id, user_id, save_id);
}

std::optional<u64> SaveDataFactory::ReadSaveDataId(const VirtualDir& save_directory) {
    std::scoped_lock lock{save_data_index_mutex};
    return ReadSaveDataIdUnlocked(save_directory);
}

std::optional<u64> SaveDataFactory::EnsureSaveDataId(const VirtualDir& root_directory,
                                                     const VirtualDir& save_directory) {
    if (root_directory == nullptr || save_directory == nullptr) {
        return std::nullopt;
    }

    std::scoped_lock lock{save_data_index_mutex};
    if (const auto existing = ReadSaveDataIdUnlocked(save_directory); existing.has_value()) {
        return existing;
    }

    auto counter_file = root_directory->GetFile(SaveDataIndexCounterFileName);
    u64 last_published_id{};
    if (counter_file != nullptr) {
        if (counter_file->GetSize() != sizeof(last_published_id) ||
            counter_file->ReadObject(&last_published_id) != sizeof(last_published_id)) {
            LOG_ERROR(Service_FS, "Invalid persisted save-data index counter");
            return std::nullopt;
        }
    } else {
        counter_file = root_directory->CreateFile(SaveDataIndexCounterFileName);
        if (counter_file == nullptr) {
            return std::nullopt;
        }
    }

    if (last_published_id == (std::numeric_limits<u64>::max)()) {
        return std::nullopt;
    }
    const u64 save_data_id = last_published_id + 1;

    // Persist the monotonic counter first. A failed sidecar write may leave a gap,
    // but a published ID must never be reused.
    if (!counter_file->Resize(sizeof(save_data_id)) ||
        counter_file->WriteObject(save_data_id) != sizeof(save_data_id)) {
        return std::nullopt;
    }

    const auto parent = save_directory->GetParentDirectory();
    if (parent == nullptr) {
        return std::nullopt;
    }
    auto id_file = parent->GetFile(GetSaveDataIdFileName(save_directory));
    if (id_file == nullptr) {
        id_file = parent->CreateFile(GetSaveDataIdFileName(save_directory));
    }
    if (id_file == nullptr || !id_file->Resize(sizeof(save_data_id)) ||
        id_file->WriteObject(save_data_id) != sizeof(save_data_id)) {
        return std::nullopt;
    }

    return save_data_id;
}

VirtualDir SaveDataFactory::Create(SaveDataSpaceId space, const SaveDataAttribute& meta) const {
    const auto storage_id = meta.type == SaveDataType::Cache ? static_cast<u64>(meta.index)
                                                             : meta.system_save_data_id;
    const auto save_path =
        GetSaveDataPath(space, meta.type, meta.program_id, meta.user_id, storage_id);
    const auto save_directory = dir->CreateDirectoryRelative(save_path);

    const bool uses_published_id =
        meta.system_save_data_id == 0 &&
        (meta.type == SaveDataType::Account || meta.type == SaveDataType::Device ||
         meta.type == SaveDataType::Cache);
    if (save_directory != nullptr && uses_published_id &&
        !EnsureSaveDataId(dir, save_directory).has_value()) {
        LOG_ERROR(Service_FS, "Failed to publish save-data ID for {}", save_path);
    }

    return save_directory;
}

VirtualDir SaveDataFactory::Open(SaveDataSpaceId space, const SaveDataAttribute& meta) const {

    const auto storage_id = meta.type == SaveDataType::Cache ? static_cast<u64>(meta.index)
                                                         : meta.system_save_data_id;
    const auto save_directory =
        GetSaveDataPath(space, meta.type, meta.program_id, meta.user_id, storage_id);

    auto out = dir->GetDirectoryRelative(save_directory);

    if (out == nullptr && (ShouldSaveDataBeAutomaticallyCreated(space, meta) && auto_create)) {
        return Create(space, meta);
    }

    return out;
}

VirtualDir SaveDataFactory::GetSaveDataSpaceDirectory(SaveDataSpaceId space) const {
    return dir->GetDirectoryRelative(GetSaveDataSpaceIdPath(space));
}

std::string SaveDataFactory::GetSaveDataSpaceIdPath(SaveDataSpaceId space) {
    switch (space) {
    case SaveDataSpaceId::System:
        return "/system/";
    case SaveDataSpaceId::User:
    case SaveDataSpaceId::SdUser:
        return "/user/";
    case SaveDataSpaceId::Temporary:
        return "/temp/";
    default:
        ASSERT_MSG(false, "Unrecognized SaveDataSpaceId: {:02X}", static_cast<u8>(space));
        return "/unrecognized/"; ///< To prevent corruption when ignoring asserts.
    }
}

std::string SaveDataFactory::GetFullPath(ProgramId program_id, VirtualDir dir,
                                         SaveDataSpaceId space, SaveDataType type, u64 title_id,
                                         u128 user_id, u64 save_id) {
    // According to switchbrew, if a save is of type SaveData and the title id field is 0, it should
    // be interpreted as the title id of the current process.
    if (type == SaveDataType::Account || type == SaveDataType::Device ||
        type == SaveDataType::Cache) {
        if (title_id == 0) {
            title_id = program_id;
        }
    }

    // For compat with a future impl.
    if (std::string future_path =
            GetFutureSaveDataPath(space, type, title_id & ~(0xFFULL), user_id);
        !future_path.empty()) {
        // Check if this location exists, and prefer it over the old.
        if (const auto future_dir = dir->GetDirectoryRelative(future_path); future_dir != nullptr) {
            LOG_INFO(Service_FS, "Using save at new location: {}", future_path);
            return future_path;
        }
    }

    std::string out = GetSaveDataSpaceIdPath(space);

    switch (type) {
    case SaveDataType::System:
        return fmt::format("{}save/{:016X}/{:016X}{:016X}", out, save_id, user_id[1], user_id[0]);
    case SaveDataType::Account:
    case SaveDataType::Device:
        return fmt::format("{}save/{:016X}/{:016X}{:016X}/{:016X}", out, 0, user_id[1], user_id[0],
                           title_id);
    case SaveDataType::Temporary:
        return fmt::format("{}{:016X}/{:016X}{:016X}/{:016X}", out, 0, user_id[1], user_id[0],
                           title_id);
    case SaveDataType::Cache:
        if (save_id == 0) {
            return fmt::format("{}save/cache/{:016X}", out, title_id);
        }
        return fmt::format("{}save/cache/{:016X}_{:04X}", out, title_id, save_id);
    default:
        ASSERT_MSG(false, "Unrecognized SaveDataType: {:02X}", static_cast<u8>(type));
        return fmt::format("{}save/unknown_{:X}/{:016X}", out, static_cast<u8>(type), title_id);
    }
}

std::string SaveDataFactory::GetUserGameSaveDataRoot(u128 user_id, bool future) {
    if (future) {
        Common::UUID uuid;
        std::memcpy(uuid.uuid.data(), user_id.data(), sizeof(Common::UUID));
        return fmt::format("/user/save/account/{}", uuid.RawString());
    }
    return fmt::format("/user/save/{:016X}/{:016X}{:016X}", 0, user_id[1], user_id[0]);
}

SaveDataSize SaveDataFactory::ReadSaveDataSize(SaveDataType type, u64 title_id,
                                               u128 user_id) const {
    const auto path = GetSaveDataPath(SaveDataSpaceId::User, type, title_id, user_id, 0);
    const auto relative_dir = GetOrCreateDirectoryRelative(dir, path);

    const auto size_file = relative_dir->GetFile(GetSaveDataSizeFileName());
    if (size_file == nullptr || size_file->GetSize() < sizeof(SaveDataSize)) {
        return {0, 0};
    }

    SaveDataSize out;
    if (size_file->ReadObject(&out) != sizeof(SaveDataSize)) {
        return {0, 0};
    }

    return out;
}

void SaveDataFactory::WriteSaveDataSize(SaveDataType type, u64 title_id, u128 user_id,
                                        SaveDataSize new_value) const {
    const auto path = GetSaveDataPath(SaveDataSpaceId::User, type, title_id, user_id, 0);
    const auto relative_dir = GetOrCreateDirectoryRelative(dir, path);

    const auto size_file = relative_dir->CreateFile(GetSaveDataSizeFileName());
    if (size_file == nullptr) {
        return;
    }

    size_file->Resize(sizeof(SaveDataSize));
    size_file->WriteObject(new_value);
}

void SaveDataFactory::SetAutoCreate(bool state) {
    auto_create = state;
}

} // namespace FileSys
