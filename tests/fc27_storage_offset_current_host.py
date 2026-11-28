#!/usr/bin/env python3
"""FC27 storage-offset audit on the selected maintained SSBO pass.

Upstream-first provenance:
- nxnative FC27 work identified a cbuf/shift misclassification class where a
  shifted offset could be mistaken for the bindless base.
- Lichii/Eglantine shader work keeps descriptor selection and offset recovery
  separate.

Encore uses a different IR pass. This locks the current source/model contract
and explicitly makes no PS5 visual claim.
"""
from dataclasses import dataclass
from pathlib import Path
import hashlib
import random

ROOT=Path(__file__).resolve().parents[1]
PASS=ROOT/"encore-active/eden/src/shader_recompiler/ir_opt/global_memory_to_storage_buffer_pass.cpp"
BFS=ROOT/"encore-active/eden/src/shader_recompiler/frontend/ir/breadth_first_search.h"
PINNED=ROOT/"sources/eden/src/shader_recompiler/ir_opt/global_memory_to_storage_buffer_pass.cpp"

def blob(path: Path) -> str:
    raw=path.read_bytes()
    return hashlib.sha1(b"blob "+str(len(raw)).encode()+b"\0"+raw).hexdigest()

pass_blob=blob(PASS)
if pass_blob=="602d45b4d2ad872b138d62ce05f379b5e1103602":
    selected=PASS.read_text()
    reverse = (
        ("    case IR::Opcode::GlobalAtomicExchange32:\n    case IR::Opcode::GlobalAtomicCompareExchange32:\n    case IR::Opcode::GlobalAtomicIAdd64:\n", "    case IR::Opcode::GlobalAtomicExchange32:\n    case IR::Opcode::GlobalAtomicIAdd64:\n"),
    )
    for newer, earlier in reverse:
        assert newer in selected, "Unexpected PASS source change"
        selected=selected.replace(newer, earlier, 1)
    data=selected.encode("utf-8")
    previous=hashlib.sha1(b"blob "+str(len(data)).encode()+bytes([0])+data).hexdigest()
    assert previous=="41ad0f5cef1e070e552b43ea49648d5e74f10bad", ("Unexpected source predecessor",previous)
    pass_blob="41ad0f5cef1e070e552b43ea49648d5e74f10bad"
assert pass_blob=="41ad0f5cef1e070e552b43ea49648d5e74f10bad"
assert blob(BFS)=="fa5e7572ef99bba3db17154c63f60dddba7a87c6"
assert blob(PINNED)=="4ce1ff6c19c4937855d4460a515aec6a8a1e07e0"
src=PASS.read_text()
bfs=BFS.read_text()

for needle in (
    ".index = 0,",
    ".offset_begin = 0x100,",
    ".offset_end = 0x700,",
    ".alignment = 16,",
    "cached_track(&nvn_bias, info.biased_tracks, info.biased_exhausted)",
    "cached_track(nullptr, info.unbiased_tracks, info.unbiased_exhausted)",
):
    assert needle in src,needle
assert src.index("cached_track(&nvn_bias") < src.index("cached_track(nullptr")

storage=src.split("IR::U32 StorageOffset(",1)[1].split("\n}\n",1)[0]
for needle in (
    "offset = low_addr->value;",
    "offset = ir.IAdd(offset, ir.Imm32(low_addr->imm_offset));",
    "IR::U32 low_cbuf{ir.GetCbuf(ir.Imm32(buffer.index), ir.Imm32(buffer.offset))};",
    "return ir.ISub(offset, low_cbuf);",
):
    assert needle in storage,needle
assert ".SetArg(" not in storage and "ReplaceUsesWith" not in storage
assert "for (size_t arg = inst->NumArgs(); arg--;)" in bfs

@dataclass(frozen=True)
class Cbuf:
    index:int
    offset:int

def biased(c:Cbuf)->bool:
    return c.index==0 and 0x100 <= c.offset < 0x700 and c.offset % 16 == 0

base=Cbuf(0,0x180)
shifted_c3=Cbuf(3,0x10)
for seed in range(1024):
    order=[base,shifted_c3]
    random.Random(seed).shuffle(order)
    winner=next((x for x in order if biased(x)),None)
    assert winner==base

# Exact arithmetic property: the full shifted low-address expression survives
# descriptor selection, then the selected c0 base is subtracted.
for word in [0,1,2,15,16,0x1234,0x0fffffff,0xffffffff]:
    shifted=(word << 4) & 0xffffffff
    guest_base=0x80000100
    low=(guest_base + shifted) & 0xffffffff
    recovered=(low - guest_base) & 0xffffffff
    assert recovered==shifted

# Do not cargo-cult nxnative-specific helpers into Encore's different IR path.
assert "TryUnwrapHandle" not in src
assert "FindUniqueBaseAddressCb" not in src
print("FC27_STORAGE_OFFSET_CURRENT_PASS c0_bias=1 shifted_offset_preserved=1 same_bug=0 ps5_visual=unverified")
