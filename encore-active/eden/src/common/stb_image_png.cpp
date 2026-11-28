// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

// Active PS5 stb implementation: Eden's pinned common/stb.h builds ONLY JPEG.
// The launcher also reads genuine LaunchBox PNG box fronts, screenshot scans
// and profile avatars. Their magic signatures are valid but stb_info fails
// because PNG decode was compiled out. Build exactly ONE stb implementation
// with BOTH required decoders, retaining upstream resize/write semantics.
// This is selected instead of the pinned src/common/stb.cpp (never alongside).
#define STBI_ONLY_JPEG 1
#define STBI_ONLY_PNG 1
#define STBI_WRITE_NO_STDIO 1
#define STB_IMAGE_IMPLEMENTATION 1
#define STB_IMAGE_RESIZE_IMPLEMENTATION 1
#define STB_IMAGE_WRITE_IMPLEMENTATION 1
#include <stb_image.h>
#include <stb_image_resize.h>
#include <stb_image_write.h>
