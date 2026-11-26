// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

// SPDX-FileCopyrightText: Copyright 2018 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <array>
#include <queue>
#include "common/logging.h"
#include "common/uuid.h"
#include "core/core.h"
#include "core/hle/kernel/k_event.h"
#include "core/hle/service/acc/errors.h"
#include "core/hle/service/friend/friend.h"
#include "core/hle/service/friend/friend_interface.h"
#include "core/hle/service/ipc_helpers.h"
#include "core/hle/service/kernel_helpers.h"
#include "core/hle/service/server_manager.h"

namespace Service::Friend {

class IFriendService final : public ServiceFramework<IFriendService> {
public:
    explicit IFriendService(Core::System& system_)
        : ServiceFramework{system_, "IFriendService"}, service_context{system, "IFriendService"} {
        // clang-format off
        static const FunctionInfo functions[] = {
            {0, &IFriendService::GetCompletionEvent, "GetCompletionEvent"},
            {1, &IFriendService::Cancel, "Cancel"},
            {10100, &IFriendService::GetFriendListIds, "GetFriendListIds"},
            {10101, &IFriendService::GetFriendList, "GetFriendList"},
            {10102, &IFriendService::UpdateFriendInfo, "UpdateFriendInfo"},
            {10110, &IFriendService::GetFriendProfileImage, "GetFriendProfileImage"},
            {10111, &IFriendService::GetFriendProfileImage, "GetFriendProfileImageWithImageSize"}, // 18.0.0+
            {10120, &IFriendService::CheckFriendListAvailability, "CheckFriendListAvailability"},
            {10121, &IFriendService::EnsureFriendListAvailable, "EnsureFriendListAvailable"},
            {10200, &IFriendService::RejectUnavailableNintendoOnline, "SendFriendRequestForApplication"},
            {10211, &IFriendService::RejectUnavailableNintendoOnline, "AddFacedFriendRequestForApplication"},
            {10400, &IFriendService::GetBlockedUserListIds, "GetBlockedUserListIds"},
            {10420, &IFriendService::CheckBlockedUserListAvailability, "CheckBlockedUserListAvailability"},
            {10421, &IFriendService::EnsureBlockedUserListAvailable, "EnsureBlockedUserListAvailable"},
            {10500, &IFriendService::GetProfileList, "GetProfileList"},
            {10501, &IFriendService::GetProfileListV2, "GetProfileListV2"}, // 18.0.0+
            {10600, &IFriendService::DeclareOpenOnlinePlaySession, "DeclareOpenOnlinePlaySession"},
            {10601, &IFriendService::DeclareCloseOnlinePlaySession, "DeclareCloseOnlinePlaySession"},
            {10610, &IFriendService::UpdateUserPresence, "UpdateUserPresence"},
            {10700, &IFriendService::GetPlayHistoryRegistrationKey, "GetPlayHistoryRegistrationKey"},
            {10701, &IFriendService::GetPlayHistoryRegistrationKeyWithNetworkServiceAccountId, "GetPlayHistoryRegistrationKeyWithNetworkServiceAccountId"},
            {10702, &IFriendService::RejectUnavailableNintendoOnline, "AddPlayHistory"},
            {11000, &IFriendService::GetProfileImageUrl, "GetProfileImageUrl"},
            {11001, &IFriendService::RejectUnavailableNintendoOnline, "GetProfileImageUrlV2"}, // 18.0.0+
            {20100, &IFriendService::GetFriendCount, "GetFriendCount"},
            {20101, &IFriendService::GetNewlyFriendCount, "GetNewlyFriendCount"},
            {20102, &IFriendService::GetFriendDetailedInfo, "GetFriendDetailedInfo"},
            {20103, &IFriendService::SyncFriendList, "SyncFriendList"},
            {20104, &IFriendService::RequestSyncFriendList, "RequestSyncFriendList"},
            {20105, &IFriendService::GetFriendListForViewer, "GetFriendListForViewerV1"},
            {20106, &IFriendService::GetFriendDetailedInfo, "UpdateFriendInfoForViewerV1"},
            {20107, &IFriendService::GetFriendDetailedInfo, "GetFriendDetailedInfoV2"}, // 20.0.0+
            {20108, &IFriendService::GetFriendListForViewer, "GetFriendListForViewerV2"}, // 22.0.0+
            {20109, &IFriendService::GetFriendDetailedInfo, "UpdateFriendInfoForViewerV2"}, // 22.0.0+
            {20110, &IFriendService::RejectUnavailableNintendoOnline, "LoadFriendSettingV1"},
            {20111, &IFriendService::RejectUnavailableNintendoOnline, "LoadFriendSettingV2"}, // 22.0.0+
            {20200, &IFriendService::GetReceivedFriendRequestCount, "GetReceivedFriendRequestCount"},
            {20201, &IFriendService::GetFriendRequestListV1, "GetFriendRequestListV1"},
            {20202, &IFriendService::GetFriendRequestListV2, "GetFriendRequestListV2"}, // 20.0.0+
            {20203, &IFriendService::RejectUnavailableNintendoOnline, "GetFriendRequestReceivedNotificationCount"}, // 22.0.0+
            {20300, &IFriendService::GetFriendCandidateList, "GetFriendCandidateList"},
            {20301, &IFriendService::GetNintendoNetworkIdInfo, "GetNintendoNetworkIdInfo"},
            {20302, &IFriendService::RejectUnavailableNintendoOnline, "GetSnsAccountLinkage"}, // 5.0.0-19.0.1
            {20303, &IFriendService::RejectUnavailableNintendoOnline, "GetSnsAccountProfile"}, // 5.0.0-19.0.1
            {20304, &IFriendService::RejectUnavailableNintendoOnline, "GetSnsAccountFriendList"}, // 5.0.0-19.0.1
            {20400, &IFriendService::GetBlockedUserListV1, "GetBlockedUserListV1"},
            {20401, &IFriendService::SyncBlockedUserList, "SyncBlockedUserList"},
            {20402, &IFriendService::GetBlockedUserListV2, "GetBlockedUserListV2"}, // 20.0.0+
            {20500, &IFriendService::RejectUnavailableNintendoOnline, "GetProfileExtraListV1"},
            {20501, &IFriendService::GetRelationship, "GetRelationship"},
            {20502, &IFriendService::GetProfileExtraListV2, "GetProfileExtraListV2"}, // 19.0.0+
            {20600, &IFriendService::RejectUnavailableNintendoOnline, "GetUserPresenceViewV1"},
            {20601, &IFriendService::RejectUnavailableNintendoOnline, "GetUserPresenceViewV2"}, // 19.0.0+
            {20700, &IFriendService::GetPlayHistoryListV1, "GetPlayHistoryListV1"},
            {20701, &IFriendService::GetPlayHistoryStatistics, "GetPlayHistoryStatistics"},
            {20702, &IFriendService::GetPlayHistoryListV2, "GetPlayHistoryListV2"}, // 19.0.0+
            {20800, &IFriendService::RejectUnavailableNintendoOnline, "LoadUserSettingV1"},
            {20801, &IFriendService::SyncUserSetting, "SyncUserSetting"},
            {20802, &IFriendService::RejectUnavailableNintendoOnline, "LoadUserSettingV2"}, // 19.0.0+
            {20900, &IFriendService::RequestListSummaryOverlayNotification, "RequestListSummaryOverlayNotification"},
            {21000, &IFriendService::GetExternalApplicationCatalog, "GetExternalApplicationCatalog"},
            {22000, &IFriendService::GetReceivedFriendInvitationListV1, "GetReceivedFriendInvitationListV1"},
            {22001, &IFriendService::RejectUnavailableNintendoOnline, "GetReceivedFriendInvitationDetailedInfoV1"},
            {22002, &IFriendService::GetReceivedFriendInvitationListV2, "GetReceivedFriendInvitationListV2"}, // 19.0.0+
            {22003, &IFriendService::RejectUnavailableNintendoOnline, "GetReceivedFriendInvitationDetailedInfoV2"}, // 19.0.0+
            {22010, &IFriendService::GetReceivedFriendInvitationCountCache, "GetReceivedFriendInvitationCountCache"},
            {30100, &IFriendService::DropFriendNewlyFlags, "DropFriendNewlyFlags"},
            {30101, &IFriendService::RejectUnavailableNintendoOnline, "DeleteFriend"},
            {30110, &IFriendService::DropFriendNewlyFlag, "DropFriendNewlyFlag"},
            {30120, &IFriendService::RejectUnavailableNintendoOnline, "ChangeFriendFavoriteFlag"},
            {30121, &IFriendService::RejectUnavailableNintendoOnline, "ChangeFriendOnlineNotificationFlag"},
            {30130, &IFriendService::RejectUnavailableNintendoOnline, "SetFriendNote"}, // 22.0.0+
            {30131, &IFriendService::RejectUnavailableNintendoOnline, "RequestUploadPendingNote"}, // 22.0.0+
            {30190, &IFriendService::RejectUnavailableNintendoOnline, "RequestSyncLocalUpdates"}, // 22.0.0+
            {30200, &IFriendService::RejectUnavailableNintendoOnline, "SendFriendRequest"},
            {30201, &IFriendService::RejectUnavailableNintendoOnline, "SendFriendRequestWithApplicationInfoV1"},
            {30202, &IFriendService::RejectUnavailableNintendoOnline, "CancelFriendRequest"},
            {30203, &IFriendService::RejectUnavailableNintendoOnline, "AcceptFriendRequest"},
            {30204, &IFriendService::RejectUnavailableNintendoOnline, "RejectFriendRequest"},
            {30205, &IFriendService::RejectUnavailableNintendoOnline, "ReadFriendRequest"},
            {30210, &IFriendService::RejectUnavailableNintendoOnline, "GetFacedFriendRequestRegistrationKey"},
            {30211, &IFriendService::RejectUnavailableNintendoOnline, "AddFacedFriendRequest"},
            {30212, &IFriendService::RejectUnavailableNintendoOnline, "CancelFacedFriendRequest"},
            {30213, &IFriendService::RejectUnavailableNintendoOnline, "GetFacedFriendRequestProfileImage"},
            {30214, &IFriendService::RejectUnavailableNintendoOnline, "GetFacedFriendRequestProfileImageFromPath"},
            {30215, &IFriendService::RejectUnavailableNintendoOnline, "SendFriendRequestWithExternalApplicationCatalogId"},
            {30216, &IFriendService::RejectUnavailableNintendoOnline, "ResendFacedFriendRequest"},
            {30217, &IFriendService::RejectUnavailableNintendoOnline, "SendFriendRequestWithNintendoNetworkIdInfo"},
            {30218, &IFriendService::RejectUnavailableNintendoOnline, "SendFriendRequestWithApplicationInfoV2"}, // 20.0.0+
            {30300, &IFriendService::RejectUnavailableNintendoOnline, "GetSnsAccountLinkPageUrl"}, // 5.0.0-19.0.1
            {30301, &IFriendService::RejectUnavailableNintendoOnline, "UnlinkSnsAccount"}, // 5.0.0-19.0.1
            {30400, &IFriendService::RejectUnavailableNintendoOnline, "BlockUser"},
            {30401, &IFriendService::RejectUnavailableNintendoOnline, "BlockUserWithApplicationInfoV1"},
            {30402, &IFriendService::RejectUnavailableNintendoOnline, "UnblockUser"},
            {30403, &IFriendService::RejectUnavailableNintendoOnline, "BlockUserWithApplicationInfoV2"}, // 20.0.0+
            {30500, &IFriendService::RejectUnavailableNintendoOnline, "GetProfileExtraFromFriendCodeV1"},
            {30501, &IFriendService::RejectUnavailableNintendoOnline, "GetProfileExtraFromFriendCodeV2"}, // 19.0.0+
            {30700, &IFriendService::DeletePlayHistory, "DeletePlayHistory"},
            {30701, &IFriendService::RejectUnavailableNintendoOnline, "AddPlayHistoryWithApplication"}, // 19.0.0+
            {30810, &IFriendService::ChangePresencePermission, "ChangePresencePermission"},
            {30811, &IFriendService::ChangeFriendRequestReception, "ChangeFriendRequestReception"},
            {30812, &IFriendService::ChangePlayLogPermission, "ChangePlayLogPermission"},
            {30820, &IFriendService::RejectUnavailableNintendoOnline, "IssueFriendCode"},
            {30830, &IFriendService::ClearPlayLog, "ClearPlayLog"},
            {30900, &IFriendService::RejectUnavailableNintendoOnline, "SendFriendInvitationV1"},
            {30901, &IFriendService::RejectUnavailableNintendoOnline, "SendFriendInvitationV2"}, // 19.0.0+
            {30910, &IFriendService::RejectUnavailableNintendoOnline, "ReadFriendInvitation"},
            {30911, &IFriendService::RejectUnavailableNintendoOnline, "ReadAllFriendInvitations"},
            {31000, &IFriendService::RejectUnavailableNintendoOnline, "OpenUser"}, // 19.0.0+
            {40100, &IFriendService::RejectUnavailableNintendoOnline, "DeleteFriendListCache"},
            {40400, &IFriendService::RejectUnavailableNintendoOnline, "DeleteBlockedUserListCache"},
            {49900, &IFriendService::DeleteNetworkServiceAccountCache, "DeleteNetworkServiceAccountCache"},
        };
        // clang-format on

        RegisterHandlers(functions);

        completion_event = service_context.CreateEvent("IFriendService:CompletionEvent");
    }

