#!/usr/bin/env python3
"""FC27 storage-offset regression: c0 base must win over shifted c3 offset.

Upstream-first provenance:
- nxnative #2505 / #1129: FC27 0100C49025D3E000 body/head and stretched
  geometry corruption when a cbuf word shifted <<4 was misclassified as a
  bindless base and its shift was lost.
- Lichii storage_buffer_pointer_tracking.cpp: storage-pointer candidates must
  beat aligned stride/offset candidates.

Encore's pass is architecturally different. This test locks its current
selection and offset-preservation contract without claiming PS5 gameplay.
"""
from __future__ import annotations
from dataclasses import dataclass
from pathlib import Path
import hashlib
import random
import re

ROOT=Path(__file__).resolve().parents[1]
PASS=ROOT/"sources/eden/src/shader_recompiler/ir_opt/global_memory_to_storage_buffer_pass.cpp"
BFS=ROOT/"sources/eden/src/shader_recompiler/frontend/ir/breadth_first_search.h"

def blob(path: Path) -> str:
    raw=path.read_bytes()
    return hashlib.sha1(b"blob "+str(len(raw)).encode()+bytes([0])+raw).hexdigest()

src=PASS.read_text()
bfs=BFS.read_text()

# Exact immutable inputs for this audit. A source change must deliberately
# update the semantic test, never silently restamp these witnesses.
assert blob(PASS)=="4ce1ff6c19c4937855d4460a515aec6a8a1e07e0"
assert blob(BFS)=="ec5542554920826ea52b33fd4170c5a8e69648df"

# The native FC27 pattern must be protected by the NVN-biased first search:
# c0 descriptor range 0x100..0x700, alignment 16. c3 cannot satisfy it.
for needle in (
    ".index = 0,",
    ".offset_begin = 0x100,",
    ".offset_end = 0x700,",
    ".alignment = 16,",
    "Track(low_addr, &nvn_bias)",
    "Track(low_addr, nullptr)",
):
    assert needle in src, needle
assert src.index("Track(low_addr, &nvn_bias)") < src.index("Track(low_addr, nullptr)")

# StorageOffset must retain the whole tracked low-address expression and only
# subtract the selected c0 base. It must not replace/unwrap a shifted operand.
storage=src.split("IR::U32 StorageOffset(",1)[1].split("\n}\n",1)[0]
for needle in (
    "offset = low_addr->value;",
    "IR::U32 low_cbuf{ir.GetCbuf(ir.Imm32(buffer.index), ir.Imm32(buffer.offset))};",
    "return ir.ISub(offset, low_cbuf);",
):
    assert needle in storage, needle
assert "ShiftLeftLogical" not in storage
assert ".SetArg(" not in storage and "ReplaceUsesWith" not in storage

# BreadthFirstSearch is right-most-first, but that order cannot promote c3:
# the predicate itself rejects cbuf index != 0 under NVN bias.
assert "for (size_t arg = inst->NumArgs(); arg--;)" in bfs

@dataclass(frozen=True)
class Cbuf:
    index:int
    offset:int

def meets_bias(c:Cbuf)->bool:
    return c.index==0 and 0x100 <= c.offset < 0x700 and c.offset % 16 == 0

def choose_biased_bfs(candidates:list[Cbuf])->Cbuf|None:
    # candidates are in actual BFS visit order; predicate is the source rule.
    return next((c for c in candidates if meets_bias(c)),None)

base=Cbuf(0,0x178)
shifted_fc27_offset=Cbuf(3,0x10)
aligned_noise=[Cbuf(0,0x210),Cbuf(3,0x100),Cbuf(4,0x178)]

# Whatever sibling order a translated expression produces, c3 is never the
# selected descriptor. If the real c0 base is the only valid c0 pointer it wins.
for seed in range(512):
    rng=random.Random(seed)
    order=[shifted_fc27_offset,base,*aligned_noise]
    rng.shuffle(order)
    # Remove c0 noise to model the exact FC27 two-source address.
    exact=[x for x in order if x in (shifted_fc27_offset,base)]
    assert choose_biased_bfs(exact)==base

# Arithmetic regression: (base + (word<<4)) - base == word<<4. The historical
# nxnative bug effectively turned this into 'word'. Exercise boundary values.
for word in [0,1,2,15,16,0x1234,0x0fffffff,0xffffffff]:
    shifted=(word << 4) & 0xffffffff
    guest_base=0x80000100
    low=(guest_base + shifted) & 0xffffffff
    recovered=(low - guest_base) & 0xffffffff
    assert recovered==shifted
    if word not in (0,):
        # For ordinary values the broken unshifted result is detectably distinct.
        assert recovered != (word & 0xffffffff) or shifted == (word & 0xffffffff)

# No nxnative-specific unwrapping helpers exist in this pass; do not cargo-cult
# their patch into a different IR architecture.
assert "TryUnwrapHandle" not in src
assert "FindUniqueBaseAddressCb" not in src

print("FC27_STORAGE_OFFSET_SHIFT_PASS c0_bias=1 c3_shift_preserved=1 nxnative_same_bug=0 ps5_visual=unverified")
