// SPDX-FileCopyrightText: Copyright 2024 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "core/hle/service/cmif_serialization.h"
#include "core/hle/service/psc/ovln/sender.h"

namespace Service::PSC {

ISender::ISender(Core::System& system_, SourceName source_name_, QueueAttribute attribute)
    : ServiceFramework{system_, "ISender"}, source_name{source_name_.GetString()},
      queue_capacity{attribute.queue_length} {
    // clang-format off
    static const FunctionInfo functions[] = {
        {0, D<&ISender::Send>, "Send"},
        {1, D<&ISender::GetUnreceivedMessageCount>, "GetUnreceivedMessageCount"},
    };
    // clang-format on

    RegisterHandlers(functions);
}

ISender::~ISender() = default;

Result ISender::Send(MessageFlags flags, const OverlayNotification& notification) {
    std::scoped_lock lk{queue_mutex};

    if (queue_capacity == 0) {
        LOG_WARNING(Service_PSC, "Send rejected for source '{}' with zero queue capacity", source_name);
        R_THROW(ResultUnknown);
    }

    if (queue.size() >= queue_capacity) {
        switch (static_cast<u8>(flags.overflow_option)) {
        case 1: // RemoveFront
            queue.erase(queue.begin());
            break;
        case 2: // RemoveBack
            queue.pop_back();
            break;
        case 0: // Error
        case 3: // Block: no receiver is wired to this local queue, so blocking cannot make progress.
        default:
            LOG_WARNING(Service_PSC, "Send queue full for source '{}', capacity={}, overflow={}",
                        source_name, queue_capacity, static_cast<u8>(flags.overflow_option));
            R_THROW(ResultUnknown);
        }
    }

    switch (static_cast<u8>(flags.enqueue_position)) {
    case 0: // Front
        queue.insert(queue.begin(), notification);
        break;
    case 1: // Back
        queue.push_back(notification);
        break;
    default:
        LOG_WARNING(Service_PSC, "Invalid enqueue position {} for source '{}'",
                    static_cast<u8>(flags.enqueue_position), source_name);
        R_THROW(ResultUnknown);
    }

    LOG_DEBUG(Service_PSC, "queued overlay notification for source '{}', unreceived={}",
              source_name, queue.size());
    R_SUCCEED();
}

Result ISender::GetUnreceivedMessageCount(Out<u32> out_count) {
    std::scoped_lock lk{queue_mutex};
    *out_count = static_cast<u32>(queue.size());
    LOG_DEBUG(Service_PSC, "called, source='{}', count={}", source_name, *out_count);
    R_SUCCEED();
}

} // namespace Service::PSC
