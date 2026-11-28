// SPDX-FileCopyrightText: Copyright 2025 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

// SPDX-FileCopyrightText: Copyright 2018 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "common/fs/path_util.h"
#include "common/hex_util.h"
#include "core/core.h"
#include "core/file_sys/vfs/vfs.h"
#include "core/file_sys/savedata_factory.h"
#include "core/hle/service/cmif_serialization.h"
#include "core/hle/service/filesystem/fsp/fs_i_save_data_info_reader.h"
#include "core/hle/service/filesystem/save_data_controller.h"

namespace Service::FileSystem {

ISaveDataInfoReader::ISaveDataInfoReader(
    Core::System& system_, std::shared_ptr<SaveDataController> save_data_controller_)
    : ServiceFramework{system_, "ISaveDataInfoReader"},
      save_data_controller{std::move(save_data_controller_)} {
    static const FunctionInfo functions[] = {
        {0, D<&ISaveDataInfoReader::ReadSaveDataInfo>, "ReadSaveDataInfo"},
    };
    RegisterHandlers(functions);

    // Encore currently has three distinct backing save-data spaces. SdUser is
    // collapsed onto User by SaveDataFactory, while SdSystem/ProperSystem/SafeMode
    // have no backing path and must not be probed (their factory path asserts).
    FindAllSaves(FileSys::SaveDataSpaceId::System);
    FindAllSaves(FileSys::SaveDataSpaceId::User);
    FindAllSaves(FileSys::SaveDataSpaceId::Temporary);
}

ISaveDataInfoReader::ISaveDataInfoReader(Core::System& system_,
                                         std::shared_ptr<SaveDataController> save_data_controller_,
                                         FileSys::SaveDataSpaceId space, bool cache_only,
                                         u64 cache_program_id)
    : ServiceFramework{system_, "ISaveDataInfoReader"}, save_data_controller{
                                                            save_data_controller_} {
    static const FunctionInfo functions[] = {
        {0, D<&ISaveDataInfoReader::ReadSaveDataInfo>, "ReadSaveDataInfo"},
    };
    RegisterHandlers(functions);

    if (cache_only) {
        FindCacheStorageSaves(space, cache_program_id);
    } else {
        FindAllSaves(space);
    }
}

ISaveDataInfoReader::ISaveDataInfoReader(
    Core::System& system_, std::shared_ptr<SaveDataController> save_data_controller_,
    FileSys::SaveDataSpaceId space, const FileSys::SaveDataFilter& filter,
    u64 current_program_id)
    : ISaveDataInfoReader(system_, std::move(save_data_controller_), space) {
    const auto requested_program_id =
        filter.attribute.program_id == 0 ? current_program_id : filter.attribute.program_id;

    std::erase_if(info, [&](const SaveDataInfo& entry) {
        if (filter.use_program_id && static_cast<u64>(entry.title_id) != requested_program_id) {
            return true;
        }
        if (filter.use_save_data_type && entry.type != filter.attribute.type) {
            return true;
        }
        if (filter.use_user_id &&
            std::memcmp(entry.user_id.data(), filter.attribute.user_id.data(),
                        entry.user_id.size()) != 0) {
            return true;
        }
        if (filter.use_save_data_id &&
            static_cast<u64>(entry.save_id_unknown) != filter.attribute.system_save_data_id) {
            return true;
        }
        if (filter.use_index && static_cast<u16>(entry.index) != filter.attribute.index) {
            return true;
        }
        if (filter.rank == FileSys::SaveDataRank::Primary &&
            entry.rank == FileSys::SaveDataRank::Secondary) {
            return true;
        }
        return false;
    });
}

ISaveDataInfoReader::~ISaveDataInfoReader() = default;

static u64 stoull_be(std::string_view str) {
    if (str.size() != 16) {
        return 0;
    }

    const auto bytes = Common::HexStringToArray<0x8>(str);
    u64 out{};
    std::memcpy(&out, bytes.data(), sizeof(u64));

    return Common::swap64(out);
}

u64 GetPublishedSaveDataId(Core::System& system, const FileSys::VirtualDir& save_directory) {
    const auto save_root = system.GetFilesystem()->OpenDirectory(
        Common::FS::GetEdenPathString(Common::FS::EdenPath::SaveDir), FileSys::OpenMode::ReadWrite);
    if (save_root == nullptr) {
        LOG_ERROR(Service_FS, "Failed to open configured SaveDir for save-data index");
        return 0;
    }
    const auto save_data_id =
        FileSys::SaveDataFactory::EnsureSaveDataId(save_root, save_directory);
    if (!save_data_id.has_value()) {
        LOG_ERROR(Service_FS, "Failed to resolve published ID for save {}", save_directory->GetFullPath());
        return 0;
    }
    return *save_data_id;
}


Result ISaveDataInfoReader::ReadSaveDataInfo(
    Out<u64> out_count, OutArray<SaveDataInfo, BufferAttr_HipcMapAlias> out_entries) {
    LOG_DEBUG(Service_FS, "called");

    // Calculate how many entries we can fit in the output buffer
    const u64 count_entries = out_entries.size();

    // Cap at total number of entries.
    const u64 actual_entries = (std::min)(count_entries, info.size() - next_entry_index);

    // Determine data start and end
    const auto* begin = reinterpret_cast<u8*>(info.data() + next_entry_index);
    const auto* end = reinterpret_cast<u8*>(info.data() + next_entry_index + actual_entries);
    const auto range_size = static_cast<std::size_t>(std::distance(begin, end));

    next_entry_index += actual_entries;

    // Write the data to memory
    std::memcpy(out_entries.data(), begin, range_size);
    *out_count = actual_entries;

    R_SUCCEED();
}

std::optional<ISaveDataInfoReader::SaveDataInfo> ISaveDataInfoReader::FindSaveDataInfo(
    const FileSys::SaveDataFilter& filter, u64 current_program_id) const {
    const auto requested_program_id =
        filter.attribute.program_id == 0 ? current_program_id : filter.attribute.program_id;
    const auto matches = [&](const SaveDataInfo& entry) {
        if (filter.use_program_id && static_cast<u64>(entry.title_id) != requested_program_id) {
            return false;
        }
        if (filter.use_save_data_type && entry.type != filter.attribute.type) {
            return false;
        }
        if (filter.use_user_id &&
            std::memcmp(entry.user_id.data(), filter.attribute.user_id.data(),
                        entry.user_id.size()) != 0) {
            return false;
        }
        if (filter.use_save_data_id &&
            static_cast<u64>(entry.save_id_unknown) != filter.attribute.system_save_data_id) {
            return false;
        }
        if (filter.use_index && static_cast<u16>(entry.index) != filter.attribute.index) {
            return false;
        }
        if (filter.rank == FileSys::SaveDataRank::Primary &&
            entry.rank == FileSys::SaveDataRank::Secondary) {
            return false;
        }
        return true;
    };
    const auto it = std::find_if(info.begin(), info.end(), matches);
    return it == info.end() ? std::nullopt : std::optional<SaveDataInfo>{*it};
}

void ISaveDataInfoReader::FindAllSaves(FileSys::SaveDataSpaceId space) {
    FileSys::VirtualDir save_root{};
    const auto result = save_data_controller->OpenSaveDataSpace(&save_root, space);

    if (result != ResultSuccess || save_root == nullptr) {
        LOG_ERROR(Service_FS, "The save root for the space_id={:02X} was invalid!", space);
        return;
    }

    for (const auto& type : save_root->GetSubdirectories()) {
        if (type->GetName() == "save") {
            FindNormalSaves(space, type);
        } else if (space == FileSys::SaveDataSpaceId::Temporary) {
            FindTemporaryStorageSaves(space, type);
        }
    }
    if (space == FileSys::SaveDataSpaceId::User || space == FileSys::SaveDataSpaceId::SdUser) {
        FindCacheStorageSaves(space, 0);
    }
}

void ISaveDataInfoReader::FindNormalSaves(FileSys::SaveDataSpaceId space,
                                          const FileSys::VirtualDir& type) {
    for (const auto& save_id : type->GetSubdirectories()) {
        for (const auto& user_id : save_id->GetSubdirectories()) {
            // Skip non user id subdirectories
            if (user_id->GetName().size() != 0x20) {
                continue;
            }

            const auto save_id_numeric = stoull_be(save_id->GetName());
            auto user_id_numeric = Common::HexStringToArray<0x10>(user_id->GetName());
            std::reverse(user_id_numeric.begin(), user_id_numeric.end());

            if (save_id_numeric != 0) {
                // System Save Data
                info.emplace_back(SaveDataInfo{
                    save_id_numeric,
                    space,
                    FileSys::SaveDataType::System,
                    {},
                    user_id_numeric,
                    save_id_numeric,
                    0,
                    user_id->GetSize(),
                    {},
                    {},
                });

                continue;
            }

            for (const auto& title_id : user_id->GetSubdirectories()) {
                const auto device = std::all_of(user_id_numeric.begin(), user_id_numeric.end(),
                                                [](u8 val) { return val == 0; });
                info.emplace_back(SaveDataInfo{
                    GetPublishedSaveDataId(system, title_id),
                    space,
                    device ? FileSys::SaveDataType::Device : FileSys::SaveDataType::Account,
                    {},
                    user_id_numeric,
                    save_id_numeric,
                    stoull_be(title_id->GetName()),
                    title_id->GetSize(),
                    {},
                    {},
                });
            }
        }
    }
}

void ISaveDataInfoReader::FindCacheStorageSaves(FileSys::SaveDataSpaceId space, u64 program_id) {
    FileSys::VirtualDir save_root{};
    const auto result = save_data_controller->OpenSaveDataSpace(&save_root, space);
    if (result != ResultSuccess || save_root == nullptr) {
        LOG_ERROR(Service_FS, "The cache save root for space_id={:02X} was invalid!", space);
        return;
    }

    const auto save_dir = save_root->GetSubdirectory("save");
    const auto cache_dir = save_dir != nullptr ? save_dir->GetSubdirectory("cache") : nullptr;
    if (cache_dir == nullptr) {
        return;
    }

    const auto parse_index = [](std::string_view name) -> std::optional<u16> {
        if (name.size() == 16) {
            return 0;
        }
        if (name.size() != 21 || name[16] != '_') {
            return std::nullopt;
        }

        u16 index{};
        for (const char c : name.substr(17)) {
            u16 nibble{};
            if (c >= '0' && c <= '9') {
                nibble = static_cast<u16>(c - '0');
            } else if (c >= 'A' && c <= 'F') {
                nibble = static_cast<u16>(c - 'A' + 10);
            } else if (c >= 'a' && c <= 'f') {
                nibble = static_cast<u16>(c - 'a' + 10);
            } else {
                return std::nullopt;
            }
            index = static_cast<u16>((index << 4) | nibble);
        }
        return index;
    };

    for (const auto& title_dir : cache_dir->GetSubdirectories()) {
        if (title_dir == nullptr) {
            continue;
        }

        const auto name = title_dir->GetName();
        const auto index = parse_index(name);
        if (!index) {
            continue;
        }

        const auto title_name = std::string_view{name}.substr(0, 16);
        const auto title_id = stoull_be(title_name);
        if (program_id != 0 && title_id != program_id) {
            continue;
        }
        info.emplace_back(SaveDataInfo{
            GetPublishedSaveDataId(system, title_dir),
            space,
            FileSys::SaveDataType::Cache,
            {},
            {},
            0,
            title_id,
            title_dir->GetSize(),
            *index,
            {},
            {},
        });
    }
}

void ISaveDataInfoReader::FindTemporaryStorageSaves(FileSys::SaveDataSpaceId space,
                                                    const FileSys::VirtualDir& type) {
    for (const auto& user_id : type->GetSubdirectories()) {
        // Skip non user id subdirectories
        if (user_id->GetName().size() != 0x20) {
            continue;
        }
        for (const auto& title_id : user_id->GetSubdirectories()) {
            if (!title_id->GetFiles().empty() || !title_id->GetSubdirectories().empty()) {
                auto user_id_numeric = Common::HexStringToArray<0x10>(user_id->GetName());
                std::reverse(user_id_numeric.begin(), user_id_numeric.end());

                info.emplace_back(SaveDataInfo{
                    0,
                    space,
                    FileSys::SaveDataType::Temporary,
                    {},
                    user_id_numeric,
                    stoull_be(type->GetName()),
                    stoull_be(title_id->GetName()),
                    title_id->GetSize(),
                    {},
                    {},
                });
            }
        }
    }
}

} // namespace Service::FileSystem
