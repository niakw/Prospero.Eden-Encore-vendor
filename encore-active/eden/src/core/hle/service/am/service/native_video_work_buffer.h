// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>

namespace Service::AM::VideoDecoderProtocol {
// A work buffer belongs to the process that opened the native GSV session.
// A borrowed IPC COPY handle alone is not a lifetime lease: the KAutoObject
// must remain open until the decoder finalizes or its session is destroyed.
template <typename Transfer, typename Process>
[[nodiscard]] bool IsWorkBufferOwned(const Transfer* transfer, const Process* process) {
    return transfer && process && transfer->IsInitialized() &&
           transfer->GetOwner() == process;
}

[[nodiscard]] inline bool IsValidWorkBufferSize(std::size_t bytes) {
    constexpr std::size_t GuestPageSize = 4096;
    return bytes >= GuestPageSize && bytes % GuestPageSize == 0;
}
} // namespace Service::AM::VideoDecoderProtocol
