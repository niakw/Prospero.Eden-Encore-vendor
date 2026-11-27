// SPDX-FileCopyrightText: Copyright 2026 Oliv3rmoon
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Pure host-path discovery, shared by the guest aoc:u and fsp-srv services.
// A bare extracted RomFS cannot provide CNMT metadata: require an explicit,
// canonical 16-hex-digit AOC Title ID to avoid associating content by name.
#include <algorithm>
#include <charconv>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

namespace FileSys::ExtractedAoc {

struct Entry {
    std::uint64_t title_id{};
    std::filesystem::path romfs_path;
};

inline bool SafeDirectory(const std::filesystem::path& path) {
    std::error_code ec;
    const auto status = std::filesystem::symlink_status(path, ec);
    return !ec && std::filesystem::is_directory(status) &&
           !std::filesystem::is_symlink(status);
}

inline std::optional<std::uint64_t> ParseTitleId(std::string_view name) {
    if (name.size() != 16) return std::nullopt;
    std::uint64_t id{};
    const auto [end, error] = std::from_chars(name.data(), name.data() + name.size(), id, 16);
    if (error != std::errc{} || end != name.data() + name.size() ||
        (id & 0x1fffU) < 0x1000U || (id & 0x1fffU) > 0x17ffU)
        return std::nullopt;
    return id;
}

// An AOC is only associated with the base ID encoded in its AOC title ID.
// Never guess from a game name, "DLC_1" filename, or unrelated folder.
inline bool BelongsToBase(std::uint64_t aoc, std::uint64_t base) {
    return (aoc & ~std::uint64_t{0x1fff}) == (base & ~std::uint64_t{0x1fff}) &&
           (aoc & 0x1fffU) >= 0x1000U && (aoc & 0x1fffU) <= 0x17ffU;
}

inline std::vector<Entry> Scan(const std::vector<std::string>& roots) {
    namespace fs = std::filesystem;
    std::vector<Entry> found;
    std::size_t budget = 0;
    // Keep this bounded independently of the size of an extracted RomFS.
    for (const auto& root_string : roots) {
        if (budget >= 512 || found.size() >= 128) break;
        const fs::path root{root_string};
        if (root.empty() || !SafeDirectory(root)) continue;
        struct Pending {
            fs::path path;
            std::optional<std::uint64_t> id;
            unsigned depth;
            unsigned distance_from_id;
        };
        std::vector<Pending> todo{{root, ParseTitleId(root.filename().string()), 0, 0}};
        while (!todo.empty() && budget++ < 512 && found.size() < 128) {
            auto item = std::move(todo.back());
            todo.pop_back();
            if (!SafeDirectory(item.path)) continue;
            std::error_code error;
            std::size_t examined = 0;
            for (fs::directory_iterator it{item.path,
                     fs::directory_options::skip_permission_denied, error}, end;
                 !error && it != end && examined++ < 128; it.increment(error)) {
                const auto path = it->path();
                const auto name = path.filename().string();
                if (name.empty() || name.front() == '.' || !SafeDirectory(path))
                    continue;
                if (name == "romfs") {
                    if (item.id && item.distance_from_id <= 2)
                        found.push_back({*item.id, path});
                    // Never descend into actual game assets.
                    continue;
                }
                if (name == "exefs" || item.depth >= 7 || todo.size() >= 128)
                    continue;
                auto id = ParseTitleId(name);
                if (id && item.id && id != item.id) continue;
                todo.push_back({path, id ? id : item.id, item.depth + 1,
                                id ? 0U : item.distance_from_id + 1});
            }
        }
    }
    std::sort(found.begin(), found.end(), [](const Entry& a, const Entry& b) {
        return a.title_id == b.title_id ?
               a.romfs_path.generic_string() < b.romfs_path.generic_string() :
               a.title_id < b.title_id;
    });
    // Reject ambiguous duplicate AOC IDs. Picking arbitrary first roots would
    // create different guest DLC contents depending on directory order.
    std::vector<Entry> unique;
    for (std::size_t i = 0; i < found.size();) {
        std::size_t j = i + 1;
        while (j < found.size() && found[j].title_id == found[i].title_id) ++j;
        if (j == i + 1) unique.push_back(found[i]);
        i = j;
    }
    return unique;
}

} // namespace FileSys::ExtractedAoc
