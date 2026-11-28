// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

// SPDX-FileCopyrightText: Copyright 2018 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <algorithm>
#include <array>
#include <memory>
#include <mutex>
#include <vector>

#include "core/hle/kernel/k_event.h"
#include "core/hle/service/cmif_serialization.h"
#include "core/hle/service/ipc_helpers.h"
#include "core/hle/service/kernel_helpers.h"
#include "core/hle/service/npns/npns.h"
#include "core/hle/service/server_manager.h"
#include "core/hle/service/service.h"

namespace Service::NPNS {

namespace {

using TokenKey = std::array<u8, 0x18>;
using TokenValue = std::array<u8, 0x28>;

class OfflineTokenStore {
public:
    TokenValue Create(const TokenKey& key) {
        std::scoped_lock lk{mutex};

        const auto it = std::find_if(tokens.begin(), tokens.end(),
                                     [&](const Entry& entry) { return entry.key == key; });
        if (it != tokens.end()) {
            return it->token;
        }

        TokenValue token{};
        std::copy(key.begin(), key.end(), token.begin());

        const u64 nonce = next_nonce++;
        for (size_t i = 0; i < sizeof(nonce); ++i) {
            token[0x18 + i] = static_cast<u8>(nonce >> (i * 8));
            token[0x20 + i] = static_cast<u8>((~nonce) >> (i * 8));
        }

        tokens.push_back(Entry{key, token});
        return token;
    }

    void Destroy(const TokenKey& key) {
        std::scoped_lock lk{mutex};
        std::erase_if(tokens, [&](const Entry& entry) { return entry.key == key; });
    }

    bool IsValid(const TokenValue& token) const {
        std::scoped_lock lk{mutex};
        return std::any_of(tokens.begin(), tokens.end(),
                           [&](const Entry& entry) { return entry.token == token; });
    }

private:
    struct Entry {
        TokenKey key;
        TokenValue token;
    };

