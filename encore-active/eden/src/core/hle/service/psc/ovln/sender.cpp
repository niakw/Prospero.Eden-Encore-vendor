// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

// SPDX-FileCopyrightText: Copyright 2024 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <deque>
#include <map>
#include <mutex>
#include <set>
#include <string>
#include <utility>

#include "core/core.h"
#include "core/hle/kernel/k_event.h"
#include "core/hle/service/cmif_serialization.h"
#include "core/hle/service/psc/ovln/sender.h"

namespace Service::PSC {

struct OverlayNotificationBroker::Impl {
    struct QueuedMessage {
        OverlayNotification notification{};
        MessageFlags flags{};
        u64 sequence{};
    };
    struct SenderState {
        std::string source_name;
        u32 queue_capacity{};
        std::deque<QueuedMessage> queue;
    };
    struct ReceiverState {
        Kernel::KEvent* event{};
        std::set<std::string> sources;
    };

    bool HasPending(const ReceiverState& receiver) const {
        for (const auto& [_, sender] : senders) {
            if (!sender.queue.empty() &&
                receiver.sources.find(sender.source_name) != receiver.sources.end()) {
                return true;
            }
        }
        return false;
    }

    mutable std::mutex mutex;
    std::map<u64, SenderState> senders;
    std::map<u64, ReceiverState> receivers;
    u64 next_sender_id{1};
    u64 next_receiver_id{1};
    u64 next_sequence{1};
};

OverlayNotificationBroker::OverlayNotificationBroker(Core::System& system_)
    : system{system_}, impl{std::make_unique<Impl>()} {}
OverlayNotificationBroker::~OverlayNotificationBroker() = default;

u64 OverlayNotificationBroker::RegisterSender(std::string source_name, u32 queue_capacity) {
    std::scoped_lock lk{impl->mutex};
    const u64 sender_id = impl->next_sender_id++;
    impl->senders.emplace(sender_id,
                          Impl::SenderState{std::move(source_name), queue_capacity, {}});
    return sender_id;
}

void OverlayNotificationBroker::UnregisterSender(u64 sender_id) {
    std::scoped_lock lk{impl->mutex};
    impl->senders.erase(sender_id);
    for (auto& [_, receiver] : impl->receivers) {
        if (receiver.event != nullptr && !impl->HasPending(receiver)) {
            receiver.event->Clear(system.Kernel());
        }
    }
}

u64 OverlayNotificationBroker::RegisterReceiver(Kernel::KEvent* event) {
    std::scoped_lock lk{impl->mutex};
    const u64 receiver_id = impl->next_receiver_id++;
    impl->receivers.emplace(receiver_id, Impl::ReceiverState{event, {}});
    return receiver_id;
}

void OverlayNotificationBroker::UnregisterReceiver(u64 receiver_id) {
    std::scoped_lock lk{impl->mutex};
    impl->receivers.erase(receiver_id);
}

Result OverlayNotificationBroker::AddSource(u64 receiver_id, std::string source_name) {
    std::scoped_lock lk{impl->mutex};
    const auto receiver_it = impl->receivers.find(receiver_id);
    if (receiver_it == impl->receivers.end()) {
        return ResultUnknown;
    }
    auto& receiver = receiver_it->second;
    receiver.sources.insert(std::move(source_name));
    if (receiver.event != nullptr && impl->HasPending(receiver)) {
        receiver.event->Signal(system.Kernel());
    }
    return ResultSuccess;
}

Result OverlayNotificationBroker::RemoveSource(u64 receiver_id, std::string source_name) {
    std::scoped_lock lk{impl->mutex};
    const auto receiver_it = impl->receivers.find(receiver_id);
    if (receiver_it == impl->receivers.end()) {
        return ResultUnknown;
    }
    auto& receiver = receiver_it->second;
    receiver.sources.erase(source_name);
    if (receiver.event != nullptr && !impl->HasPending(receiver)) {
        receiver.event->Clear(system.Kernel());
    }
    return ResultSuccess;
}

Result OverlayNotificationBroker::Send(u64 sender_id, MessageFlags flags,
                                       const OverlayNotification& notification) {
    std::scoped_lock lk{impl->mutex};
    const auto sender_it = impl->senders.find(sender_id);
    if (sender_it == impl->senders.end()) {
        return ResultUnknown;
    }
    auto& sender = sender_it->second;
    const auto enqueue_position = static_cast<u8>(flags.enqueue_position);
    const auto overflow_option = static_cast<u8>(flags.overflow_option);
    if (enqueue_position > 1 || overflow_option > 3 || sender.queue_capacity == 0) {
        return ResultUnknown;
    }

    if (sender.queue.size() >= sender.queue_capacity) {
        switch (overflow_option) {
        case 1:
            sender.queue.pop_front();
            break;
        case 2:
            sender.queue.pop_back();
            break;
        case 0:
        case 3:
        default:
            return ResultUnknown;
        }
    }

    Impl::QueuedMessage message{notification, flags, impl->next_sequence++};
    if (enqueue_position == 0) {
        sender.queue.push_front(message);
    } else {
        sender.queue.push_back(message);
    }

    for (auto& [_, receiver] : impl->receivers) {
        if (receiver.event != nullptr &&
            receiver.sources.find(sender.source_name) != receiver.sources.end()) {
            receiver.event->Signal(system.Kernel());
        }
    }
    return ResultSuccess;
}

u32 OverlayNotificationBroker::GetUnreceivedMessageCount(u64 sender_id) const {
    std::scoped_lock lk{impl->mutex};
    const auto sender_it = impl->senders.find(sender_id);
    return sender_it == impl->senders.end() ? 0
                                            : static_cast<u32>(sender_it->second.queue.size());
}

Result OverlayNotificationBroker::Receive(u64 receiver_id,
                                          OverlayNotification* out_notification,
                                          MessageFlags* out_flags, u64* out_tick) {
    std::scoped_lock lk{impl->mutex};
    const auto receiver_it = impl->receivers.find(receiver_id);
    if (receiver_it == impl->receivers.end()) {
        return ResultUnknown;
    }

    auto& receiver = receiver_it->second;
    auto target = impl->senders.end();
    u64 target_sequence{};
    for (auto it = impl->senders.begin(); it != impl->senders.end(); ++it) {
        auto& sender = it->second;
        if (sender.queue.empty() ||
            receiver.sources.find(sender.source_name) == receiver.sources.end()) {
            continue;
        }
        const u64 sequence = sender.queue.front().sequence;
        if (target == impl->senders.end() || sequence < target_sequence) {
            target = it;
            target_sequence = sequence;
        }
    }

    if (target == impl->senders.end()) {
        *out_notification = {};
        *out_flags = {};
        *out_tick = 0;
        if (receiver.event != nullptr) {
            receiver.event->Clear(system.Kernel());
        }
        return ResultUnknown;
    }

    const auto message = target->second.queue.front();
    target->second.queue.pop_front();
    *out_notification = message.notification;
    *out_flags = message.flags;
    *out_tick = 0;
    if (receiver.event != nullptr && !impl->HasPending(receiver)) {
        receiver.event->Clear(system.Kernel());
    }
    return ResultSuccess;
}

ISender::ISender(Core::System& system_, SourceName source_name, QueueAttribute attribute,
                 std::shared_ptr<OverlayNotificationBroker> broker_)
    : ServiceFramework{system_, "ISender"}, broker{std::move(broker_)} {
    // clang-format off
    static const FunctionInfo functions[] = {
        {0, D<&ISender::Send>, "Send"},
        {1, D<&ISender::GetUnreceivedMessageCount>, "GetUnreceivedMessageCount"},
    };
    // clang-format on
    RegisterHandlers(functions);
    sender_id = broker->RegisterSender(source_name.GetString(), attribute.queue_length);
}

ISender::~ISender() {
    broker->UnregisterSender(sender_id);
}

Result ISender::Send(MessageFlags flags, const OverlayNotification& notification) {
    const Result result = broker->Send(sender_id, flags, notification);
    LOG_DEBUG(Service_PSC, "Send result={:#x}, unreceived={}", result.raw,
              broker->GetUnreceivedMessageCount(sender_id));
    R_RETURN(result);
}

Result ISender::GetUnreceivedMessageCount(Out<u32> out_count) {
    *out_count = broker->GetUnreceivedMessageCount(sender_id);
    LOG_DEBUG(Service_PSC, "called, count={}", *out_count);
    R_SUCCEED();
}

} // namespace Service::PSC
