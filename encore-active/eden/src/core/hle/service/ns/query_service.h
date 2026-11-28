// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

// SPDX-FileCopyrightText: Copyright 2021 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <array>

#include "common/uuid.h"
#include "core/hle/service/am/am_types.h"
#include "core/hle/service/cmif_types.h"
#include "core/hle/service/ns/ns_types.h"
#include "core/hle/service/service.h"

#include <memory>

namespace PlayTime {
class PlayTimeManager;
}

namespace Service::NS {

// nn::pdm::PlayStatistics for HOS 16.0.0+ (HOS 23 target).
// The former 0x28 layout was PlayStatisticsV1 and is ABI-incompatible with current titles.
struct PlayStatistics {
    u64 application_id{};
    u32 first_entry_index{};
    u32 padding0{};
    u64 first_timestamp_user{};
    u64 first_timestamp_network{};
    u32 last_entry_index{};
    u32 padding1{};
    u64 last_timestamp_user{};
    u64 last_timestamp_network{};
    u64 play_time_ns{};
    u32 total_launches{};
    u32 padding2{};
};
static_assert(sizeof(PlayStatistics) == 0x48, "PlayStatistics is an invalid size");

struct LastPlayTime {
    u64 application_id{};
    u32 last_timestamp_user{};
    u32 last_timestamp_network{};
    u32 minutes_since_last_played{};
    u8 has_minutes_since_last_played{};
    std::array<u8, 3> padding{};
};
static_assert(sizeof(LastPlayTime) == 0x18, "LastPlayTime is an invalid size");

struct ApplicationPlayStatistics {
    u64 application_id{};
    u64 play_time_ns{};
    u64 launch_count{};
};
static_assert(sizeof(ApplicationPlayStatistics) == 0x18, "ApplicationPlayStatistics is an invalid size");

class IQueryService final : public ServiceFramework<IQueryService> {
public:
    explicit IQueryService(Core::System& system_);
    ~IQueryService() override;

private:
    void RejectUnavailablePlayLogQuery(HLERequestContext& ctx);
    Result QueryPlayStatisticsByApplicationId(Out<PlayStatistics> out_play_statistics, bool unknown,
                                              u64 application_id);
    Result QueryPlayStatisticsByApplicationIdAndUserAccountId(
        Out<PlayStatistics> out_play_statistics, bool unknown, u64 application_id, Uid account_id);
    Result QueryLastPlayTime(Out<s32> out_entries, u8 unknown,
                             OutArray<LastPlayTime, BufferAttr_HipcMapAlias> out_last_play_times,
                             InArray<u64, BufferAttr_HipcMapAlias> application_ids);
    Result QueryLastPlayTimeV0(
        Out<s32> out_entries,
        OutArray<LastPlayTime, BufferAttr_HipcMapAlias> out_last_play_times,
        InArray<u64, BufferAttr_HipcMapAlias> application_ids);
    Result QueryApplicationPlayStatisticsForSystemV0(
        Out<s32> out_entries,
        OutArray<ApplicationPlayStatistics, BufferAttr_HipcMapAlias> out_stats,
        InArray<u64, BufferAttr_HipcMapAlias> application_ids);
    Result QueryApplicationPlayStatisticsByUserAccountIdForSystemV0(
        Out<s32> out_entries, Common::UUID user_id,
        OutArray<ApplicationPlayStatistics, BufferAttr_HipcMapAlias> out_stats,
        InArray<u64, BufferAttr_HipcMapAlias> application_ids);
    Result QueryApplicationPlayStatisticsForSystem(
        Out<s32> out_entries, u8 flag,
        OutArray<ApplicationPlayStatistics, BufferAttr_HipcMapAlias> out_stats,
        InArray<u64, BufferAttr_HipcMapAlias> application_ids);
    Result QueryApplicationPlayStatisticsByUserAccountIdForSystem(
        Out<s32> out_entries, u8 flag, Common::UUID user_id,
        OutArray<ApplicationPlayStatistics, BufferAttr_HipcMapAlias> out_stats,
        InArray<u64, BufferAttr_HipcMapAlias> application_ids);

    std::unique_ptr<PlayTime::PlayTimeManager> play_time_manager;
};

} // namespace Service::NS