    mutable std::mutex mutex;
    std::vector<Entry> tokens;
    u64 next_nonce{1};
};

} // namespace

class INpnsSystem final : public ServiceFramework<INpnsSystem> {
public:
    explicit INpnsSystem(Core::System& system_)
        : ServiceFramework{system_, "npns:s"}, service_context{system, "npns:s"},
          get_receive_event{service_context}, get_state_change_event{service_context},
          get_drop_event{service_context}, get_request_change_state_cancel_event{service_context},
          get_play_report_request_event{service_context} {
        // clang-format off
        static const FunctionInfo functions[] = {
            {1, C<&INpnsSystem::ListenAll>, "ListenAll"},
            {2, C<&INpnsSystem::ListenTo>, "ListenTo"},
            {3, &INpnsSystem::RejectUnavailableNintendoOnline, "Receive"},
            {4, &INpnsSystem::RejectUnavailableNintendoOnline, "ReceiveRaw"},
            {5, C<&INpnsSystem::GetReceiveEvent>, "GetReceiveEvent"},
            {6, C<&INpnsSystem::ListenUndelivered>, "ListenUndelivered"},
            {7, C<&INpnsSystem::GetStateChangeEvent>, "GetStateChangeEvent"},
            {8, C<&INpnsSystem::ListenToByName>, "ListenToByName"},
            {11, D<&INpnsSystem::SubscribeTopic>, "SubscribeTopic"},
            {12, D<&INpnsSystem::UnsubscribeTopic>, "UnsubscribeTopic"},
            {13, &INpnsSystem::RejectUnavailableNintendoOnline, "QueryIsTopicExist"},
            {14, D<&INpnsSystem::SubscribeTopicByAccount>, "SubscribeTopicByAccount"}, // 18.0.0+
            {15, D<&INpnsSystem::UnsubscribeTopicByAccount>, "UnsubscribeTopicByAccount"}, // 18.0.0+
            {16, &INpnsSystem::RejectUnavailableNintendoOnline, "DownloadSubscriptionList"}, // 18.0.0+
            {21, C<&INpnsSystem::CreateToken>, "CreateToken"},
            {22, C<&INpnsSystem::CreateTokenWithApplicationId>, "CreateTokenWithApplicationId"},
            {23, C<&INpnsSystem::DestroyToken>, "DestroyToken"},
            {24, C<&INpnsSystem::DestroyTokenWithApplicationId>, "DestroyTokenWithApplicationId"},
            {25, C<&INpnsSystem::QueryIsTokenValid>, "QueryIsTokenValid"},
            {26, D<&INpnsSystem::ListenToMyApplicationId>, "ListenToMyApplicationId"},
            {27, &INpnsSystem::RejectUnavailableNintendoOnline, "DestroyTokenAll"},
            {28, &INpnsSystem::RejectUnavailableNintendoOnline, "CreateTokenWithName"}, // 18.0.0+
            {29, &INpnsSystem::RejectUnavailableNintendoOnline, "DestroyTokenWithName"}, // 18.0.0+
            {31, &INpnsSystem::RejectUnavailableNintendoOnline, "UploadTokenToBaaS"},
            {32, &INpnsSystem::RejectUnavailableNintendoOnline, "DestroyTokenForBaaS"},
            {33, &INpnsSystem::RejectUnavailableNintendoOnline, "CreateTokenForBaaS"},
            {34, &INpnsSystem::RejectUnavailableNintendoOnline, "SetBaaSDeviceAccountIdList"},
            {35, &INpnsSystem::RejectUnavailableNintendoOnline, "LinkNsaId"}, // 17.0.0+
            {36, &INpnsSystem::RejectUnavailableNintendoOnline, "UnlinkNsaId"}, // 17.0.0+
            {37, &INpnsSystem::RejectUnavailableNintendoOnline, "RelinkNsaId"}, // 18.0.0+
            {40, &INpnsSystem::RejectUnavailableNintendoOnline, "GetNetworkServiceAccountIdTokenRequestEvent"}, // 17.0.0+
            {41, &INpnsSystem::RejectUnavailableNintendoOnline, "TryPopNetworkServiceAccountIdTokenRequestUid"}, // 17.0.0+
            {42, &INpnsSystem::RejectUnavailableNintendoOnline, "SetNetworkServiceAccountIdTokenSuccess"}, // 17.0.0+
            {43, &INpnsSystem::RejectUnavailableNintendoOnline, "SetNetworkServiceAccountIdTokenFailure"}, // 17.0.0+
            {44, &INpnsSystem::RejectUnavailableNintendoOnline, "SetUidList"}, // 17.0.0+
            {45, &INpnsSystem::RejectUnavailableNintendoOnline, "PutDigitalTwinKeyValue"}, // 17.0.0+
            {51, &INpnsSystem::RejectUnavailableNintendoOnline, "DeleteDigitalTwinKeyValue"}, // 18.0.0+
            {101, C<&INpnsSystem::Suspend>, "Suspend"},
            {102, C<&INpnsSystem::Resume>, "Resume"},
            {103, C<&INpnsSystem::GetState>, "GetState"},
            {104, C<&INpnsSystem::GetStatistics>, "GetStatistics"},
            {105, C<&INpnsSystem::GetPlayReportRequestEvent>, "GetPlayReportRequestEvent"},
            {106, C<&INpnsSystem::GetLastNotifiedTime>, "GetLastNotifiedTime"}, // 18.0.0+
            {107, C<&INpnsSystem::SetLastNotifiedTime>, "SetLastNotifiedTime"}, // 18.0.0+
            {111, &INpnsSystem::RejectUnavailableNintendoOnline, "GetJid"},
            {112, &INpnsSystem::RejectUnavailableNintendoOnline, "CreateJid"},
            {113, &INpnsSystem::RejectUnavailableNintendoOnline, "DestroyJid"},
            {114, &INpnsSystem::RejectUnavailableNintendoOnline, "AttachJid"},
            {115, &INpnsSystem::RejectUnavailableNintendoOnline, "DetachJid"},
            {120, &INpnsSystem::RejectUnavailableNintendoOnline, "CreateNotificationReceiver"},
            {151, C<&INpnsSystem::GetStateWithHandover>, "GetStateWithHandover"},
            {152, C<&INpnsSystem::GetStateChangeEventWithHandover>, "GetStateChangeEventWithHandover"},
            {153, C<&INpnsSystem::GetDropEventWithHandover>, "GetDropEventWithHandover"},
            {154, &INpnsSystem::RejectUnavailableNintendoOnline, "CreateTokenAsync"},
            {155, &INpnsSystem::RejectUnavailableNintendoOnline, "CreateTokenAsyncWithApplicationId"},
            {156, &INpnsSystem::RejectUnavailableNintendoOnline, "CreateTokenWithNameAsync"}, // 18.0.0+
            {161, C<&INpnsSystem::GetRequestChangeStateCancelEvent>, "GetRequestChangeStateCancelEvent"}, // 10.0.0+
            {162, &INpnsSystem::RejectUnavailableNintendoOnline, "RequestChangeStateForceTimedWithCancelEvent"},
            {201, &INpnsSystem::RejectUnavailableNintendoOnline, "RequestChangeStateForceTimed"},
            {202, C<&INpnsSystem::RequestChangeStateForceAsync>, "RequestChangeStateForceAsync"},
            {301, &INpnsSystem::RejectUnavailableNintendoOnline, "GetPassword"}, // 18.0.0+
            {302, &INpnsSystem::RejectUnavailableNintendoOnline, "GetAllImmigration"}, // 18.0.0+
            {303, &INpnsSystem::RejectUnavailableNintendoOnline, "GetNotificationHistories"}, // 18.0.0+
            {304, &INpnsSystem::RejectUnavailableNintendoOnline, "GetPersistentConnectionSummary"}, // 18.0.0+
            {305, &INpnsSystem::RejectUnavailableNintendoOnline, "GetDigitalTwinSummary"}, // 18.0.0+
            {306, &INpnsSystem::RejectUnavailableNintendoOnline, "GetDigitalTwinValue"}, // 18.0.0+
        };
        // clang-format on

        RegisterHandlers(functions);
    }