    ~IFriendService() override {
        service_context.CloseEvent(completion_event);
    }

private:
    enum class PresenceFilter : u32 {
        None = 0,
        Online = 1,
        OnlinePlay = 2,
        OnlineOrOnlinePlay = 3,
    };

    struct SizedFriendFilter {
        PresenceFilter presence;
        u8 is_favorite;
        u8 same_app;
        u8 same_app_played;
        u8 arbitrary_app_played;
        u64 group_id;
    };
    static_assert(sizeof(SizedFriendFilter) == 0x10, "SizedFriendFilter is an invalid size");

    struct FriendsUserSetting {
        Common::UUID uuid;
        u32 presence_permission;
        u32 play_log_permission;
        u64 friend_request_reception;
        char friend_code[0x20];
        u64 friend_code_next_issuable_time;
        u8 unk_x48[0x7B8];
    };
    static_assert(sizeof(FriendsUserSetting) == 0x800, "FriendsUserSetting is an invalid size");

    struct LocalUserSetting {
        Common::UUID uuid{};
        u32 presence_permission{2};
        u32 play_log_permission{5};
        u64 friend_request_reception{1};
    };

    LocalUserSetting& GetLocalUserSetting(const Common::UUID& uuid) {
        for (auto& setting : local_user_settings) {
            if (setting.uuid == uuid) {
                return setting;
            }
        }
        local_user_settings.push_back(LocalUserSetting{.uuid = uuid});
        return local_user_settings.back();
    }

