// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// This classifier describes EXISTING Eden container/loader routes, not
// Switch 2 decryption, entitlement, game-card, GPU, or title compatibility.
// A filename alone cannot identify a Switch 2-exclusive game, a Switch 2
// Edition upgrade, or a game-key card.
#include <string_view>

namespace Eden::NS2::Content {
enum class Kind {
    ExeFSMain, NRO, NSO, NCA, NAX, XCI, XCZ, DXCI, NSP, NSZ, DNSP, NX, KIP,
    NPDMMetadata, RomFSData, Unsupported,
};
enum class Route {
    Executable, ExistingContainer, ExistingArchive, MetadataOnly, AssetsOnly, Unknown,
};
constexpr char Lower(char c) {
    return c >= 'A' && c <= 'Z' ? static_cast<char>(c + ('a' - 'A')) : c;
}
constexpr bool EqualFold(std::string_view a, std::string_view b) {
    if (a.size() != b.size()) return false;
    for (std::size_t i = 0; i < a.size(); ++i) {
        if (Lower(a[i]) != Lower(b[i])) return false;
    }
    return true;
}
constexpr Kind IdentifyName(std::string_view name) {
    // These two names have special meaning in the maintained loader.
    if (name == "main") return Kind::ExeFSMain;
    if (name == "00") return Kind::NCA;
    if (EqualFold(name, "romfs") || EqualFold(name, "romfs.bin"))
        return Kind::RomFSData;
    const auto dot = name.rfind('.');
    if (dot == std::string_view::npos || dot == name.size()-1)
        return Kind::Unsupported;
    const auto ext = name.substr(dot + 1);
    if (EqualFold(ext, "nro")) return Kind::NRO;
    if (EqualFold(ext, "nso")) return Kind::NSO;
    if (EqualFold(ext, "nca")) return Kind::NCA;
    if (EqualFold(ext, "nax")) return Kind::NAX;
    if (EqualFold(ext, "xci")) return Kind::XCI;
    if (EqualFold(ext, "xcz")) return Kind::XCZ;
    if (EqualFold(ext, "dxci")) return Kind::DXCI;
    if (EqualFold(ext, "nsp")) return Kind::NSP;
    if (EqualFold(ext, "nsz")) return Kind::NSZ;
    if (EqualFold(ext, "dnsp")) return Kind::DNSP;
    if (EqualFold(ext, "nx")) return Kind::NX;
    if (EqualFold(ext, "kip")) return Kind::KIP;
    if (EqualFold(ext, "npdm")) return Kind::NPDMMetadata;
    return Kind::Unsupported;
}
constexpr Route LoaderRoute(Kind kind) {
    switch (kind) {
    case Kind::ExeFSMain: case Kind::NRO: case Kind::NSO: case Kind::KIP:
        return Route::Executable;
    case Kind::NSP: case Kind::NSZ: case Kind::DNSP:
    case Kind::XCI: case Kind::XCZ: case Kind::DXCI: case Kind::NX:
        return Route::ExistingContainer;
    case Kind::NCA: case Kind::NAX:
        return Route::ExistingArchive;
    case Kind::NPDMMetadata: return Route::MetadataOnly;
    case Kind::RomFSData: return Route::AssetsOnly;
    default: return Route::Unknown;
    }
}
// New compressed/decrypted aliases and NX must not be admitted by a filename
// alone. The real NSP/XCI/NX probes verify the header before any loader runs.
constexpr bool RequiresRecognizedContent(Kind kind) {
    return kind == Kind::NSZ || kind == Kind::XCZ ||
           kind == Kind::DNSP || kind == Kind::DXCI ||
           kind == Kind::NX;
}
constexpr bool MayFallbackByName(Kind kind) {
    return !RequiresRecognizedContent(kind);
}
constexpr bool HasExistingLoaderRoute(Kind kind) {
    return LoaderRoute(kind) == Route::Executable ||
           LoaderRoute(kind) == Route::ExistingArchive ||
           LoaderRoute(kind) == Route::ExistingContainer;
}
} // namespace Eden::NS2::Content