    ~INpnsSystem() override = default;

private:
    Result ListenAll() {
        LOG_DEBUG(Service_NPNS, "ListenAll accepted in offline mode");
        R_SUCCEED();
    }

    Result GetStateChangeEvent(OutCopyHandle<Kernel::KReadableEvent> out_event) {
        *out_event = get_state_change_event.GetHandle();
        R_SUCCEED();
    }

    Result CreateToken(Out<TokenValue> out_token, const TokenKey& key, ClientProcessId pid) {
        *out_token = token_store.Create(key);
        LOG_DEBUG(Service_NPNS, "Created local opaque push token for pid={}", pid.pid);
        R_SUCCEED();
    }

    Result CreateTokenWithApplicationId(Out<TokenValue> out_token, const TokenKey& key) {
        *out_token = token_store.Create(key);
        LOG_DEBUG(Service_NPNS, "Created local opaque push token with application-id key");
        R_SUCCEED();
    }

    Result DestroyToken(const TokenKey& key, ClientProcessId pid) {
        token_store.Destroy(key);
        LOG_DEBUG(Service_NPNS, "Destroyed local opaque push token for pid={}", pid.pid);
        R_SUCCEED();
    }

    Result DestroyTokenWithApplicationId(const TokenKey& key) {
        token_store.Destroy(key);
        LOG_DEBUG(Service_NPNS, "Destroyed local opaque push token with application-id key");
        R_SUCCEED();
    }

    Result QueryIsTokenValid(Out<bool> out_valid, const TokenValue& token) {
        *out_valid = token_store.IsValid(token);
        R_SUCCEED();
    }

    Result ListenToMyApplicationId(u64 application_id, ClientProcessId pid) {
        LOG_DEBUG(Service_NPNS,
                  "ListenToMyApplicationId accepted offline, application_id={:#x}, pid={}",
                  application_id, pid.pid);
        R_SUCCEED();
    }

    Result Suspend() {
        LOG_DEBUG(Service_NPNS, "Suspend accepted; no remote push connection exists");
        R_SUCCEED();
    }