    void GetCompletionEvent(HLERequestContext& ctx) {
        LOG_DEBUG(Service_Friend, "called");

        auto& readable_event = completion_event->GetReadableEvent();

        IPC::ResponseBuilder rb{ctx, 2, 1};
        rb.Push(readable_event.Signal(system.Kernel()));
        rb.PushCopyObjects(ctx, readable_event);
    }

    void Cancel(HLERequestContext& ctx) {
        // Encore has no remote friends operation queue. Cancellation is an acknowledged no-op.
        LOG_DEBUG(Service_Friend, "Friend-service cancel ignored (no remote operation pending)");

        IPC::ResponseBuilder rb{ctx, 2};
        rb.Push(ResultSuccess);
    }

    void GetFriendListIds(HLERequestContext& ctx) {
        IPC::RequestParser rp{ctx};
        const auto friend_offset = rp.Pop<u32>();
        const auto uuid = rp.PopRaw<Common::UUID>();
        [[maybe_unused]] const auto filter = rp.PopRaw<SizedFriendFilter>();
        const auto pid = rp.Pop<u64>();

        // No Nintendo friends backend is connected, therefore there are no network friend
        // account IDs to enumerate. Count zero means the type-0xA output buffer is untouched.
        LOG_DEBUG(Service_Friend,
                  "Returning empty friend-id list, offset={}, uuid=0x{}, pid={}", friend_offset,
                  uuid.RawString(), pid);
        IPC::ResponseBuilder rb{ctx, 3};
        rb.Push(ResultSuccess);
        rb.Push<u32>(0);
    }



    void UpdateFriendInfo(HLERequestContext& ctx) {
        IPC::RequestParser rp{ctx};
        const auto uuid = rp.PopRaw<Common::UUID>();
        const auto pid_placeholder = rp.Pop<u64>();

        // The Pointer input buffer contains NSAIDs and the MapAlias output contains
        // corresponding FriendImpl records. Offline mode has no records to refresh,
        // so initialize every requested output slot as invalid/default.
        std::vector<u8> friends(ctx.GetWriteBufferSize());
        ctx.WriteBuffer(friends);
        LOG_DEBUG(Service_Friend,
                  "Returning default UpdateFriendInfo records, uuid=0x{}, pid_placeholder={}, bytes={}",
                  uuid.RawString(), pid_placeholder, friends.size());

        IPC::ResponseBuilder rb{ctx, 2};
        rb.Push(ResultSuccess);
    }

    void GetFriendProfileImage(HLERequestContext& ctx) {
        IPC::RequestParser rp{ctx};
        const auto uuid = rp.PopRaw<Common::UUID>();
        const auto friend_id = rp.Pop<u64>();

        // No remote profile image exists. A zero size makes the MapAlias output buffer
        // contain no valid image bytes.
        LOG_DEBUG(Service_Friend,
                  "Returning empty friend profile image, uuid=0x{}, friend_id={:016X}",
                  uuid.RawString(), friend_id);
        IPC::ResponseBuilder rb{ctx, 3};
        rb.Push(ResultSuccess);
        rb.Push<u32>(0);
    }


    void GetFriendRequestListV1(HLERequestContext& ctx) {
        IPC::RequestParser rp{ctx};
        const auto offset = rp.Pop<u32>();
        const auto count_limit = rp.Pop<u32>();
        const auto uuid = rp.PopRaw<Common::UUID>();

        if (ctx.CanWriteBuffer()) {
            std::vector<u8> requests(ctx.GetWriteBufferSize());
            ctx.WriteBuffer(requests);
        }
        LOG_DEBUG(Service_Friend,
                  "Returning empty friend-request list, offset={}, limit={}, uuid=0x{}",
                  offset, count_limit, uuid.RawString());
        IPC::ResponseBuilder rb{ctx, 3};
        rb.Push(ResultSuccess);
        rb.Push<u32>(0);
    }

    void GetBlockedUserListV1(HLERequestContext& ctx) {
        IPC::RequestParser rp{ctx};
        const auto offset = rp.Pop<u32>();
        const auto uuid = rp.PopRaw<Common::UUID>();

        if (ctx.CanWriteBuffer()) {
            std::vector<u8> blocked(ctx.GetWriteBufferSize());
            ctx.WriteBuffer(blocked);
        }
        LOG_DEBUG(Service_Friend, "Returning empty blocked-user list V1, offset={}, uuid=0x{}",
                  offset, uuid.RawString());
        IPC::ResponseBuilder rb{ctx, 3};
        rb.Push(ResultSuccess);
        rb.Push<u32>(0);
    }

    void GetPlayHistoryListV1(HLERequestContext& ctx) {
        IPC::RequestParser rp{ctx};
        const auto offset = rp.Pop<u32>();
        const auto uuid = rp.PopRaw<Common::UUID>();

        if (ctx.CanWriteBuffer()) {
            std::vector<u8> history(ctx.GetWriteBufferSize());
            ctx.WriteBuffer(history);
        }
        LOG_DEBUG(Service_Friend, "Returning empty play-history list, offset={}, uuid=0x{}",
                  offset, uuid.RawString());
        IPC::ResponseBuilder rb{ctx, 3};
        rb.Push(ResultSuccess);
        rb.Push<u32>(0);
    }

    void GetReceivedFriendInvitationListV1(HLERequestContext& ctx) {
        IPC::RequestParser rp{ctx};
        const auto uuid = rp.PopRaw<Common::UUID>();

        if (ctx.CanWriteBuffer()) {
            std::vector<u8> invitations(ctx.GetWriteBufferSize());
            ctx.WriteBuffer(invitations);
        }
        LOG_DEBUG(Service_Friend, "Returning empty received invitation list, uuid=0x{}",
                  uuid.RawString());
        IPC::ResponseBuilder rb{ctx, 3};
        rb.Push(ResultSuccess);
        rb.Push<u32>(0);
    }

    void GetNintendoNetworkIdInfo(HLERequestContext& ctx) {
        IPC::RequestParser rp{ctx};
        const auto offset = rp.Pop<u32>();
        const auto uuid = rp.PopRaw<Common::UUID>();

        // User info is a fixed 0x38 Pointer output; the second output is a MapAlias list.
        // Offline mode has no Nintendo Network ID data, so initialize both and return count 0.
        const std::array<u8, 0x38> user_info{};
        ctx.WriteBuffer(user_info, 0);
        if (ctx.CanWriteBuffer(1)) {
            std::vector<u8> friend_info(ctx.GetWriteBufferSize(1));
            ctx.WriteBuffer(friend_info, 1);
        }

        LOG_DEBUG(Service_Friend,
                  "Returning empty Nintendo Network ID info, offset={}, uuid=0x{}",
                  offset, uuid.RawString());
        IPC::ResponseBuilder rb{ctx, 3};
        rb.Push(ResultSuccess);
        rb.Push<u32>(0);
    }

    void GetExternalApplicationCatalog(HLERequestContext& ctx) {
        IPC::RequestParser rp{ctx};
        const auto language_code = rp.Pop<u64>();
        [[maybe_unused]] const auto catalog_id = rp.PopRaw<std::array<u8, 0x10>>();

        // ExternalApplicationCatalog is a fixed 0x4B8 Pointer output. There is no
        // Nintendo friends catalog backend, so return the default object.
        const std::array<u8, 0x4B8> catalog{};
        ctx.WriteBuffer(catalog);
        LOG_DEBUG(Service_Friend,
                  "Returning empty external application catalog, language_code={:016X}",
                  language_code);

        IPC::ResponseBuilder rb{ctx, 2};
        rb.Push(ResultSuccess);
    }

