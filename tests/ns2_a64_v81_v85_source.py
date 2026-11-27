#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Source/provenance contract for the reviewed Switch 2 A64 ISA promotion.

This deliberately proves source ownership only. Engine CI separately compiles
and executes the real Dynarmic frontend before any native PS5 package.
"""
from __future__ import annotations
from pathlib import Path
import hashlib

ROOT = Path(__file__).resolve().parents[1]
BASE = ROOT / "sources/eden/src/dynarmic/src/dynarmic/frontend/A64"
ACTIVE = ROOT / "encore-active/eden/src/dynarmic/src/dynarmic/frontend/A64"
TRANSLATOR = ROOT / "encore-active/eden/src/dynarmic/frontend/A64/translate/a64_translate.cpp"

def git_blob(path: Path) -> str:
    data = path.read_bytes()
    return hashlib.sha1(b"blob " + str(len(data)).encode() + b"\0" + data).hexdigest()

# Lichii 11aac324 parent a05231f... is byte-identical to Encore's pinned
# Dynarmic baseline for the three files the upstream commit modifies in place.
assert git_blob(BASE / "decoder/a64.inc") == "8a3fab7ac1d85ad4f81b8d38c87935d034c4d8ad"
assert git_blob(BASE / "translate/impl/impl.h") == "c8afef32837f76e71b5287b7422c032eae011884"
base_translate = ROOT / "sources/eden/src/dynarmic/src/dynarmic/frontend/A64/translate/a64_translate.cpp"
assert git_blob(base_translate) == "7840c63d794d81d9a63100023cdcb3dd9eafdd36"

# Exact reviewed Lichii 11aac324 output blobs, not the older Encore research
# overlay. The exclusive/branch owners revert to the immutable common base;
# their old LDAPR/RETA local implementations are superseded by the full units.
expected = {
    "decoder/a64.inc": "ee9f0faa18a8a7cca1913e16213b816fcf323d68",
    "translate/impl/impl.h": "d8e6f2b7394d6588863047ea01fe13b886d7b7df",
    "translate/impl/load_store_atomic.cpp": "eafc993988eab53ec79f4716b70981725caac0f9",
    "translate/impl/pointer_authentication.cpp": "e36942c942138242254498acc37d6177b2ea292e",
    "translate/impl/memory_tagging.cpp": "9c6bcb78728b56bb8113e31697b449d04183f1f8",
    "translate/impl/load_store_exclusive.cpp": "0e67e11b4b3e36f7ca6542a38129f84aae239f55",
    "translate/impl/a64_branch.cpp": "36b91f13e9b169e035f16ae01aa995715f06b0ac",
}
for relative, want in expected.items():
    path = ACTIVE / relative
    assert path.is_file(), relative
    assert git_blob(path) == want, (relative, git_blob(path), want)

decoder = (ACTIVE / "decoder/a64.inc").read_text()
for marker in (
    'INST(LDAPR_sz, "LDAPRB, LDAPRH, LDAPR"',
    'INST(AtomicMemOp, "LDADD/LDCLR/LDEOR/LDSET/LDSMAX/LDSMIN/LDUMAX/LDUMIN/SWP"',
    'INST(CompareAndSwap, "CASB, CASH, CAS"',
    'INST(CompareAndSwapPair, "CASP"',
    'INST(PACAUT_reg, "PACIA/PACIB/PACDA/PACDB/AUTIA/AUTIB/AUTDA/AUTDB"',
    'INST(XPAC_1, "XPACI, XPACD"',
    'INST(PACGA, "PACGA"',
    'INST(RETA, "RETAA, RETAB"',
    'INST(BRA_BLRA, "BRAA/BRAB/BLRAA/BLRAB (+Z)"',
    'INST(LDRA, "LDRAA, LDRAB"',
    'INST(STG_family, "STG/STZG/ST2G/STZ2G/LDG"',
    'INST(STGP, "STGP"',
    'INST(IRG, "IRG"',
    'INST(GMI, "GMI"',
    'INST(ADDG, "ADDG"',
    'INST(SUBG, "SUBG"',
):
    assert decoder.count(marker) == 1, marker
assert 'INST(LDAPR, "LDAPR"' not in decoder
assert 'INST(RETA, "RETA"' not in decoder

impl = (ACTIVE / "translate/impl/impl.h").read_text()
for decl in (
    "bool AtomicMemOp(", "bool LDAPR_sz(", "bool CompareAndSwap(",
    "bool CompareAndSwapPair(", "bool PACAUT_reg(", "bool BRA_BLRA(",
    "bool STG_family(", "bool STGP(",
):
    assert impl.count(decl) == 1, decl

atomic = (ACTIVE / "translate/impl/load_store_atomic.cpp").read_text()
assert "IR::Term::CheckBit" in atomic
assert "ExclusiveMem(address, 16" in atomic
assert "AtomicFinish(*this, status)" in atomic
pointer = (ACTIVE / "translate/impl/pointer_authentication.cpp").read_text()
assert "TranslatorVisitor::RETA" in pointer
assert "TranslatorVisitor::BRA_BLRA" in pointer
assert "TranslatorVisitor::PACGA" in pointer
mte = (ACTIVE / "translate/impl/memory_tagging.cpp").read_text()
assert "TranslatorVisitor::STG_family" in mte
assert "TranslatorVisitor::STGP" in mte
assert "TranslatorVisitor::ADDG" in mte and "TranslatorVisitor::SUBG" in mte

translator = TRANSLATOR.read_text()
guard = '#if defined(EDEN_NS2_A64_V81_V85)'
assert translator.count(guard) == 1
guarded = translator.split(guard, 1)[1].split("#endif", 1)[0]
for name in ("load_store_atomic.cpp", "memory_tagging.cpp", "pointer_authentication.cpp"):
    assert guarded.count(name) == 1
assert "EDEN_NS2_A64_V81_V85" not in translator.split(guard, 1)[0]

print("NS2_A64_V81_V85_SOURCE_PASS upstream=11aac324 base=a05231f exact_blobs=7")
print("Runtime/native PS5 compatibility remains unverified by this source test")
