// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later
// PFS0 and DLC plaintext-NCA builders derived from Lichii nx2
// src/core/file_sys/decrypted_nca.cpp at b0a54756043b. Other
// decryption tools are deliberately not part of the PS5 runtime.
#include <algorithm>
#include <array>
#include <cstring>
#include <limits>
#include <vector>
#include <openssl/evp.h>
#include "common/alignment.h"
#include "common/logging.h"
#include "core/file_sys/decrypted_nca.h"
#include "core/file_sys/fssystem/fssystem_nca_header.h"
#include "core/file_sys/vfs/vfs_concat.h"
#include "core/file_sys/vfs/vfs_static.h"
#include "core/file_sys/vfs/vfs_vector.h"

namespace FileSys {
namespace {
template <typename T> void Put(std::vector<u8>& out, std::size_t at, T value) {
    std::memcpy(out.data() + at, &value, sizeof(T));
}
constexpr std::size_t HeaderSize =
    NcaHeader::Size + NcaHeader::FsCountMax * NcaFsHeader::Size;
bool Sha256(const void* data, std::size_t size, Hash& out) {
    unsigned int length = 0;
    return EVP_Digest(data, size, out.value.data(), &length, EVP_sha256(), nullptr) == 1 &&
           length == out.value.size();
}
} // namespace

VirtualFile BuildPartitionFile(std::string name, const std::vector<VirtualFile>& files) {
    std::vector<u8> strings;
    for (const auto& file : files) {
        const auto file_name = file->GetName();
        strings.insert(strings.end(), file_name.begin(), file_name.end());
        strings.push_back(0);
    }
    constexpr std::size_t PfsHeaderSize = 0x10;
    constexpr std::size_t EntrySize = 0x18;
    const std::size_t fixed = PfsHeaderSize + EntrySize * files.size();
    strings.resize(Common::AlignUp(fixed + strings.size(), 0x20) - fixed);

    std::vector<u8> header(fixed + strings.size());
    std::memcpy(header.data(), "PFS0", 4);
    Put<u32>(header, 4, static_cast<u32>(files.size()));
    Put<u32>(header, 8, static_cast<u32>(strings.size()));

    u64 offset = 0;
    u32 name_offset = 0;
    for (std::size_t i = 0; i < files.size(); ++i) {
        const auto at = PfsHeaderSize + EntrySize * i;
        const u64 size = files[i]->GetSize();
        Put<u64>(header, at, offset);
        Put<u64>(header, at + 8, size);
        Put<u32>(header, at + 16, name_offset);
        offset += size;
        name_offset += static_cast<u32>(files[i]->GetName().size() + 1);
    }
    std::memcpy(header.data() + fixed, strings.data(), strings.size());

    std::vector<VirtualFile> parts;
    parts.reserve(files.size() + 1);
    parts.push_back(std::make_shared<VectorVfsFile>(std::move(header), "header"));
    for (const auto& file : files) {
        if (file->GetSize() != 0) {
            parts.push_back(file);
        }
    }
    return ConcatenatedVfsFile::MakeConcatenatedFile(std::move(name), std::move(parts));
}

VirtualFile BuildAddOnContentNca(u64 title_id, VirtualFile romfs) {
    // A malformed or unavailable NX add-on must never be dereferenced or
    // registered as a valid content NCA. CreateRomFS() can return null.
    if (!romfs) {
        return nullptr;
    }
    const u64 romfs_size = romfs->GetSize();
    constexpr u64 sector = NcaHeader::SectorSize;
    if (romfs_size > std::numeric_limits<u64>::max() - (sector - 1)) {
        return nullptr;
    }
    const u64 section_size = Common::AlignUp(romfs_size, sector);
    if (section_size > std::numeric_limits<u64>::max() - HeaderSize) {
        return nullptr;
    }

    NcaFsHeader fs{};
    fs.version = 2;
    fs.fs_type = NcaFsHeader::FsType::RomFs;
    fs.hash_type = NcaFsHeader::HashType::None;
    fs.encryption_type = NcaFsHeader::EncryptionType::None;

    NcaHeader nca{};
    nca.magic = Common::MakeMagic('D', 'N', 'C', 'A');
    nca.content_type = NcaHeader::ContentType::PublicData;
    nca.content_size = HeaderSize + section_size;
    nca.program_id = title_id;
    nca.sdk_addon_version = 0x000B0000;
    nca.fs_info[0] = {NcaHeader::ByteToSector(HeaderSize),
                      NcaHeader::ByteToSector(HeaderSize + section_size), 0, 0};
    if (!Sha256(&fs, sizeof(fs), nca.fs_header_hash[0])) {
        return nullptr;
    }

    std::vector<u8> header(HeaderSize);
    std::memcpy(header.data(), &nca, sizeof(nca));
    std::memcpy(header.data() + NcaHeader::Size, &fs, sizeof(fs));
    std::vector<VirtualFile> parts{std::make_shared<VectorVfsFile>(std::move(header), "header"),
                                   std::move(romfs)};
    if (section_size > romfs_size) {
        parts.push_back(std::make_shared<StaticVfsFile>(u8{0}, section_size - romfs_size));
    }
    return ConcatenatedVfsFile::MakeConcatenatedFile(fmt::format("{:016x}.nca", title_id),
                                                     std::move(parts));
}


} // namespace FileSys