    void GetFriendList(HLERequestContext& ctx) {
        IPC::RequestParser rp{ctx};
        const auto friend_offset = rp.Pop<u32>();
        const auto uuid = rp.PopRaw<Common::UUID>();
        [[maybe_unused]] const auto filter = rp.PopRaw<SizedFriendFilter>();
        const auto pid = rp.Pop<u64>();
        LOG_DEBUG(Service_Friend,
                  "Returning empty friend list, offset={}, uuid=0x{}, pid={}", friend_offset,
                  uuid.RawString(), pid);

        // No Nintendo friends backend is connected. A zero count means the output array contains
        // no entries, so there is intentionally no buffer payload to write.
        IPC::ResponseBuilder rb{ctx, 3};
        rb.Push(ResultSuccess);
        rb.Push<u32>(0);
    }

    void CheckFriendListAvailability(HLERequestContext& ctx) {
        IPC::RequestParser rp{ctx};
        const auto uuid{rp.PopRaw<Common::UUID>()};

        LOG_WARNING(Service_Friend, "(STUBBED) called, uuid=0x{}", uuid.RawString());

        IPC::ResponseBuilder rb{ctx, 3};
        rb.Push(ResultSuccess);
        rb.Push(true);
    }

    void GetBlockedUserListIds(HLERequestContext& ctx) {
        // No remote social backend is exposed, therefore no blocked-user records can exist.
        LOG_DEBUG(Service_Friend, "Returning empty blocked-user list");
        IPC::ResponseBuilder rb{ctx, 3};
        rb.Push(ResultSuccess);
        rb.Push<u32>(0);
    }

    void CheckBlockedUserListAvailability(HLERequestContext& ctx) {
        IPC::RequestParser rp{ctx};
        const auto uuid{rp.PopRaw<Common::UUID>()};

        LOG_WARNING(Service_Friend, "(STUBBED) called, uuid=0x{}", uuid.RawString());

        IPC::ResponseBuilder rb{ctx, 3};
        rb.Push(ResultSuccess);
        rb.Push(true);
    }


    void GetProfileList(HLERequestContext& ctx) {
        IPC::RequestParser rp{ctx};
        const auto uuid = rp.PopRaw<Common::UUID>();

        // ProfileImpl is returned through a MapAlias output array for the NSAIDs
        // provided in the Pointer input buffer. Without a social backend every requested
        // profile is invalid/default, so initialize the complete output buffer to zero.
        std::vector<u8> profiles(ctx.GetWriteBufferSize());
        ctx.WriteBuffer(profiles);
        LOG_DEBUG(Service_Friend, "Returning default offline profile list, uuid=0x{}, bytes={}",
                  uuid.RawString(), profiles.size());

        IPC::ResponseBuilder rb{ctx, 2};
        rb.Push(ResultSuccess);
    }

    void GetPlayHistoryRegistrationKeyWithNetworkServiceAccountId(HLERequestContext& ctx) {
        IPC::RequestParser rp{ctx};
        const auto local_play = rp.Pop<bool>();
        const auto friend_id = rp.Pop<u64>();

        const std::array<u8, 0x40> registration_key{};
        ctx.WriteBuffer(registration_key);
        LOG_DEBUG(Service_Friend,
                  "Returning empty play-history key for NSAID, local_play={}, friend_id={:016X}",
                  local_play, friend_id);

        IPC::ResponseBuilder rb{ctx, 2};
        rb.Push(ResultSuccess);
    }

    void GetProfileImageUrl(HLERequestContext& ctx) {
        IPC::RequestParser rp{ctx};
        [[maybe_unused]] const auto input_url = rp.PopRaw<std::array<u8, 0xA0>>();
        const auto image_size = rp.Pop<u32>();

        // nn::friends::Url is exactly 0xA0 bytes. No remote profile image exists in
        // offline mode, so return the default/empty URL as Ryujinx does.
        const std::array<u8, 0xA0> output_url{};
        LOG_DEBUG(Service_Friend, "Returning empty profile-image URL, image_size={}", image_size);

        IPC::ResponseBuilder rb{ctx, 42};
        rb.Push(ResultSuccess);
        rb.PushRaw(output_url);
    }

    void DeclareOpenOnlinePlaySession(HLERequestContext& ctx) {
        // Encore has no Nintendo presence backend to publish an online-play session to.
        // This mirrors DeclareCloseOnlinePlaySession: local gameplay state is unaffected.
        LOG_DEBUG(Service_Friend, "Open-online-play-session ignored (offline social backend)");
        IPC::ResponseBuilder rb{ctx, 2};
        rb.Push(ResultSuccess);
    }

    void DeclareCloseOnlinePlaySession(HLERequestContext& ctx) {
        // Online-play presence is not published to Nintendo services by Encore.
        LOG_DEBUG(Service_Friend, "Close-online-play-session ignored (offline social backend)");
        IPC::ResponseBuilder rb{ctx, 2};
        rb.Push(ResultSuccess);
    }

    void UpdateUserPresence(HLERequestContext& ctx) {
        // Presence publication is intentionally local-only; there is no Nintendo friends backend.
        LOG_DEBUG(Service_Friend, "User-presence publication ignored (offline social backend)");
        IPC::ResponseBuilder rb{ctx, 2};
        rb.Push(ResultSuccess);
    }

    void GetPlayHistoryRegistrationKey(HLERequestContext& ctx) {
        IPC::RequestParser rp{ctx};
        const auto local_play = rp.Pop<bool>();
        const auto uuid = rp.PopRaw<Common::UUID>();

        // nn::friends::PlayHistoryRegistrationKey is a 0x40-byte output buffer. Without an
        // authenticated Nintendo friends backend there is no registration key to issue, so return
        // the ABI-correct all-zero value rather than leaving guest memory untouched.
        const std::array<u8, 0x40> registration_key{};
        ctx.WriteBuffer(registration_key);
        LOG_DEBUG(Service_Friend,
                  "Returning empty play-history registration key, local_play={}, uuid=0x{}",
                  local_play, uuid.RawString());

        IPC::ResponseBuilder rb{ctx, 2};
        rb.Push(ResultSuccess);
    }

    void GetFriendCount(HLERequestContext& ctx) {
        LOG_DEBUG(Service_Friend, "Returning friend count 0 (offline social backend)");

        IPC::ResponseBuilder rb{ctx, 3};
        rb.Push(ResultSuccess);
        rb.Push<s32>(0);
    }

    void GetNewlyFriendCount(HLERequestContext& ctx) {
        LOG_DEBUG(Service_Friend, "Returning newly-friend count 0 (offline social backend)");

        IPC::ResponseBuilder rb{ctx, 3};
        rb.Push(ResultSuccess);
        rb.Push<s32>(0);
    }

    void RequestSyncFriendList(HLERequestContext& ctx) {
        // There is no remote friend list to synchronize in Encore.
        LOG_DEBUG(Service_Friend, "Friend-list sync ignored (offline social backend)");

        IPC::ResponseBuilder rb{ctx, 2};
        rb.Push(ResultSuccess);
    }


    void GetFriendDetailedInfo(HLERequestContext& ctx) {
        IPC::RequestParser rp{ctx};
        const auto uuid = rp.PopRaw<Common::UUID>();
        const auto friend_id = rp.Pop<u64>();

        // FriendDetailedInfoImpl is a fixed 0x800-byte pointer output. Encore has no
        // Nintendo social backend, so there is no friend record to populate.
        const std::array<u8, 0x800> detailed_info{};
        ctx.WriteBuffer(detailed_info);

        LOG_DEBUG(Service_Friend,
                  "Returning empty friend detailed info, uuid=0x{}, friend_id={:016X}",
                  uuid.RawString(), friend_id);
        IPC::ResponseBuilder rb{ctx, 2};
        rb.Push(ResultSuccess);
    }

