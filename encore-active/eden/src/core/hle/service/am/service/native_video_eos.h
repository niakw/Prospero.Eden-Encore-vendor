// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <deque>

namespace Service::AM::VideoDecoderProtocol {
// Reuse the final genuine decoded frame to report EOS, even if its packet
// was delivered in an earlier IPC call. Never invent an image or signal a
// GPU fence for a frame that did not exist.
template <typename Pending, typename Output>
[[nodiscard]] bool MarkLastEos(std::deque<Pending>& pending,
                               std::deque<Output>& outputs) {
    if (!pending.empty()) {
        pending.back().eos = true;
        return true;
    }
    if (!outputs.empty()) {
        outputs.back().eos = true;
        return true;
    }
    return false;
}
} // namespace Service::AM::VideoDecoderProtocol
