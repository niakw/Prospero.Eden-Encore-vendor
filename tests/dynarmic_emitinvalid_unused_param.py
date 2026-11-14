#!/usr/bin/env python3
"""Exact source proof for the cosmetic Dynarmic EmitInvalid parameter warning."""
from pathlib import Path
import hashlib

ROOT=Path(__file__).resolve().parents[1]
OLD="void EmitX64::EmitInvalid(EmitContext&, IR::Inst* inst) {\n    UNREACHABLE();\n}"
NEW="void EmitX64::EmitInvalid(EmitContext&, IR::Inst*) {\n    UNREACHABLE();\n}"
EXPECTED={
 "encore-active/eden-core/src/dynarmic/backend/x64/emit_x64_native.cpp":
   "205aa034f0ec2330aec5a81a628ce3ae326856ab",
 "encore-active/eden-core/src/dynarmic/backend/x64/emit_x64_host.cpp":
   "b2ec3288f0b4d939422ff70d5a3716d722e6b842",
}
def blob(text:str)->str:
 raw=text.encode()
 return hashlib.sha1(b"blob "+str(len(raw)).encode()+bytes([0])+raw).hexdigest()
for rel,old_blob in EXPECTED.items():
 text=(ROOT/rel).read_text()
 assert text.count(NEW)==1 and OLD not in text, rel
 reconstructed=text.replace(NEW,OLD,1)
 assert blob(reconstructed)==old_blob,(rel,blob(reconstructed))
# Upstream/pinned source still names the unused parameter: this branch is a
# local warning cleanup, not a claimed upstream semantic fix.
pinned=(ROOT/"sources/eden/src/dynarmic/src/dynarmic/backend/x64/emit_x64.cpp").read_text()
assert pinned.count(OLD)==1
print("DYNARMIC_EMITINVALID_UNUSED_PARAM_SOURCE_PASS abi=unchanged body=unchanged suppression=0")