    void GetRelationship(HLERequestContext& ctx) {
        IPC::RequestParser rp{ctx};
        const auto uuid = rp.PopRaw<Common::UUID>();
        const auto friend_id = rp.Pop<u64>();

        // nn::friends::Relationship is an 8-byte raw output. With no friend record,
        // Ryujinx and other offline implementations return the default/zero value.
        constexpr u64 relationship{};
        LOG_DEBUG(Service_Friend,
                  "Returning empty relationship, uuid=0x{}, friend_id={:016X}",
                  uuid.RawString(), friend_id);

        IPC::ResponseBuilder rb{ctx, 4};
        rb.Push(ResultSuccess);
        rb.Push(relationship);
    }

    void SyncFriendList(HLERequestContext& ctx) {
        // There is no remote list distinct from the already-empty local friend state.
        LOG_DEBUG(Service_Friend, "Friend-list sync ignored (offline social backend)");
        IPC::ResponseBuilder rb{ctx, 2};
        rb.Push(ResultSuccess);
    }

    void GetFriendCandidateList(HLERequestContext& ctx) {
        // Friend candidates are provided by Nintendo's social backend. With that backend absent,
        // the correct local result set is empty; count zero leaves the output buffer untouched.
        LOG_DEBUG(Service_Friend, "Returning empty friend-candidate list");
        IPC::ResponseBuilder rb{ctx, 3};
        rb.Push(ResultSuccess);
        rb.Push<u32>(0);
    }

    void SyncBlockedUserList(HLERequestContext& ctx) {
        // The blocked-user list is already represented locally as empty and has no remote source.
        LOG_DEBUG(Service_Friend, "Blocked-user-list sync ignored (offline social backend)");
        IPC::ResponseBuilder rb{ctx, 2};
        rb.Push(ResultSuccess);
    }

    void SyncUserSetting(HLERequestContext& ctx) {
        // No Nintendo friends settings backend exists to synchronize with.
        LOG_DEBUG(Service_Friend, "Friend user-setting sync ignored (offline social backend)");
        IPC::ResponseBuilder rb{ctx, 2};
        rb.Push(ResultSuccess);
    }

    void GetFriendListForViewer(HLERequestContext& ctx) {
        LOG_DEBUG(Service_Friend, "(STUBBED) called");

        IPC::ResponseBuilder rb{ctx, 3};
        rb.Push(ResultSuccess);
        rb.Push<u32>(0);
    }

    void GetReceivedFriendRequestCount(HLERequestContext& ctx) {
        IPC::RequestParser rp{ctx};
        [[maybe_unused]] const auto uuid = rp.PopRaw<Common::UUID>();

        LOG_DEBUG(Service_Friend, "Returning received-friend-request counts 0/0");

        IPC::ResponseBuilder rb{ctx, 4};
        rb.Push(ResultSuccess);
        rb.Push<s32>(0);
        rb.Push<s32>(0);
    }

    void GetUserPresenceView(HLERequestContext& ctx) {
        IPC::RequestParser rp{ctx};
        const auto uuid = rp.PopRaw<Common::UUID>();
        LOG_DEBUG(Service_Friend, "(STUBBED) called, uuid={}.", uuid.RawString());

        u8 buf[0xe0]{};
        ctx.WriteBuffer(buf);

        IPC::ResponseBuilder rb{ctx, 3};
        rb.Push(ResultSuccess);
    }

    void GetPlayHistoryStatistics(HLERequestContext& ctx) {
        IPC::RequestParser rp{ctx};
        [[maybe_unused]] const auto uuid = rp.PopRaw<Common::UUID>();

        // PlayHistoryStatistics is a 0x10-byte (u128) raw output. No online play history exists
        // without a friends backend, so report the ABI-correct zero statistics object.
        const u128 statistics{};
        IPC::ResponseBuilder rb{ctx, 6};
        rb.Push(ResultSuccess);
        rb.PushRaw(statistics);
        LOG_DEBUG(Service_Friend, "Returning empty play-history statistics");
    }

    void LoadUserSetting(HLERequestContext& ctx) {
        IPC::RequestParser rp{ctx};
        const auto uuid = rp.PopRaw<Common::UUID>();
        const auto& local = GetLocalUserSetting(uuid);

        FriendsUserSetting setting{};
        setting.uuid = uuid;
        setting.presence_permission = local.presence_permission;
        setting.play_log_permission = local.play_log_permission;
        setting.friend_request_reception = local.friend_request_reception;
        setting.friend_code_next_issuable_time = 99999999999;
        strcpy(setting.friend_code, "0000-0000-0000");
        ctx.WriteBuffer(setting);

        IPC::ResponseBuilder rb{ctx, 2};
        rb.Push(ResultSuccess);
    }

    void ChangePresencePermission(HLERequestContext& ctx) {
        struct Parameters {
            Common::UUID uuid;
            s32 permission;
            INSERT_PADDING_WORDS(1);
        };
        static_assert(sizeof(Parameters) == 0x18);

        IPC::RequestParser rp{ctx};
        const auto params = rp.PopRaw<Parameters>();
        GetLocalUserSetting(params.uuid).presence_permission = static_cast<u32>(params.permission);

        IPC::ResponseBuilder rb{ctx, 2};
        rb.Push(ResultSuccess);
    }

    void ChangeFriendRequestReception(HLERequestContext& ctx) {
        struct Parameters {
            Common::UUID uuid;
            u8 reception;
            INSERT_PADDING_BYTES(7);
        };
        static_assert(sizeof(Parameters) == 0x18);

        IPC::RequestParser rp{ctx};
        const auto params = rp.PopRaw<Parameters>();
        GetLocalUserSetting(params.uuid).friend_request_reception = params.reception != 0;

        IPC::ResponseBuilder rb{ctx, 2};
        rb.Push(ResultSuccess);
    }

    void ChangePlayLogPermission(HLERequestContext& ctx) {
        struct Parameters {
            Common::UUID uuid;
            s32 permission;
            INSERT_PADDING_WORDS(1);
        };
        static_assert(sizeof(Parameters) == 0x18);

        IPC::RequestParser rp{ctx};
        const auto params = rp.PopRaw<Parameters>();
        GetLocalUserSetting(params.uuid).play_log_permission = static_cast<u32>(params.permission);

        IPC::ResponseBuilder rb{ctx, 2};
        rb.Push(ResultSuccess);
    }

    void RequestListSummaryOverlayNotification(HLERequestContext& ctx) {
        // No Horizon friends overlay is displayed by Encore; acknowledge the optional request.
        LOG_DEBUG(Service_Friend, "Friend-list overlay notification ignored");

        IPC::ResponseBuilder rb{ctx, 2};
        rb.Push(ResultSuccess);
    }

    void GetReceivedFriendInvitationCountCache(HLERequestContext& ctx) {
        LOG_DEBUG(Service_Friend, "Returning invitation count cache 0 (offline social backend)");

        IPC::ResponseBuilder rb{ctx, 3};
        rb.Push(ResultSuccess);
        rb.Push<s32>(0);
    }

    void GetProfileListV2(HLERequestContext& ctx) {
        if (ctx.CanWriteBuffer()) {
            std::vector<u8> profiles(ctx.GetWriteBufferSize());
            ctx.WriteBuffer(profiles);
        }
        LOG_DEBUG(Service_Friend, "Returning empty profile list V2");
        IPC::ResponseBuilder rb{ctx, 3};
        rb.Push(ResultSuccess);
        rb.Push<u32>(0);
    }

