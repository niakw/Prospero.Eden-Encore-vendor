#!/usr/bin/env python3
"""Source/semantic contract for the Maxwell PRMT operand-form backport.

Upstream reference:
  zortos293/Lichii f6a8f513249754e2dd69a7f04efe5f8bb34ab756
  src/tests/video_core/maxwell_permute_lod.cpp

This host test does not claim FC27 uses rc/cr on PS5. It proves that Encore's
two newly admitted forms map operands exactly like the upstream translator and
that the default-mode byte/sign-replication semantics are exhaustive.
"""
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
SRC = ROOT / "encore-active/eden-core/src/shader_recompiler/frontend/maxwell/translate/impl/not_implemented.cpp"
text = SRC.read_text()

def body(name: str) -> str:
    start = text.index(f"void TranslatorVisitor::{name}(u64 insn)")
    end = text.index("\nvoid TranslatorVisitor::", start + 1)
    return text[start:end]

rc = body("PRMT_rc")
cr = body("PRMT_cr")
reg = body("PRMT_reg")
imm = body("PRMT_imm")

assert "const IR::U32 b{GetCbuf(insn)};" in rc
assert "const IR::U32 selector{GetReg39(insn)};" in rc
assert "const IR::U32 b{GetReg39(insn)};" in cr
assert "const IR::U32 selector{GetCbuf(insn)};" in cr
for section, tag in ((rc, "RC"), (cr, "CR")):
    assert "if (mode != 0)" in section
    assert f"EDEN_GPU_PRMT_{tag}_UNSUPPORTED" in section
    assert f"EDEN_GPU_PRMT_{tag}_INDEX" in section
    assert "ir.BitwiseAnd(nibble, ir.Imm32(4))" in section
    assert "ir.BitwiseAnd(nibble, ir.Imm32(8))" in section
    assert "ir.BitwiseAnd(selected_byte, ir.Imm32(128))" in section
    assert "X(static_cast<IR::Reg>(insn & 255ULL), result);" in section

# Existing reg/imm forms remain implemented rather than being replaced by
# a broader unqualified refactor.
assert "EDEN_GPU_PRMT_REG_INDEX" in reg
assert "EDEN_GPU_PRMT_IMM_INDEX" in imm

def translated(a: int, selector: int, c: int) -> int:
    result = 0
    for output in range(4):
        nibble = (selector >> (output * 4)) & 0xF
        source = c if (nibble & 4) else a
        selected = (source >> ((nibble & 3) * 8)) & 0xFF
        if nibble & 8:
            selected = 0xFF if selected & 0x80 else 0
        result |= selected << (output * 8)
    return result & 0xFFFFFFFF

def upstream_reference(a: int, selector: int, c: int) -> int:
    values = [
        a & 0xFF, (a >> 8) & 0xFF, (a >> 16) & 0xFF, (a >> 24) & 0xFF,
        c & 0xFF, (c >> 8) & 0xFF, (c >> 16) & 0xFF, (c >> 24) & 0xFF,
    ]
    result = 0
    for output in range(4):
        nibble = (selector >> (output * 4)) & 0xF
        value = values[nibble % 8]
        selected = value if nibble < 8 else (0xFF if value >= 0x80 else 0)
        result |= selected << (output * 8)
    return result & 0xFFFFFFFF

a, c = 0x817F00FF, 0x01FE80AA
for selector in range(0x10000):
    assert translated(a, selector, c) == upstream_reference(a, selector, c), hex(selector)

assert translated(0x11223344, 0x3210, 0x55667788) == 0x11223344
assert translated(0x11223344, 0x7654, 0x55667788) == 0x55667788
assert translated(0x80ABCDEF, 0xBBBB, 0) == 0xFFFFFFFF
assert translated(0x11223344, 0xFFFF, 0x55667788) == 0

print("FC27_MAXWELL_PRMT_OPERAND_FORMS_PASS selectors=65536 forms=rc,cr mode0=1 ps5_fc27=unverified")
