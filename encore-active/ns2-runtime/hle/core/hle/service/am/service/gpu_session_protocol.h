// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Strict subset of the NS2 GPU-session TIPC command ABI.
// Sourced from 0x2593/Eden-NS2@5d2d3cf0 gpu_session.cpp; the execution
// backend, NVDRV fd lifetime and Kernel event ownership are NOT ported here.

#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <span>

namespace Service::AM::GpuSessionProtocol {
using U32 = std::uint32_t;
using U64 = std::uint64_t;

inline constexpr std::size_t MaxDevices = 256;
inline constexpr std::size_t MaxAddressSpaces = 8;
inline constexpr std::size_t MaxNotificationEvents = 64;
enum class Status {
    Success,
    InvalidPacket,
    Unsupported,
    NotFound,
    OutOfResource,
    InvalidState,
};
struct Device {
    U64 name{};
    U64 debug_label{};
};
struct CreateDeviceRequest {
    U64 name{};
    U64 debug_label{};
};
struct PartitionRequest {
    U32 address_space{};
    bool user{};
};
constexpr U64 ReadPair(U32 lo, U32 hi) {
    return U64{lo} | (U64{hi} << 32);
}
inline constexpr std::optional<CreateDeviceRequest> DecodeCreateDevice(
    std::span<const U32> packet) {
    if (packet.size() != 8 || packet[0] != 0x39 || packet[1] != 6 ||
        ReadPair(packet[4], packet[5]) != 0) {
        return std::nullopt;
    }
    return CreateDeviceRequest{ReadPair(packet[2], packet[3]),
                               ReadPair(packet[6], packet[7])};
}
inline constexpr std::optional<U32> DecodeDestroyDevice(std::span<const U32> packet) {
    if (packet.size() != 3 || packet[0] != 0x3a || packet[1] != 1 ||
        packet[2] == 0 || packet[2] >= MaxDevices) {
        return std::nullopt;
    }
    return packet[2];
}
// Command 33 is a real NVDRV address-space allocation, not a guest GPU
// mapping. Reject reserved payload words and do not truncate handle IDs.
inline constexpr bool IsCreateAddressSpace(std::span<const U32> packet) {
    return packet.size() == 3 && packet[0] == 0x31 && packet[1] == 1 &&
           packet[2] == 0;
}
inline constexpr std::optional<U32> DecodeRegisterAddressSpace(
    std::span<const U32> packet) {
    if (packet.size() != 3 || packet[0] != 0x1c2 || packet[1] != 1 ||
        packet[2] == 0 || packet[2] > std::numeric_limits<std::int32_t>::max())
        return {};
    return packet[2];
}

// Bounds and ownership are enforced separately from the actual NVDRV
// allocator; failed opens / ioctls must never consume a slot.
class AddressSpaceSlots {
public:
    [[nodiscard]] bool HasCapacity() const { return count < MaxAddressSpaces; }
    [[nodiscard]] std::size_t Count() const { return count; }
    [[nodiscard]] bool Owns(U32 id) const {
        for (const auto& slot : slots)
            if (slot && slot->id == id) return true;
        return false;
    }
    [[nodiscard]] bool Registered(U32 id) const {
        for (const auto& slot : slots)
            if (slot && slot->id == id) return slot->registered;
        return false;
    }
    Status Add(U32 id) {
        if (id == 0 || id > std::numeric_limits<std::int32_t>::max())
            return Status::InvalidPacket;
        if (Owns(id)) return Status::InvalidState;
        for (auto& slot : slots) {
            if (!slot) {
                slot = Slot{id, false};
                ++count;
                return Status::Success;
            }
        }
        return Status::OutOfResource;
    }
    Status Register(U32 id) {
        for (auto& slot : slots) {
            if (slot && slot->id == id) {
                slot->registered = true;
                return Status::Success;
            }
        }
        return Status::NotFound;
    }
    [[nodiscard]] std::array<std::optional<U32>, MaxAddressSpaces> OwnedIds() const {
        std::array<std::optional<U32>, MaxAddressSpaces> ids{};
        for (std::size_t i = 0; i < slots.size(); ++i)
            if (slots[i]) ids[i] = slots[i]->id;
        return ids;
    }
private:
    struct Slot { U32 id; bool registered; };
    std::array<std::optional<Slot>, MaxAddressSpaces> slots{};
    std::size_t count{};
};

// Verify the returned NVDRV user region before multiplying or adding.
inline constexpr std::optional<U64> AddressSpaceRegionEnd(
    U64 offset, U64 pages, U64 page_size) {
    if (!pages || !page_size || pages >
        std::numeric_limits<U64>::max() / page_size) return {};
    const U64 bytes = pages * page_size;
    if (offset > std::numeric_limits<U64>::max() - (bytes - 1)) return {};
    return offset + bytes - 1;
}

inline constexpr bool IsGetNotificationRange(std::span<const U32> packet) {
    return packet.size() == 2 && packet[0] == 0x1b && packet[1] == 0;
}
inline constexpr std::optional<U32> DecodeEventIndex(std::span<const U32> packet,
                                                        U32 command) {
    // TIPC command 12 = 0x1c, command 21 = 0x25.
    if (packet.size() != 3 || packet[0] != command ||
        packet[1] != 1 || packet[2] >= MaxNotificationEvents) {
        return std::nullopt;
    }
    return packet[2];
}
inline constexpr std::optional<PartitionRequest> DecodeAddressSpacePartition(
    std::span<const U32> packet, U32 command) {
    if (command != 202 && command != 203) {
        return std::nullopt;
    }
    if (packet.size() != 3 || packet[0] != 16 + command ||
        packet[1] != 1) {
        return std::nullopt;
    }
    return PartitionRequest{packet[2], command == 203};
}
inline constexpr bool IsAcquireWorker(std::span<const U32> packet) {
    return packet.size() == 2 && packet[0] == 0x26 && packet[1] == 0;
}

class DeviceSlots {
public:
    // The upstream session reserves id 0 and selects the first empty id 1..255.
    struct Creation {
        Status status{Status::InvalidPacket};
        U32 id{};
    };
    Creation Create(std::span<const U32> packet) {
        if (packet.size() != 8 || packet[0] != 0x39 || packet[1] != 6)
            return {};
        if (ReadPair(packet[4], packet[5]) != 0)
            return {Status::Unsupported, 0};
        const auto request = DecodeCreateDevice(packet);
        if (!request)
            return {};
        for (std::size_t i = 1; i < devices.size(); ++i) {
            if (!devices[i]) {
                devices[i] = Device{request->name, request->debug_label};
                ++count;
                return {Status::Success, static_cast<U32>(i)};
            }
        }
        return {Status::OutOfResource, 0};
    }
    Status Destroy(std::span<const U32> packet) {
        const auto id = DecodeDestroyDevice(packet);
        if (!id)
            return Status::InvalidPacket;
        auto& selected = devices[*id];
        if (!selected)
            return Status::NotFound;
        selected.reset();
        --count;
        return Status::Success;
    }
    const std::optional<Device>& Get(U32 id) const {
        if (id >= MaxDevices)
            return devices[0];  // reserved null slot, no OOB
        return devices[id];
    }
    [[nodiscard]] std::size_t Count() const { return count; }
private:
    std::array<std::optional<Device>, MaxDevices> devices{};
    std::size_t count{};
};

class NotificationSlots {
public:
    Status MarkEvent(U32 index) {
        if (index >= MaxNotificationEvents)
            return Status::InvalidPacket;
        events[index] = true;
        return Status::Success;
    }
    Status RequestWorker(std::span<const U32> packet) const {
        const auto index = DecodeEventIndex(packet, 0x25);
        if (!index)
            return Status::InvalidPacket;
        return events[*index] ? Status::Success : Status::InvalidState;
    }
    bool HasEvent(U32 index) const {
        return index < MaxNotificationEvents && events[index];
    }
private:
    std::array<bool, MaxNotificationEvents> events{};
};

} // namespace Service::AM::GpuSessionProtocol