    void GetProfileExtraListV2(HLERequestContext& ctx) {
        if (ctx.CanWriteBuffer()) {
            std::vector<u8> profiles(ctx.GetWriteBufferSize());
            ctx.WriteBuffer(profiles);
        }
        LOG_DEBUG(Service_Friend, "Returning empty profile-extra list V2");
        IPC::ResponseBuilder rb{ctx, 3};
        rb.Push(ResultSuccess);
        rb.Push<u32>(0);
    }

    void EnsureFriendListAvailable(HLERequestContext& ctx) {
        IPC::RequestParser rp{ctx};
        const auto uuid = rp.PopRaw<Common::UUID>();
        LOG_DEBUG(Service_Friend, "Friend list is locally available for uuid=0x{}",
                  uuid.RawString());
        IPC::ResponseBuilder rb{ctx, 2};
        rb.Push(ResultSuccess);
    }

    void EnsureBlockedUserListAvailable(HLERequestContext& ctx) {
        IPC::RequestParser rp{ctx};
        const auto uuid = rp.PopRaw<Common::UUID>();
        LOG_DEBUG(Service_Friend, "Blocked-user list is locally available for uuid=0x{}",
                  uuid.RawString());
        IPC::ResponseBuilder rb{ctx, 2};
        rb.Push(ResultSuccess);
    }

    void GetFriendRequestListV2(HLERequestContext& ctx) {
        if (ctx.CanWriteBuffer()) {
            std::vector<u8> requests(ctx.GetWriteBufferSize());
            ctx.WriteBuffer(requests);
        }
        LOG_DEBUG(Service_Friend, "Returning empty Friend request list V2");
        IPC::ResponseBuilder rb{ctx, 3};
        rb.Push(ResultSuccess);
        rb.Push<u32>(0);
    }

    void GetBlockedUserListV2(HLERequestContext& ctx) {
        if (ctx.CanWriteBuffer()) {
            std::vector<u8> blocked(ctx.GetWriteBufferSize());
            ctx.WriteBuffer(blocked);
        }
        LOG_DEBUG(Service_Friend, "Returning empty blocked-user list V2");
        IPC::ResponseBuilder rb{ctx, 3};
        rb.Push(ResultSuccess);
        rb.Push<u32>(0);
    }

    void GetPlayHistoryListV2(HLERequestContext& ctx) {
        if (ctx.CanWriteBuffer()) {
            std::vector<u8> history(ctx.GetWriteBufferSize());
            ctx.WriteBuffer(history);
        }
        LOG_DEBUG(Service_Friend, "Returning empty play-history list V2");
        IPC::ResponseBuilder rb{ctx, 3};
        rb.Push(ResultSuccess);
        rb.Push<u32>(0);
    }

    void GetReceivedFriendInvitationListV2(HLERequestContext& ctx) {
        if (ctx.CanWriteBuffer()) {
            std::vector<u8> invitations(ctx.GetWriteBufferSize());
            ctx.WriteBuffer(invitations);
        }
        LOG_DEBUG(Service_Friend, "Returning empty received invitation list V2");
        IPC::ResponseBuilder rb{ctx, 3};
        rb.Push(ResultSuccess);
        rb.Push<u32>(0);
    }

    void DropFriendNewlyFlags(HLERequestContext& ctx) {
        IPC::RequestParser rp{ctx};
        const auto uuid = rp.PopRaw<Common::UUID>();
        LOG_DEBUG(Service_Friend, "No friend-newly flags to drop for uuid=0x{}", uuid.RawString());
        IPC::ResponseBuilder rb{ctx, 2};
        rb.Push(ResultSuccess);
    }

    void DropFriendNewlyFlag(HLERequestContext& ctx) {
        IPC::RequestParser rp{ctx};
        const auto uuid = rp.PopRaw<Common::UUID>();
        const auto friend_id = rp.Pop<u64>();
        LOG_DEBUG(Service_Friend,
                  "No friend-newly flag to drop for uuid=0x{}, friend_id={:#x}",
                  uuid.RawString(), friend_id);
        IPC::ResponseBuilder rb{ctx, 2};
        rb.Push(ResultSuccess);
    }

    void DeletePlayHistory(HLERequestContext& ctx) {
        IPC::RequestParser rp{ctx};
        const auto uuid = rp.PopRaw<Common::UUID>();
        LOG_DEBUG(Service_Friend, "No play-history backend to clear for uuid=0x{}",
                  uuid.RawString());
        IPC::ResponseBuilder rb{ctx, 2};
        rb.Push(ResultSuccess);
    }

    void ClearPlayLog(HLERequestContext& ctx) {
        IPC::RequestParser rp{ctx};
        const auto uuid = rp.PopRaw<Common::UUID>();
        LOG_DEBUG(Service_Friend, "No friend play-log backend to clear for uuid=0x{}",
                  uuid.RawString());
        IPC::ResponseBuilder rb{ctx, 2};
        rb.Push(ResultSuccess);
    }

    void DeleteNetworkServiceAccountCache(HLERequestContext& ctx) {
        IPC::RequestParser rp{ctx};
        const auto uuid = rp.PopRaw<Common::UUID>();
        LOG_DEBUG(Service_Friend, "No network-service-account cache to delete for uuid=0x{}",
                  uuid.RawString());
        IPC::ResponseBuilder rb{ctx, 2};
        rb.Push(ResultSuccess);
    }

    KernelHelpers::ServiceContext service_context;
    std::vector<LocalUserSetting> local_user_settings;

    Kernel::KEvent* completion_event;

    // Encore NS1: unavailable Nintendo online backend; explicit error, never fake success.
    void RejectUnavailableNintendoOnline(HLERequestContext& ctx) {
        IPC::ResponseBuilder rb{ctx, 2};
        rb.Push(ResultUnknown);
    }

};

class INotificationService final : public ServiceFramework<INotificationService> {
public:
    explicit INotificationService(Core::System& system_, Common::UUID uuid_)
        : ServiceFramework{system_, "INotificationService"}, uuid{uuid_},
          service_context{system_, "INotificationService"} {
        // clang-format off
        static const FunctionInfo functions[] = {
            {0, &INotificationService::GetEvent, "GetEvent"},
            {1, &INotificationService::Clear, "Clear"},
            {2, &INotificationService::Pop, "Pop"}
        };
        // clang-format on

        RegisterHandlers(functions);

        notification_event = service_context.CreateEvent("INotificationService:NotifyEvent");
    }

    ~INotificationService() override {
        service_context.CloseEvent(notification_event);
    }

private:
    void GetEvent(HLERequestContext& ctx) {
        LOG_DEBUG(Service_Friend, "called");

        IPC::ResponseBuilder rb{ctx, 2, 1};
        rb.Push(ResultSuccess);
        rb.PushCopyObjects(ctx, notification_event->GetReadableEvent());
    }

    void Clear(HLERequestContext& ctx) {
        LOG_DEBUG(Service_Friend, "called");
        while (!notifications.empty()) {
            notifications.pop();
        }
        std::memset(&states, 0, sizeof(States));

        IPC::ResponseBuilder rb{ctx, 2};
        rb.Push(ResultSuccess);
    }

    void Pop(HLERequestContext& ctx) {
        LOG_DEBUG(Service_Friend, "called");

        if (notifications.empty()) {
            LOG_ERROR(Service_Friend, "No notifications in queue!");
            IPC::ResponseBuilder rb{ctx, 2};
            rb.Push(Account::ResultNoNotifications);
            return;
        }

        const auto notification = notifications.front();
        notifications.pop();

        switch (notification.notification_type) {
        case NotificationTypes::HasUpdatedFriendsList:
            states.has_updated_friends = false;
            break;
        case NotificationTypes::HasReceivedFriendRequest:
            states.has_received_friend_request = false;
            break;
        default:
            // HOS seems not have an error case for an unknown notification
            LOG_WARNING(Service_Friend, "Unknown notification {:08X}",
                        notification.notification_type);
            break;
        }

        IPC::ResponseBuilder rb{ctx, 6};
        rb.Push(ResultSuccess);
        rb.PushRaw<SizedNotificationInfo>(notification);
    }

