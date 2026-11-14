// SPDX-FileCopyrightText: Copyright 2025 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

// SPDX-FileCopyrightText: Copyright 2025 Eden Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

// SPDX-FileCopyrightText: Copyright 2023 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <limits>
#include "common/alignment.h"
#include "core/file_sys/fssystem/fssystem_integrity_verification_storage.h"

namespace FileSys {

constexpr inline u32 ILog2(u32 val) {
    ASSERT(val > 0);
    return static_cast<u32>((sizeof(u32) * 8) - 1 - std::countl_zero<u32>(val));
}

void IntegrityVerificationStorage::Initialize(VirtualFile hs, VirtualFile ds, s64 verif_block_size,
                                              s64 upper_layer_verif_block_size, bool is_real_data) {
    // Validate preconditions.
    ASSERT(verif_block_size >= HashSize);

    // Set storages.
    m_hash_storage = hs;
    m_data_storage = ds;

    // Set verification block sizes.
    m_verification_block_size = verif_block_size;
    m_verification_block_order = ILog2(static_cast<u32>(verif_block_size));
    ASSERT(m_verification_block_size == 1ll << m_verification_block_order);

    // Set upper layer block sizes.
    upper_layer_verif_block_size = (std::max)(upper_layer_verif_block_size, HashSize);
    m_upper_layer_verification_block_size = upper_layer_verif_block_size;
    m_upper_layer_verification_block_order = ILog2(static_cast<u32>(upper_layer_verif_block_size));
    ASSERT(m_upper_layer_verification_block_size == 1ll << m_upper_layer_verification_block_order);

    // Validate sizes.
    {
        s64 hash_size = m_hash_storage->GetSize();
        s64 data_size = m_data_storage->GetSize();
        ASSERT(((hash_size / HashSize) * m_verification_block_size) >= data_size);
    }

    // Set data.
    m_is_real_data = is_real_data;
}

void IntegrityVerificationStorage::Finalize() {
    m_hash_storage = VirtualFile();
    m_data_storage = VirtualFile();
}

size_t IntegrityVerificationStorage::Read(u8* buffer, size_t size, size_t offset) const {
    // Succeed if zero size.
    if (size == 0) {
        return size;
    }

    // Validate arguments.
    ASSERT(buffer != nullptr);

    // Lichii #947: limit padding to the caller's requested extent.
    const size_t data_size = m_data_storage->GetSize();
    if (offset > data_size || m_verification_block_size <= 0) {
        return 0;
    }
    const size_t block = static_cast<size_t>(m_verification_block_size);
    if ((block & (block - 1)) != 0 ||
        data_size > (std::numeric_limits<size_t>::max)() - (block - 1)) {
        return 0;
    }
    const size_t padded_size = Common::AlignUp(data_size, block);
    if (offset > padded_size || size > padded_size - offset) {
        return 0;
    }
    const size_t read_size = (std::min)(size, data_size - offset);
    if (read_size < size) {
        std::memset(buffer + read_size, 0, size - read_size);
    }

    // Perform the read.
    return m_data_storage->Read(buffer, read_size, offset);
}

size_t IntegrityVerificationStorage::GetSize() const {
    return m_data_storage->GetSize();
}

} // namespace FileSys
