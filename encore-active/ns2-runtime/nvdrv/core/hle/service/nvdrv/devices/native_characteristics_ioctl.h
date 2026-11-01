// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later
// Native characteristics IOCTL envelope, isolated to research mode.
// The GPU capability response does not imply native Ampere execution.
#pragma once

#include <algorithm>
#include <array>
#include <cstring>
#include <span>
#include "common/common_types.h"
#include "common/swap.h"
#include "core/hle/service/nvdrv/devices/native_gpu_characteristics.h"

namespace Service::Nvidia::Devices {

enum class NativeCharacteristicsIoctlStatus {
    NotNative,
    BadParameter,
    Success,
};

// Accept only exact native V2 extended IOCTL command IDs. The legacy call
// and any unsupported command remain owned by the original NVDRV handler.
inline NativeCharacteristicsIoctlStatus WriteNativeCharacteristicsIoctlReply(
    u32 raw, std::span<const u8> input, std::span<u8> output,
    std::span<u8> inline_output, std::span<const u8, 0xa0> legacy) {
    if (raw != 0xc0e04705U && raw != 0xc0e84705U) {
        return NativeCharacteristicsIoctlStatus::NotNative;
    }
    const size_t size = raw == 0xc0e84705U
                            ? ExtendedNativeGpuCharacteristicsSize
                            : NativeGpuCharacteristicsSize;
    const size_t packet_size = 0x10 + size;
    // Never change output buffers until ALL lengths validate.
    if (input.size() < packet_size || output.size() < packet_size ||
        inline_output.size() < size) {
        return NativeCharacteristicsIoctlStatus::BadParameter;
    }
    std::array<u8, ExtendedNativeGpuCharacteristicsSize> native{};
    if (size == ExtendedNativeGpuCharacteristicsSize) {
        native = MakeNativeGpuCharacteristics<ExtendedNativeGpuCharacteristicsSize>(legacy);
    } else {
        const auto original = MakeNativeGpuCharacteristics(legacy);
        std::copy(original.begin(), original.end(), native.begin());
    }
    // Mirror the native upstream's reply structure without touching trailing
    // bytes owned by the caller or exposing uninitialised memory.
    std::fill_n(output.begin(), packet_size, u8{});
    std::fill_n(inline_output.begin(), size, u8{});
    std::memcpy(output.data(), input.data(), 0x10);
    const u64_le native_size{static_cast<u64>(size)};
    std::memcpy(output.data(), &native_size, sizeof(native_size));
    std::memcpy(output.data() + 0x10, native.data(), size);
    std::memcpy(inline_output.data(), native.data(), size);
    return NativeCharacteristicsIoctlStatus::Success;
}

} // namespace Service::Nvidia::Devices
