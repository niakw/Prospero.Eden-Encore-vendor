#!/usr/bin/env python3
"""Host/source regression for the PS5-selected NS2 AM save UID CMIF layout.

Eglantine-Community/Eglantine PR #80: a one-byte SaveDataType precedes an
8-byte-aligned 16-byte nn::account::Uid in ExtendSaveData/GetSaveDataSize.
Common::UUID is an array of bytes (alignment 1), whereas u128 is an array
of two u64s (alignment 8); the old handler read the UID seven bytes early.
This is a source/host ABI witness, NOT a real PS5 guest boot test.
"""
from pathlib import Path
import ctypes

root = Path(__file__).resolve().parents[1]
rel = "encore-active/ns2-runtime/am/core/hle/service/am/service/"
header = (root / (rel + "application_functions.h")).read_text()
source = (root / (rel + "application_functions.cpp")).read_text()
common = (root / "sources/eden/src/common/common_types.h").read_text()
uuid = (root / "encore-active/eden-core/include/common/uuid.h").read_text()

# Verify the actual type definitions that produce the CMIF alignment rule.
assert "using u128 = std::array<std::uint64_t, 2>;" in common
assert "std::array<u8, 0x10> uuid;" in uuid

class OldArgument(ctypes.Structure):
    _fields_ = [("save_type", ctypes.c_uint8), ("user_id", ctypes.c_uint8 * 16)]

class CorrectArgument(ctypes.Structure):
    _fields_ = [("save_type", ctypes.c_uint8), ("user_id", ctypes.c_uint64 * 2)]

assert OldArgument.user_id.offset == 1
assert CorrectArgument.user_id.offset == 8
assert ctypes.sizeof(CorrectArgument) == 24

# Both handler declarations AND matching definitions must use aligned u128.
assert "Result ExtendSaveData(Out<u64> out_required_size, FileSys::SaveDataType type, u128 user_id," in header
assert "FileSys::SaveDataType type, u128 user_id);" in header
assert "Result IApplicationFunctions::ExtendSaveData(Out<u64>" in source
assert "u128 user_id, u64 normal_size," in source
assert "Result IApplicationFunctions::GetSaveDataSize(Out<u64>" in source
assert "FileSys::SaveDataType type, u128 user_id)" in source
assert "type, m_applet->program_id, user_id, {normal_size, journal_size}" in source
assert "type, m_applet->program_id, user_id);" in source

# EnsureSaveData takes the user_id as the sole CMIF argument. It does NOT
# have a leading enum and must remain unchanged in this narrow fix.
assert "Result EnsureSaveData(Out<u64> out_size, Common::UUID user_id);" in header
assert "Result IApplicationFunctions::EnsureSaveData(Out<u64> out_size, Common::UUID user_id)" in source
assert "attribute.user_id = user_id.AsU128();" in source

assert "Common::UUID user_id, u64 normal_size" not in source
assert "FileSys::SaveDataType type, Common::UUID user_id)" not in source

print("NS2_AM_SAVE_UID_CMIF_PASS old_offset=1 corrected_offset=8 "
      "selected_ns2_owner=1 ensure_savadata_unchanged=1 ps5_unverified=1")
