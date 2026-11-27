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
SRC = ROOT / "encore-active/eden/src/shader_recompiler/frontend/maxwell/translate/impl/not_implemented.cpp"
text = SRC.read_text()

def body(name: str) -> str:
    start = text.index(f"void TranslatorVisitor::{name}(u64 insn)")
    end = text.index("\nvoid TranslatorVisitor::", start + 1)
    return text[start:end]

rc = body("PRMT_rc")
cr = body("PRMT_cr")
reg = body("PRMT_reg")
imm = body("PRMT_imm")

# The four frontend forms now share the same qualified implementation.
# Pin the complete selected source and verify the operand ordering explicitly:
# rc => selector=reg39, B=cbuf; cr => selector=cbuf, B=reg39.
raw = SRC.read_bytes()
import hashlib
assert hashlib.sha1(b"blob "+str(len(raw)).encode()+bytes([0])+raw).hexdigest() == \
       "a22e8c635831d970fced9a5b2afaf9c6851b7460"
assert "TranslatePrmt(*this, insn, GetReg39(insn), GetCbuf(insn));" in rc
assert "TranslatePrmt(*this, insn, GetCbuf(insn), GetReg39(insn));" in cr
assert "TranslatePrmt(*this, insn, GetReg20(insn), GetReg39(insn));" in reg
assert "TranslatePrmt(*this, insn, GetImm20(insn), GetReg39(insn));" in imm

helper_start = text.index("void TranslatePrmt(TranslatorVisitor& v, u64 insn")
helper_end = text.index("\nenum class ImadSpType", helper_start)
helper = text[helper_start:helper_end]
for needle in (
    "if (raw_mode > static_cast<u32>(PrmtMode::Replicate16))",
    "const IR::U32 src_a{v.X(prmt.src_reg_a)};",
    "if (mode == PrmtMode::Index)",
    "v.ir.BitFieldExtract(selector, v.ir.Imm32(byte * 4), v.ir.Imm32(4), false)",
    "v.ir.BitwiseAnd(nibble, v.ir.Imm32(7))",
    "v.ir.BitwiseAnd(nibble, v.ir.Imm32(8))",
    "PickPrmtByte(v, src_a, src_b, index, replicate_sign)",
    "v.X(prmt.dest_reg, result);",
):
    assert needle in helper, needle

pick_start = text.index("IR::U32 PickPrmtByte(")
pick_end = text.index("\nvoid TranslatePrmt(", pick_start)
pick = text[pick_start:pick_end]
for needle in (
    "v.ir.BitwiseAnd(index, v.ir.Imm32(4))",
    "v.ir.BitwiseAnd(index, v.ir.Imm32(3))",
    "v.ir.BitFieldExtract(source, offset, v.ir.Imm32(8), false)",
    "v.ir.BitwiseAnd(value, v.ir.Imm32(0x80))",
    "v.ir.Select(replicate_sign, sign_byte, value)",
):
    assert needle in pick, needle

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
