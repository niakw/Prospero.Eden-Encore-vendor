// SPDX-FileCopyrightText: Copyright 2020 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <array>

#include "common/container/unordered_map.h"

#include "core/hle/service/cmif_types.h"
#include "core/hle/service/service.h"

namespace Core {
class System;
}

namespace Service::Capture {
class AlbumManager;
enum class AlbumStorage : u8;
enum class ShimLibraryVersion : u64;

struct ControlApplicationId {
    u64 application_id{};
    u8 unknown_08{};
    u8 unknown_09{};
    std::array<u8, 6> reserved{};
};
static_assert(sizeof(ControlApplicationId) == 0x10);

class IAlbumControlService final : public ServiceFramework<IAlbumControlService> {
public:
    explicit IAlbumControlService(Core::System& system_,
                                  std::shared_ptr<AlbumManager> album_manager);
    ~IAlbumControlService() override;

private:
    Result SetShimLibraryVersion(ShimLibraryVersion library_version,
                                 ClientAppletResourceUserId aruid);
    Result NotifyAlbumStorageIsAvailable(AlbumStorage storage);
    Result NotifyAlbumStorageIsUnavailable(AlbumStorage storage);
    Result RegisterAppletResourceUserId(u64 aruid, const ControlApplicationId& application_id);
    Result UnregisterAppletResourceUserId(u64 aruid, const ControlApplicationId& application_id);
    Result GetApplicationIdFromAruid(Out<ControlApplicationId> out_application_id, u64 aruid);

    std::shared_ptr<AlbumManager> manager = nullptr;
    ::Common::unordered_map<u64, ControlApplicationId> registered_applications;
};

} // namespace Service::Capture
