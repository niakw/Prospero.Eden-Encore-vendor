// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

// SPDX-FileCopyrightText: Copyright 2018 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <algorithm>
#include <cstring>
#include "common/common_types.h"
#include "common/logging.h"
#include "common/swap.h"
#include "core/file_sys/nca_metadata.h"
#include "core/file_sys/vfs/vfs.h"

namespace FileSys {

CNMT::CNMT(VirtualFile file) {
    if (file->ReadObject(&header) != sizeof(CNMTHeader)) {
        return;
    }

    const auto extended_size = static_cast<std::size_t>(header.table_offset);
    if (extended_size > file->GetSize() - (std::min)(file->GetSize(), sizeof(CNMTHeader))) {
        LOG_WARNING(Loader, "Invalid CNMT extended header size {}.", extended_size);
        return;
    }
    extended_header.resize(extended_size);
    if (extended_size != 0 &&
        file->Read(extended_header.data(), extended_size, sizeof(CNMTHeader)) != extended_size) {
        LOG_WARNING(Loader, "Failed to read CNMT extended header.");
        extended_header.clear();
        return;
    }
    if (extended_header.size() >= sizeof(OptionalHeader)) {
        std::memcpy(&opt_header, extended_header.data(), sizeof(OptionalHeader));
    }

    const auto table_start = sizeof(CNMTHeader) + extended_size;
    for (u16 i = 0; i < header.number_content_entries; ++i) {
        auto& next = content_records.emplace_back(ContentRecord{});
        if (file->ReadObject(&next, table_start + i * sizeof(ContentRecord)) !=
            sizeof(ContentRecord)) {
            content_records.erase(content_records.end() - 1);
        }
    }

    const auto meta_start =
        table_start + static_cast<std::size_t>(header.number_content_entries) *
                          sizeof(ContentRecord);
    for (u16 i = 0; i < header.number_meta_entries; ++i) {
        auto& next = meta_records.emplace_back(MetaRecord{});
        if (file->ReadObject(&next, meta_start + i * sizeof(MetaRecord)) != sizeof(MetaRecord)) {
            meta_records.erase(meta_records.end() - 1);
        }
    }
}

CNMT::CNMT(CNMTHeader header_, OptionalHeader opt_header_,
           std::vector<ContentRecord> content_records_, std::vector<MetaRecord> meta_records_)
    : header(std::move(header_)), opt_header(std::move(opt_header_)),
      extended_header(static_cast<std::size_t>(header.table_offset)),
      content_records(std::move(content_records_)), meta_records(std::move(meta_records_)) {
    if (!extended_header.empty()) {
        std::memcpy(extended_header.data(), &opt_header,
                    (std::min)(extended_header.size(), sizeof(OptionalHeader)));
    }
}

CNMT::~CNMT() = default;

const CNMTHeader& CNMT::GetHeader() const {
    return header;
}

const std::vector<u8>& CNMT::GetExtendedHeader() const {
    return extended_header;
}

u64 CNMT::GetTitleID() const {
    return header.title_id;
}

u32 CNMT::GetTitleVersion() const {
    return header.title_version;
}

TitleType CNMT::GetType() const {
    return header.type;
}

const std::vector<ContentRecord>& CNMT::GetContentRecords() const {
    return content_records;
}

const std::vector<MetaRecord>& CNMT::GetMetaRecords() const {
    return meta_records;
}

bool CNMT::UnionRecords(const CNMT& other) {
    bool change = false;
    for (const auto& rec : other.content_records) {
        const auto iter = std::find_if(content_records.begin(), content_records.end(),
                                       [&rec](const ContentRecord& r) {
                                           return r.nca_id == rec.nca_id && r.type == rec.type;
                                       });
        if (iter == content_records.end()) {
            content_records.emplace_back(rec);
            ++header.number_content_entries;
            change = true;
        }
    }
    for (const auto& rec : other.meta_records) {
        const auto iter =
            std::find_if(meta_records.begin(), meta_records.end(), [&rec](const MetaRecord& r) {
                return r.title_id == rec.title_id && r.title_version == rec.title_version &&
                       r.type == rec.type;
            });
        if (iter == meta_records.end()) {
            meta_records.emplace_back(rec);
            ++header.number_meta_entries;
            change = true;
        }
    }
    return change;
}

std::vector<u8> CNMT::Serialize() const {
    const auto extended_size = static_cast<std::size_t>(header.table_offset);
    std::vector<u8> out(sizeof(CNMTHeader) + extended_size +
                        content_records.size() * sizeof(ContentRecord) +
                        meta_records.size() * sizeof(MetaRecord));
    std::memcpy(out.data(), &header, sizeof(CNMTHeader));

    if (extended_size != 0) {
        const auto copied = (std::min)(extended_size, extended_header.size());
        if (copied != 0) {
            std::memcpy(out.data() + sizeof(CNMTHeader), extended_header.data(), copied);
        }
        if (copied < (std::min)(extended_size, sizeof(OptionalHeader))) {
            std::memcpy(out.data() + sizeof(CNMTHeader), &opt_header,
                        (std::min)(extended_size, sizeof(OptionalHeader)));
        }
    }

    std::size_t offset = sizeof(CNMTHeader) + extended_size;
    for (const auto& rec : content_records) {
        std::memcpy(out.data() + offset, &rec, sizeof(ContentRecord));
        offset += sizeof(ContentRecord);
    }
    for (const auto& rec : meta_records) {
        std::memcpy(out.data() + offset, &rec, sizeof(MetaRecord));
        offset += sizeof(MetaRecord);
    }
    return out;
}
} // namespace FileSys