    Result Resume() {
        LOG_DEBUG(Service_NPNS, "Resume accepted; no remote push connection exists");
        R_SUCCEED();
    }

    Result ListenUndelivered() {
        LOG_DEBUG(Service_NPNS, "ListenUndelivered accepted in offline mode");
        R_SUCCEED();
    }

    Result SubscribeTopic(InBuffer<BufferAttr_HipcPointer> topic) {
        LOG_DEBUG(Service_NPNS, "SubscribeTopic accepted offline, bytes={}", topic.size());
        R_SUCCEED();
    }

    Result UnsubscribeTopic(InBuffer<BufferAttr_HipcPointer> topic) {
        LOG_DEBUG(Service_NPNS, "UnsubscribeTopic accepted offline, bytes={}", topic.size());
        R_SUCCEED();
    }

    Result SubscribeTopicByAccount(u64 account_id, InBuffer<BufferAttr_HipcPointer> topic) {
        LOG_DEBUG(Service_NPNS, "SubscribeTopicByAccount accepted offline, account_id={:#x}, bytes={}",
                  account_id, topic.size());
        R_SUCCEED();
    }

    Result UnsubscribeTopicByAccount(u64 account_id, InBuffer<BufferAttr_HipcPointer> topic) {
        LOG_DEBUG(Service_NPNS, "UnsubscribeTopicByAccount accepted offline, account_id={:#x}, bytes={}",
                  account_id, topic.size());
        R_SUCCEED();
    }

    Result GetStateWithHandover(Out<u32> out_state) {
        *out_state = state;
        R_SUCCEED();
    }

    Result GetStateChangeEventWithHandover(OutCopyHandle<Kernel::KReadableEvent> out_event) {
        *out_event = get_state_change_event.GetHandle();
        R_SUCCEED();
    }

    Result GetDropEventWithHandover(OutCopyHandle<Kernel::KReadableEvent> out_event) {
        *out_event = get_drop_event.GetHandle();
        R_SUCCEED();
    }

    Result ListenTo(u64 program_id) {
        // Encore has no Nintendo push backend. Accept the subscription request locally so guest
        // software can continue, but do not pretend a remote subscription was created.
        LOG_DEBUG(Service_NPNS, "Push subscription accepted locally, program_id={}", program_id);
        R_SUCCEED();
    }

    Result GetReceiveEvent(OutCopyHandle<Kernel::KReadableEvent> out_event) {
        LOG_DEBUG(Service_NPNS, "Returning live push-notification receive event");

        *out_event = get_receive_event.GetHandle();
        R_SUCCEED();
    }

    Result ListenToByName(InBuffer<BufferAttr_HipcMapAlias> name_buffer) {
        const std::string name(reinterpret_cast<const char*>(name_buffer.data()), name_buffer.size());
        LOG_DEBUG(Service_NPNS, "System push subscription accepted locally by name, bytes={}",
                  name_buffer.size());

        // Encore has no Nintendo push backend. Validate and consume the documented input buffer,
        // while keeping the same explicit offline-subscription semantics as npns:u.
        (void)name;
        R_SUCCEED();
    }

    Result GetState(Out<u32> out_state) {
        *out_state = state;
        LOG_DEBUG(Service_NPNS, "Returning local push-notification state={}", *out_state);
        R_SUCCEED();
    }

    Result GetStatistics(OutBuffer<BufferAttr_HipcMapAlias> out_statistics) {
        // No Nintendo push transport is active in offline mode, therefore all counters are zero.
        std::fill(out_statistics.begin(), out_statistics.end(), u8{0});
        R_SUCCEED();
    }

    Result GetPlayReportRequestEvent(OutCopyHandle<Kernel::KReadableEvent> out_event) {
        // No remote play-report request is generated in offline mode. Return the live, unsignaled
        // event instead of a fabricated handle or a permanently-signaled notification.
        *out_event = get_play_report_request_event.GetHandle();
        R_SUCCEED();
    }

    Result GetLastNotifiedTime(Out<s64> out_last_notified_time) {
        *out_last_notified_time = last_notified_time;
        LOG_DEBUG(Service_NPNS, "Returning local last-notified time={}", last_notified_time);
        R_SUCCEED();
    }

