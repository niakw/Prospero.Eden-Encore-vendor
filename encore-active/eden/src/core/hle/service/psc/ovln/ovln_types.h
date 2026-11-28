// SPDX-FileCopyrightText: Copyright 2025 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

// SPDX-FileCopyrightText: Copyright 2024 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <memory>
#include <string>

#include "common/bit_field.h"
#include "common/common_types.h"
#include "core/hle/result.h"

namespace Core {
class System;
}

namespace Kernel {
class KEvent;
}

namespace Service::PSC {

using OverlayNotification = std::array<u64, 0x10>;
static_assert(sizeof(OverlayNotification) == 0x80, "OverlayNotification has incorrect size");

union MessageFlags {
    u64 raw;
    BitField<0, 8, u64> message_type;
    BitField<8, 8, u64> queue_type;
    BitField<0, 8, u64> enqueue_position;
    BitField<8, 8, u64> overflow_option;
};
static_assert(sizeof(MessageFlags) == 0x8, "MessageFlags has incorrect size");

struct SourceName {
    char name[0x10];

    std::string GetString() const {
        std::size_t length = 0;
        while (length < sizeof(name) && name[length] != '\0') {
            ++length;
        }
        return std::string{name, length};
    }
};
static_assert(sizeof(SourceName) == 0x10, "SourceName has incorrect size");

struct QueueAttribute {
    u32 queue_length;
    u32 reserved;
};
static_assert(sizeof(QueueAttribute) == 0x8, "QueueAttribute has incorrect size");

class OverlayNotificationBroker {
public:
    explicit OverlayNotificationBroker(Core::System& system_);
    ~OverlayNotificationBroker();

    OverlayNotificationBroker(const OverlayNotificationBroker&) = delete;
    OverlayNotificationBroker& operator=(const OverlayNotificationBroker&) = delete;

    u64 RegisterSender(std::string source_name, u32 queue_capacity);
    void UnregisterSender(u64 sender_id);
    u64 RegisterReceiver(Kernel::KEvent* event);
    void UnregisterReceiver(u64 receiver_id);

    Result AddSource(u64 receiver_id, std::string source_name);
    Result RemoveSource(u64 receiver_id, std::string source_name);
    Result Send(u64 sender_id, MessageFlags flags, const OverlayNotification& notification);
    u32 GetUnreceivedMessageCount(u64 sender_id) const;
    Result Receive(u64 receiver_id, OverlayNotification* out_notification, MessageFlags* out_flags,
                   u64* out_tick);

private:
    struct Impl;

    Core::System& system;
    std::unique_ptr<Impl> impl;
};

} // namespace Service::PSC
