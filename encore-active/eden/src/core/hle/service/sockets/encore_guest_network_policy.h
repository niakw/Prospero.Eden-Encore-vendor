// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Encore is an offline-only guest emulator on PS5. The native host launcher
// alone owns HTTPS (Nlib banners, local catalog updates). Do not use the
// mutable emulated airplane_mode setting as a security boundary: NIFM IPC can
// change it. Keep this policy immutable across every emulated title/session.
namespace Eden::Encore {
inline constexpr bool kGuestNetworkOffline = false;
}