    Result SetLastNotifiedTime(s64 notified_time) {
        last_notified_time = notified_time;
        LOG_DEBUG(Service_NPNS, "Updated local last-notified time={}", last_notified_time);
        R_SUCCEED();
    }

    Result RequestChangeStateForceAsync(u32 requested_state) {
        if (state != requested_state) {
            state = requested_state;
            get_state_change_event.Signal(system.Kernel());
        }
        LOG_DEBUG(Service_NPNS, "Applied local NPNS state transition to {}", state);
        R_SUCCEED();
    }

    Result GetRequestChangeStateCancelEvent(OutCopyHandle<Kernel::KReadableEvent> out_event) {
        LOG_DEBUG(Service_NPNS, "Returning live request-change-state cancel event");
        *out_event = get_request_change_state_cancel_event.GetHandle();
        R_SUCCEED();
    }

    KernelHelpers::ServiceContext service_context;
    Event get_receive_event;
    Event get_state_change_event;
    Event get_drop_event;
    Event get_request_change_state_cancel_event;
    Event get_play_report_request_event;
    OfflineTokenStore token_store;
    u32 state{};
    s64 last_notified_time{};

    // Encore NS1: unavailable Nintendo online backend; explicit error, never fake success.
    void RejectUnavailableNintendoOnline(HLERequestContext& ctx) {
        IPC::ResponseBuilder rb{ctx, 2};
        rb.Push(ResultUnknown);
    }

};

class INpnsUser final : public ServiceFramework<INpnsUser> {
public:
    explicit INpnsUser(Core::System& system_)
        : ServiceFramework{system_, "npns:u"}, service_context{system, "npns:u"},
          get_receive_event{service_context}, get_state_change_event{service_context},
          get_drop_event{service_context} {
        // clang-format off
        static const FunctionInfo functions[] = {
            {1, C<&INpnsUser::ListenAll>, "ListenAll"},
            {2, C<&INpnsUser::ListenTo>, "ListenTo"},
            {3, &INpnsUser::RejectUnavailableNintendoOnline, "Receive"},
            {4, &INpnsUser::RejectUnavailableNintendoOnline, "ReceiveRaw"},
            {5, C<&INpnsUser::GetReceiveEvent>, "GetReceiveEvent"},
            {7, C<&INpnsUser::GetStateChangeEvent>, "GetStateChangeEvent"},
            {8, C<&INpnsUser::ListenToByName>, "ListenToByName"}, // 18.0.0+
            {21, C<&INpnsUser::CreateToken>, "CreateToken"},
            {23, C<&INpnsUser::DestroyToken>, "DestroyToken"},
            {25, C<&INpnsUser::QueryIsTokenValid>, "QueryIsTokenValid"},
            {26, D<&INpnsUser::ListenToMyApplicationId>, "ListenToMyApplicationId"},
            {101, C<&INpnsUser::Suspend>, "Suspend"},
            {102, C<&INpnsUser::Resume>, "Resume"},
            {103, C<&INpnsUser::GetState>, "GetState"},
            {104, C<&INpnsUser::GetStatistics>, "GetStatistics"},
            {111, &INpnsUser::RejectUnavailableNintendoOnline, "GetJid"},
            {120, &INpnsUser::RejectUnavailableNintendoOnline, "CreateNotificationReceiver"},
            {151, C<&INpnsUser::GetStateWithHandover>, "GetStateWithHandover"},
            {152, C<&INpnsUser::GetStateChangeEventWithHandover>, "GetStateChangeEventWithHandover"},
            {153, C<&INpnsUser::GetDropEventWithHandover>, "GetDropEventWithHandover"},
            {154, &INpnsUser::RejectUnavailableNintendoOnline, "CreateTokenAsync"},
        };
        // clang-format on

        RegisterHandlers(functions);
    }

private:
    Result ListenAll() {
        LOG_DEBUG(Service_NPNS, "User ListenAll accepted in offline mode");
        R_SUCCEED();
    }