    enum class NotificationTypes : u32 {
        HasUpdatedFriendsList = 0x65,
        HasReceivedFriendRequest = 0x1
    };

    struct SizedNotificationInfo {
        NotificationTypes notification_type;
        INSERT_PADDING_WORDS(
            1); // TODO(ogniK): This doesn't seem to be used within any IPC returns as of now
        u64_le account_id;
    };
    static_assert(sizeof(SizedNotificationInfo) == 0x10,
                  "SizedNotificationInfo is an incorrect size");

    struct States {
        bool has_updated_friends;
        bool has_received_friend_request;
    };

    Common::UUID uuid;
    KernelHelpers::ServiceContext service_context;

    Kernel::KEvent* notification_event;
    std::queue<SizedNotificationInfo> notifications;
    States states{};
};


class IDaemonSuspendSessionService final
    : public ServiceFramework<IDaemonSuspendSessionService> {
public:
    explicit IDaemonSuspendSessionService(Core::System& system_)
        : ServiceFramework{system_, "IDaemonSuspendSessionService"} {}
};

void Module::Interface::CreateFriendService(HLERequestContext& ctx) {
    IPC::ResponseBuilder rb{ctx, 2, 0, 1};
    rb.Push(ResultSuccess);
    rb.PushIpcInterface<IFriendService>(ctx, system);
    LOG_DEBUG(Service_Friend, "called");
}

void Module::Interface::CreateNotificationService(HLERequestContext& ctx) {
    IPC::RequestParser rp{ctx};
    auto uuid = rp.PopRaw<Common::UUID>();

    LOG_DEBUG(Service_Friend, "called, uuid=0x{}", uuid.RawString());

    IPC::ResponseBuilder rb{ctx, 2, 0, 1};
    rb.Push(ResultSuccess);
    rb.PushIpcInterface<INotificationService>(ctx, system, uuid);
}


void Module::Interface::CreateDaemonSuspendSessionService(HLERequestContext& ctx) {
    IPC::ResponseBuilder rb{ctx, 2, 0, 1};
    rb.Push(ResultSuccess);
    rb.PushIpcInterface<IDaemonSuspendSessionService>(ctx, system);
    LOG_DEBUG(Service_Friend, "created empty daemon suspend-session service");
}

Module::Interface::Interface(std::shared_ptr<Module> module_, Core::System& system_, const char* name)
    : ServiceFramework{system_, name}, module{std::move(module_)}
{}

Module::Interface::~Interface() = default;

class IServiceForApplication final : public ServiceFramework<IServiceForApplication> {
public:
    explicit IServiceForApplication(Core::System& system_)
        : ServiceFramework{system_, "nd:app"}
    {
        static const FunctionInfo functions[] = {
            {0, &IServiceForApplication::RejectUnavailableNintendoOnline, "GetReceivableNeighborInfoCountMax"},
            {10, &IServiceForApplication::RejectUnavailableNintendoOnline, "IsNeighborDetectionEnabled"},
        };
        RegisterHandlers(functions);
    }

    // Encore NS1: unavailable Nintendo online backend; explicit error, never fake success.
    void RejectUnavailableNintendoOnline(HLERequestContext& ctx) {
        IPC::ResponseBuilder rb{ctx, 2};
        rb.Push(ResultUnknown);
    }

};

class IServiceForSystem final : public ServiceFramework<IServiceForSystem> {
public:
    explicit IServiceForSystem(Core::System& system_)
        : ServiceFramework{system_, "nd:sys"}
    {
        static const FunctionInfo functions[] = {
            {0, &IServiceForSystem::RejectUnavailableNintendoOnline, "GetReceivableNeighborInfoCountMax"},
            {10, &IServiceForSystem::RejectUnavailableNintendoOnline, "IsNeighborDetectionEnabled"},
            {200, &IServiceForSystem::RejectUnavailableNintendoOnline, "SetSystemData"},
            {201, &IServiceForSystem::RejectUnavailableNintendoOnline, "ClearSystemData"},
            {203, &IServiceForSystem::RejectUnavailableNintendoOnline, "GetReceivableNeighborInfoCountForSystem"},
            {204, &IServiceForSystem::RejectUnavailableNintendoOnline, "ReceiveNeighborInfoForSystem"},
            {205, &IServiceForSystem::RejectUnavailableNintendoOnline, "SetSender"},
            {206, &IServiceForSystem::RejectUnavailableNintendoOnline, "GetSender"},
            {207, &IServiceForSystem::RejectUnavailableNintendoOnline, "CreateScannerForSystem"},
            {208, &IServiceForSystem::RejectUnavailableNintendoOnline, "CreateReceiveEventHolderForSystem"},
            {223, &IServiceForSystem::RejectUnavailableNintendoOnline, "EnableNeighborDetection"},
            {224, &IServiceForSystem::RejectUnavailableNintendoOnline, "DisableNeighborDetection"},
            {226, &IServiceForSystem::RejectUnavailableNintendoOnline, "EnablePowerSave"},
            {227, &IServiceForSystem::RejectUnavailableNintendoOnline, "DisablePowerSave"},
            {228, &IServiceForSystem::RejectUnavailableNintendoOnline, "IsPowerSaveEnabled"},
            {232, &IServiceForSystem::RejectUnavailableNintendoOnline, "ClearBlockedUsers"},
            {233, &IServiceForSystem::RejectUnavailableNintendoOnline, "GetBlockedUserCount"},
            {234, &IServiceForSystem::RejectUnavailableNintendoOnline, "BlockUserByLocalUserId"},
            {235, &IServiceForSystem::RejectUnavailableNintendoOnline, "BlockUserByNetworkUserId"},
            {236, &IServiceForSystem::RejectUnavailableNintendoOnline, "UnblockUserByLocalUserId"},
            {237, &IServiceForSystem::RejectUnavailableNintendoOnline, "UnblockUserByNetworkUserId"},
            {240, &IServiceForSystem::RejectUnavailableNintendoOnline, "DeleteApplication"},
            {250, &IServiceForSystem::RejectUnavailableNintendoOnline, "InitializeApplicationInfo"},
            {260, &IServiceForSystem::RejectUnavailableNintendoOnline, "CreateAccountSystemSaveDataAccessSuppressor"},
            {300, &IServiceForSystem::RejectUnavailableNintendoOnline, "AddReceivedNeighborInfoForSystemForDebug"},
            {301, &IServiceForSystem::RejectUnavailableNintendoOnline, "GetSendDataForDebug"},
            {302, &IServiceForSystem::RejectUnavailableNintendoOnline, "ClearReceiveCounterForDebug"},
            {303, &IServiceForSystem::RejectUnavailableNintendoOnline, "GetNextReceiveCounterForDebug"},
            {304, &IServiceForSystem::RejectUnavailableNintendoOnline, "ListBlockedUsersForDebug"},
            {305, &IServiceForSystem::RejectUnavailableNintendoOnline, "RefreshSendDataIdForDebug"},
            {306, &IServiceForSystem::RejectUnavailableNintendoOnline, "ReloadFwdbgSettingsForDebug"},
            {307, &IServiceForSystem::RejectUnavailableNintendoOnline, "EnableApplicationForDebug"},
            {308, &IServiceForSystem::RejectUnavailableNintendoOnline, "GetNextReceiveCountersForDebug"},
            {309, &IServiceForSystem::RejectUnavailableNintendoOnline, "ListApplicationInfoForDebug"},
            {310, &IServiceForSystem::RejectUnavailableNintendoOnline, "SetApplicationDataForDebug"},
            {400, &IServiceForSystem::RejectUnavailableNintendoOnline, "GetNetworkUserId"},
            {401, &IServiceForSystem::RejectUnavailableNintendoOnline, "DeleteNetworkUserId"},
        };
        RegisterHandlers(functions);
    }

