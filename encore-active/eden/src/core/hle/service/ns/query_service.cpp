// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

// SPDX-FileCopyrightText: Copyright 2021 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <algorithm>
#include <ctime>
#include <limits>

#include "common/logging.h"
#include "common/uuid.h"
#include "core/hle/service/cmif_serialization.h"
#include "core/hle/service/ipc_helpers.h"
#include "core/hle/service/ns/query_service.h"
#include "core/hle/service/service.h"
#include "core/launch_timestamp_cache.h"
#include "frontend_common/play_time_manager.h"

namespace Service::NS {

IQueryService::IQueryService(Core::System& system_) : ServiceFramework{system_, "pdm:qry"},
    play_time_manager{std::make_unique<PlayTime::PlayTimeManager>()} {
    // clang-format off
    static const FunctionInfo functions[] = {
        {0, &IQueryService::RejectUnavailablePlayLogQuery, "QueryAppletEvent"},
        {1, &IQueryService::RejectUnavailablePlayLogQuery, "QueryPlayStatistics"},
        {2, &IQueryService::RejectUnavailablePlayLogQuery, "QueryPlayStatisticsByUserAccountId"},
        {3, &IQueryService::RejectUnavailablePlayLogQuery, "QueryPlayStatisticsByNetworkServiceAccountId"},
        {4, D<&IQueryService::QueryPlayStatisticsByApplicationId>, "QueryPlayStatisticsByApplicationId"},
        {5, D<&IQueryService::QueryPlayStatisticsByApplicationIdAndUserAccountId>, "QueryPlayStatisticsByApplicationIdAndUserAccountId"},
        {6, &IQueryService::RejectUnavailablePlayLogQuery, "QueryPlayStatisticsByApplicationIdAndNetworkServiceAccountId"},
        {7, D<&IQueryService::QueryLastPlayTimeV0>, "QueryLastPlayTimeV0"},
        {8, &IQueryService::RejectUnavailablePlayLogQuery, "QueryPlayEvent"},
        {9, &IQueryService::RejectUnavailablePlayLogQuery, "GetAvailablePlayEventRange"},
        {10, &IQueryService::RejectUnavailablePlayLogQuery, "QueryAccountEvent"},
        {11, &IQueryService::RejectUnavailablePlayLogQuery, "QueryAccountPlayEvent"},
        {12, &IQueryService::RejectUnavailablePlayLogQuery, "GetAvailableAccountPlayEventRange"},
        {13, D<&IQueryService::QueryApplicationPlayStatisticsForSystemV0>, "QueryApplicationPlayStatisticsForSystemV0"},
        {16, D<&IQueryService::QueryApplicationPlayStatisticsByUserAccountIdForSystemV0>, "QueryApplicationPlayStatisticsByUserAccountIdForSystemV0"},
        {17, D<&IQueryService::QueryLastPlayTime>, "QueryLastPlayTime"},
        {18, D<&IQueryService::QueryApplicationPlayStatisticsForSystem>, "QueryApplicationPlayStatisticsForSystem"},
        {19, D<&IQueryService::QueryApplicationPlayStatisticsByUserAccountIdForSystem>, "QueryApplicationPlayStatisticsByUserAccountIdForSystem"},
        {30, &IQueryService::RejectUnavailablePlayLogQuery, "Cmd30"},
        {31, &IQueryService::RejectUnavailablePlayLogQuery, "Cmd31"},
        {100, &IQueryService::RejectUnavailablePlayLogQuery, "Cmd100"},
        {110, &IQueryService::RejectUnavailablePlayLogQuery, "Cmd110"},
        {118, &IQueryService::RejectUnavailablePlayLogQuery, "Cmd118"},
        {119, &IQueryService::RejectUnavailablePlayLogQuery, "Cmd119"},
    };
    // clang-format on

    RegisterHandlers(functions);
}

IQueryService::~IQueryService() = default;

void IQueryService::RejectUnavailablePlayLogQuery(HLERequestContext& ctx) {
    LOG_DEBUG(Service_NS, "pdm:qry raw play-log query unavailable; local aggregate statistics remain available");
    IPC::ResponseBuilder rb{ctx, 2};
    rb.Push(IPC::ResultNotSupported);
}

Result IQueryService::QueryPlayStatisticsByApplicationId(
    Out<PlayStatistics> out_play_statistics, bool unknown, u64 application_id) {
    const u64 launches = Core::LaunchTimestampCache::GetLaunchCount(application_id);
    const s64 last_timestamp = Core::LaunchTimestampCache::GetLaunchTimestamp(application_id);
    const u64 play_time_ns = play_time_manager->GetPlayTime(application_id) * 1'000'000'000ULL;

    PlayStatistics stats{};
    stats.application_id = application_id;
    if (launches != 0 && last_timestamp > 0) {
        stats.last_timestamp_user = static_cast<u64>(last_timestamp);
        stats.last_timestamp_network = static_cast<u64>(last_timestamp);
    }
    stats.play_time_ns = play_time_ns;
    stats.total_launches = static_cast<u32>((std::min)(launches, static_cast<u64>(UINT32_MAX)));
    *out_play_statistics = stats;

    LOG_DEBUG(Service_NS,
              "called, flag={}, application_id={:016X}, play_time_ns={}, launches={}",
              unknown, application_id, play_time_ns, launches);
    R_SUCCEED();
}

Result IQueryService::QueryPlayStatisticsByApplicationIdAndUserAccountId(
    Out<PlayStatistics> out_play_statistics, bool unknown, u64 application_id, Uid account_id) {
    const u64 launches = Core::LaunchTimestampCache::GetLaunchCount(application_id);
    const s64 last_timestamp = Core::LaunchTimestampCache::GetLaunchTimestamp(application_id);
    const u64 play_time_ns = play_time_manager->GetPlayTime(application_id) * 1'000'000'000ULL;

    PlayStatistics stats{};
    stats.application_id = application_id;
    // Eden currently persists the last launch timestamp and count, not the global PDM event log.
    // Use the persisted timestamp for the most recent user/network observation; leave first/index
    // fields zero rather than fabricating global PDM entry numbers.
    if (launches != 0 && last_timestamp > 0) {
        stats.last_timestamp_user = static_cast<u64>(last_timestamp);
        stats.last_timestamp_network = static_cast<u64>(last_timestamp);
    }
    stats.play_time_ns = play_time_ns;
    stats.total_launches = static_cast<u32>((std::min)(launches, static_cast<u64>(UINT32_MAX)));
    *out_play_statistics = stats;

    LOG_DEBUG(Service_NS,
              "called, flag={}, application_id={:016X}, account_id={}, play_time_ns={}, launches={}",
              unknown, application_id, account_id.uuid.FormattedString(), play_time_ns, launches);
    R_SUCCEED();
}

Result IQueryService::QueryLastPlayTime(
    Out<s32> out_entries, u8 unknown,
    OutArray<LastPlayTime, BufferAttr_HipcMapAlias> out_last_play_times,
    InArray<u64, BufferAttr_HipcMapAlias> application_ids) {
    constexpr s64 HorizonMinuteEpoch = 946598400; // 1999-12-31 00:00 UTC
    const s64 now = static_cast<s64>(std::time(nullptr));
    const size_t count = std::min(out_last_play_times.size(), application_ids.size());
    s32 written = 0;

    for (size_t i = 0; i < count; ++i) {
        const u64 application_id = application_ids[i];
        LastPlayTime entry{};
        entry.application_id = application_id;

        const u64 launches = Core::LaunchTimestampCache::GetLaunchCount(application_id);
        if (launches != 0) {
            const s64 timestamp = Core::LaunchTimestampCache::GetLaunchTimestamp(application_id);
            if (timestamp >= HorizonMinuteEpoch) {
                const u64 horizon_minutes =
                    static_cast<u64>(timestamp - HorizonMinuteEpoch) / 60ULL;
                const u32 encoded_timestamp = static_cast<u32>((std::min)(
                    horizon_minutes, static_cast<u64>(std::numeric_limits<u32>::max())));
                entry.last_timestamp_user = encoded_timestamp;
                entry.last_timestamp_network = encoded_timestamp;

                if (now >= timestamp) {
                    const u64 elapsed_minutes = static_cast<u64>(now - timestamp) / 60ULL;
                    entry.minutes_since_last_played = static_cast<u32>((std::min)(
                        elapsed_minutes, static_cast<u64>(std::numeric_limits<u32>::max())));
                    entry.has_minutes_since_last_played = 1;
                }
            }
        }

        out_last_play_times[i] = entry;
        ++written;
    }

    *out_entries = written;
    LOG_DEBUG(Service_NS, "called, entries={} flag={}", written, unknown);
    R_SUCCEED();
}

Result IQueryService::QueryLastPlayTimeV0(
    Out<s32> out_entries,
    OutArray<LastPlayTime, BufferAttr_HipcMapAlias> out_last_play_times,
    InArray<u64, BufferAttr_HipcMapAlias> application_ids) {
    R_RETURN(QueryLastPlayTime(out_entries, 0, out_last_play_times, application_ids));
}

Result IQueryService::QueryApplicationPlayStatisticsForSystemV0(
    Out<s32> out_entries,
    OutArray<ApplicationPlayStatistics, BufferAttr_HipcMapAlias> out_stats,
    InArray<u64, BufferAttr_HipcMapAlias> application_ids) {
    R_RETURN(QueryApplicationPlayStatisticsForSystem(out_entries, 0, out_stats, application_ids));
}

Result IQueryService::QueryApplicationPlayStatisticsByUserAccountIdForSystemV0(
    Out<s32> out_entries, Common::UUID user_id,
    OutArray<ApplicationPlayStatistics, BufferAttr_HipcMapAlias> out_stats,
    InArray<u64, BufferAttr_HipcMapAlias> application_ids) {
    R_RETURN(QueryApplicationPlayStatisticsByUserAccountIdForSystem(
        out_entries, 0, user_id, out_stats, application_ids));
}

Result IQueryService::QueryApplicationPlayStatisticsForSystem(
    Out<s32> out_entries, u8 flag,
    OutArray<ApplicationPlayStatistics, BufferAttr_HipcMapAlias> out_stats,
    InArray<u64, BufferAttr_HipcMapAlias> application_ids) {
    const size_t count = std::min(out_stats.size(), application_ids.size());
    s32 written = 0;
    for (size_t i = 0; i < count; ++i) {
        const u64 app_id = application_ids[i];
        ApplicationPlayStatistics stats{};
        stats.application_id = app_id;
        stats.play_time_ns = play_time_manager->GetPlayTime(app_id) * 1'000'000'000ULL;
        stats.launch_count = Core::LaunchTimestampCache::GetLaunchCount(app_id);
        out_stats[i] = stats;
        ++written;
    }
    *out_entries = written;
    LOG_DEBUG(Service_NS, "called, entries={} flag={}", written, flag);
    R_SUCCEED();
}

Result IQueryService::QueryApplicationPlayStatisticsByUserAccountIdForSystem(
    Out<s32> out_entries, u8 flag, Common::UUID user_id,
    OutArray<ApplicationPlayStatistics, BufferAttr_HipcMapAlias> out_stats,
    InArray<u64, BufferAttr_HipcMapAlias> application_ids) {
    // well we don't do per-user tracking :>
    return QueryApplicationPlayStatisticsForSystem(out_entries, flag, out_stats, application_ids);
}

} // namespace Service::NS