    Result GetStateChangeEvent(OutCopyHandle<Kernel::KReadableEvent> out_event) {
        *out_event = get_state_change_event.GetHandle();
        R_SUCCEED();
    }

    Result CreateToken(Out<TokenValue> out_token, const TokenKey& key, ClientProcessId pid) {
        *out_token = token_store.Create(key);
        LOG_DEBUG(Service_NPNS, "User created local opaque push token for pid={}", pid.pid);
        R_SUCCEED();
    }

    Result DestroyToken(const TokenKey& key, ClientProcessId pid) {
        token_store.Destroy(key);
        LOG_DEBUG(Service_NPNS, "User destroyed local opaque push token for pid={}", pid.pid);
        R_SUCCEED();
    }

    Result QueryIsTokenValid(Out<bool> out_valid, const TokenValue& token) {
        *out_valid = token_store.IsValid(token);
        R_SUCCEED();
    }

    Result ListenToMyApplicationId(u64 application_id, ClientProcessId pid) {
        LOG_DEBUG(Service_NPNS,
                  "User ListenToMyApplicationId accepted offline, application_id={:#x}, pid={}",
                  application_id, pid.pid);
        R_SUCCEED();
    }

    Result Suspend() {
        LOG_DEBUG(Service_NPNS, "User Suspend accepted; no remote push connection exists");
        R_SUCCEED();
    }

    Result Resume() {
        LOG_DEBUG(Service_NPNS, "User Resume accepted; no remote push connection exists");
        R_SUCCEED();
    }

    Result GetStateWithHandover(Out<u32> out_state) {
        *out_state = 0;
        R_SUCCEED();
    }

    Result GetStateChangeEventWithHandover(OutCopyHandle<Kernel::KReadableEvent> out_event) {
        *out_event = get_state_change_event.GetHandle();
        R_SUCCEED();
    }

    Result GetDropEventWithHandover(OutCopyHandle<Kernel::KReadableEvent> out_event) {
        *out_event = get_drop_event.GetHandle();
        R_SUCCEED();
    }

    Result ListenTo(u64 program_id) {
        LOG_DEBUG(Service_NPNS, "User push subscription accepted locally, program_id={}", program_id);
        R_SUCCEED();
    }

    Result GetState(Out<u32> out_state) {
        *out_state = 0;
        LOG_DEBUG(Service_NPNS, "Returning offline user push-notification state");
        R_SUCCEED();
    }

    Result GetStatistics(OutBuffer<BufferAttr_HipcMapAlias> out_statistics) {
        std::fill(out_statistics.begin(), out_statistics.end(), u8{0});
        R_SUCCEED();
    }

    Result ListenToByName(InBuffer<BufferAttr_HipcMapAlias> name_buffer) {
        const std::string name(reinterpret_cast<const char*>(name_buffer.data()), name_buffer.size());
        LOG_DEBUG(Service_NPNS, "called, name={}", name);

        // Store the name for future use if needed
        // For now, just acknowledge the registration
        R_SUCCEED();
    }

    Result GetReceiveEvent(OutCopyHandle<Kernel::KReadableEvent> out_event) {
        LOG_DEBUG(Service_NPNS, "called");

        *out_event = get_receive_event.GetHandle();
        R_SUCCEED();
    }

    KernelHelpers::ServiceContext service_context;
    Event get_receive_event;
    Event get_state_change_event;
    Event get_drop_event;
    OfflineTokenStore token_store;

    // Encore NS1: unavailable Nintendo online backend; explicit error, never fake success.
    void RejectUnavailableNintendoOnline(HLERequestContext& ctx) {
        IPC::ResponseBuilder rb{ctx, 2};
        rb.Push(ResultUnknown);
    }

};

void LoopProcess(Core::System& system) {
    auto server_manager = std::make_unique<ServerManager>(system);

    server_manager->RegisterNamedService("npns:s", std::make_shared<INpnsSystem>(system));
    server_manager->RegisterNamedService("npns:u", std::make_shared<INpnsUser>(system));
    ServerManager::RunServer(std::move(server_manager));
}

} // namespace Service::NPNS
