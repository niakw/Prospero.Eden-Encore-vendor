#!/usr/bin/env python3
"""C04 maintained NS1 CPU/JIT source-difference attestation only.

Exact old Git objects, three approved small changes, and no unreviewed A32
memory sign-extension rewrite. Full Dynarmic/native PS5 tests remain mandatory.
"""
from pathlib import Path
import hashlib

ROOT = Path(__file__).resolve().parents[1]
ACTIVE = ROOT / "encore-active"
ORIGINAL_BLOBS = {
    "eden-header-hygiene/include/common/page_table.h": "0c12891565d487578c1720c74f6de994b55e82eb",
    "eden-core/src/dynarmic/backend/x64/emit_x64_data_processing.cpp": "3dd13ebc5c519de63672f0699a796009024e4a13",
    "eden-core/src/dynarmic/backend/x64/emit_x64_vector.cpp": "4afc50c77b167d6e36d180e04c5cf58259db62b7",
}
MEMORY_PATH = ACTIVE / "eden-core/src/dynarmic/backend/x64/emit_x64_memory.h"


def blob(data: str) -> str:
    raw = data.encode()
    return hashlib.sha1(b"blob " + str(len(raw)).encode() + bytes([0]) + raw).hexdigest()


def recover_one(rel: str, old: str, new: str) -> None:
    current = (ACTIVE / rel).read_text()
    assert current.count(new) == 1, (rel, "candidate site is not unique")
    assert old not in current or old in new, (rel, "old source still present")
    reconstructed = current.replace(new, old, 1)
    assert blob(reconstructed) == ORIGINAL_BLOBS[rel], (rel, "unapproved drift")
    print("C04_EXACT_SOURCE_DELTA_PASS", rel)


header = "eden-header-hygiene/include/common/page_table.h"
oldbit = "static constexpr u64 SIGN_BIT = 45 + 12; // 44 bits of data + page offset"
newbit = "static constexpr u64 SIGN_BIT = 44 + 12 - 1; // top bit of 44-bit ATTRIBUTE_MASK"
recover_one(header, oldbit, newbit)
assert 44 + 12 - 1 == 55
assert (((1 << 44) - 1) << 12).bit_length() - 1 == 55

division = "eden-core/src/dynarmic/backend/x64/emit_x64_data_processing.cpp"
s = (ACTIVE / division).read_text()
old = "    code.test(divisor, divisor);\n    code.jz(end, code.T_NEAR);"
new = "    code.test(divisor, divisor);\n    code.cmovz(eax, divisor); // SDIV32 by zero returns zero\n    code.jz(end, code.T_NEAR);"
start = s.index("void EmitX64::EmitSignedDiv32(")
end = s.index("void EmitX64::EmitSignedDiv64(", start)
assert s[start:end].count(new) == 1
assert blob(s.replace(new, old, 1)) == ORIGINAL_BLOBS[division]
print("C04_SIGNED_DIV32_ZERO_GUARD_SOURCE_PASS")

vector = "eden-core/src/dynarmic/backend/x64/emit_x64_vector.cpp"
s = (ACTIVE / vector).read_text()
shift_old = "auto const shifted = U(element) << U(T(shift_amount));"
shift_new = "U const shifted = U(U(element) << U(T(shift_amount)));"
result_old = "auto result = 0;"
result_new = "T result = 0;"
assert s.count(shift_new) == s.count(result_new) == 1
reconstructed = s.replace(shift_new, shift_old, 1).replace(result_new, result_old, 1)
assert blob(reconstructed) == ORIGINAL_BLOBS[vector]
print("C04_VECTOR_LANE_TYPE_SOURCE_PASS")

# Upstream #136 uses a sign-*bit-index* parameter, but the selected native
# G11 config already supplies countl_zero(pointer_mask), i.e. a shift count.
# Shifting by (63 - 8) instead of 8 could corrupt an A32 guest address.
# Preserve the maintained code and independently verify the unchanged blob.
raw = MEMORY_PATH.read_text()
assert blob(raw) == "c34906cc34a7b9576025520f1eb7f6416543ef33"
assert "code.shl(page, *ctx.conf.page_table_sign_extension);" in raw
assert "63 - int(*ctx.conf.page_table_sign_extension)" not in raw
print("C04_A32_BIT_INDEX_PORT_EXCLUDED shift_semantics_unproven=1")
print("C04_SOURCE_GATES_PASS corrections=3 no_unqualified_jit_port=1")
