// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 ProsperoGallery contributors; adapted for Eden Encore.
// Converts ONE chosen JPEG XR capture into bounded tone-mapped SDR WebP.
// Never enumerate/convert the PS5 gallery from this codec.
#pragma once
#include <stdbool.h>
#ifdef __cplusplus
extern "C" {
#endif
bool encore_jxr_to_webp(const char *src, const char *dst);
#ifdef __cplusplus
}
#endif