    // Encore NS1: unavailable Nintendo online backend; explicit error, never fake success.
    void RejectUnavailableNintendoOnline(HLERequestContext& ctx) {
        IPC::ResponseBuilder rb{ctx, 2};
        rb.Push(ResultUnknown);
    }

};

class IChatShimObject final : public ServiceFramework<IChatShimObject> {
public:
    explicit IChatShimObject(Core::System& system_, const char* name)
        : ServiceFramework{system_, name} {}

    Result HandleSyncRequest(Kernel::KServerSession& session,
                             HLERequestContext& ctx) override {
        const auto type = ctx.GetCommandType();
        if (type == IPC::CommandType::Close || type == IPC::CommandType::TIPC_Close ||
            type == IPC::CommandType::Control ||
            type == IPC::CommandType::ControlWithContext) {
            return ServiceFrameworkBase::HandleSyncRequest(session, ctx);
        }
        // These scoped GameChat objects have no state outside an active room.
        // Return an explicit empty success instead of depending on the global
        // use_auto_stub preference.
        const auto guard = LockService();
        LOG_DEBUG(Service_Friend, "(STUBBED) {} command={}", GetServiceName(),
                  ctx.GetCommand());
        IPC::ResponseBuilder rb{ctx, 2};
        rb.Push(ResultSuccess);
        return system.IsPoweredOn() ? ctx.WriteToOutgoingCommandBuffer()
                                    : ResultSuccess;
    }
};

class IChatSession final : public ServiceFramework<IChatSession> {
public:
    explicit IChatSession(Core::System& system_)
        : ServiceFramework{system_, "nx2:chat:u::0"},
          service_context{system_, "IChatSession"} {
        static const FunctionInfo functions[] = {
            {1, &IChatSession::GetEvent, "GetEvent"},
            {2, &IChatSession::AttachToChangedEvent, "AttachToRoomInfoChangedEvent"},
            {3, &IChatSession::AttachToChangedEvent,
             "AttachToLocalCameraImageProcessingModeChangedEvent"},
            {4, &IChatSession::AttachToChangedEvent,
             "AttachToLocalCameraZoomMagnificationChangedEvent"},
        };
        RegisterHandlers(functions);
        event = service_context.CreateEvent("IChatSession:Event");
        static constexpr std::array<const char*, 3> names{
            "IChatSession:RoomInfoChanged",
            "IChatSession:LocalCameraImageProcessingModeChanged",
            "IChatSession:LocalCameraZoomMagnificationChanged",
        };
        for (std::size_t i = 0; i < changed_events.size(); ++i) {
            changed_events[i] = service_context.CreateEvent(names[i]);
        }
    }

    ~IChatSession() override {
        service_context.CloseEvent(event);
        for (auto* changed : changed_events) {
            service_context.CloseEvent(changed);
        }
    }

private:
    void GetEvent(HLERequestContext& ctx) {
        IPC::ResponseBuilder rb{ctx, 2, 1};
        rb.Push(ResultSuccess);
        rb.PushCopyObjects(ctx, event->GetReadableEvent());
    }

    void AttachToChangedEvent(HLERequestContext& ctx) {
        const u32 command = ctx.GetCommand();
        if (command < 2 || command > 4) {
            IPC::ResponseBuilder rb{ctx, 2};
            rb.Push(Kernel::ResultInvalidArgument);
            return;
        }
        IPC::ResponseBuilder rb{ctx, 2, 1};
        rb.Push(ResultSuccess);
        rb.PushCopyObjects(ctx, changed_events[command - 2]->GetReadableEvent());
    }

    KernelHelpers::ServiceContext service_context;
    Kernel::KEvent* event{};
    std::array<Kernel::KEvent*, 3> changed_events{};
};

class IChatService final : public ServiceFramework<IChatService> {
public:
    explicit IChatService(Core::System& system_)
        : ServiceFramework{system_, "nx2:chat:u"} {
        static const FunctionInfo functions[] = {
            {0, &IChatService::OpenSession, "OpenSession"},
            {1, &IChatService::OpenSession, "OpenSessionWithFlag"},
            {2, &IChatService::OpenShimObject, "OpenUserLayoutCustomizationShimScopedObject"},
            {3, &IChatService::OpenShimObject, "OpenVirtualCameraShimScopedObject"},
            {4, &IChatService::OpenShimObject, "OpenUserSelectiveTalkShimScopedObject"},
            {11, &IChatService::GetRoomInfo, "GetRoomInfo"},
        };
        RegisterHandlers(functions);
    }

private:
    void OpenSession(HLERequestContext& ctx) {
        IPC::ResponseBuilder rb{ctx, 2, 0, 1};
        rb.Push(ResultSuccess);
        rb.PushIpcInterface<IChatSession>(ctx, system);
    }

    void GetRoomInfo(HLERequestContext& ctx) {
        constexpr std::array<u8, 0xF0> room_info{};
        ctx.WriteBuffer(room_info);
        IPC::ResponseBuilder rb{ctx, 2};
        rb.Push(ResultSuccess);
    }

    void OpenShimObject(HLERequestContext& ctx) {
        static constexpr std::array<const char*, 3> names{
            "nx2:chat:IUserLayoutCustomizationShimScopedObject",
            "nx2:chat:IVirtualCameraShimScopedObject",
            "nx2:chat:IUserSelectiveTalkShimScopedObject",
        };
        const u32 command = ctx.GetCommand();
        if (command < 2 || command > 4) {
            IPC::ResponseBuilder rb{ctx, 2};
            rb.Push(Kernel::ResultInvalidArgument);
            return;
        }
        IPC::ResponseBuilder rb{ctx, 2, 0, 1};
        rb.Push(ResultSuccess);
        rb.PushIpcInterface<IChatShimObject>(ctx, system, names[command - 2]);
    }
};

void LoopProcess(Core::System& system) {
    auto server_manager = std::make_unique<ServerManager>(system);
    auto module = std::make_shared<Module>();

    server_manager->RegisterNamedService("nd:app", std::make_shared<IServiceForApplication>(system));
    server_manager->RegisterNamedService("nd:sys", std::make_shared<IServiceForSystem>(system));

    server_manager->RegisterNamedService("friend:a", std::make_shared<Friend>(module, system, "friend:a"));
    server_manager->RegisterNamedService("friend:m", std::make_shared<Friend>(module, system, "friend:m"));
    server_manager->RegisterNamedService("friend:s", std::make_shared<Friend>(module, system, "friend:s"));
    server_manager->RegisterNamedService("friend:u", std::make_shared<Friend>(module, system, "friend:u"));
    server_manager->RegisterNamedService("friend:v", std::make_shared<Friend>(module, system, "friend:v"));

    // MKW 1.8.0 waits in SM when the Switch 2 GameChat service is absent.
    server_manager->RegisterNamedService("chat:u", std::make_shared<IChatService>(system));

    ServerManager::RunServer(std::move(server_manager));
}

} // namespace Service::Friend
