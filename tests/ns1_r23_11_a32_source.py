#!/usr/bin/env python3
"""R23 #11 A32 SHA1 exact official 4-source port; no native JIT assumption."""
from pathlib import Path
import hashlib
ROOT=Path(__file__).resolve().parents[1]
files={
"frontend/A32/decoder/asimd.inc":("def1a17866fef0e36c6c2fd8cf9adfa3b034b3e7","139859a20ada46d487d37b23d4f7c1784496b0a4"),
"frontend/A32/translate/impl/a32_translate_impl.h":("dd88b79237a7157c4815b50a9759d3f4a7268a2b","e959c8439d5abf7add467beef52eb1f3b7b9b05f"),
"frontend/A32/translate/impl/asimd_three_regs.cpp":("8d5e694bca239a1bec231fbd76de5400971aaa6a","c2340ac13a190beeaada794d3e0e586ccc3cfca1"),
"frontend/A32/translate/impl/asimd_two_regs_misc.cpp":("45455aa4441833f5c9fd6801393ec97d281dd528","e61a3ebd1b12eb1d99852b766c5c26653638a6a8")
}
def blob(file):
 b=file.read_bytes()
 return hashlib.sha1(b"blob "+str(len(b)).encode()+bytes([0])+b).hexdigest()
for rel,(pre,post) in files.items():
 assert blob(ROOT/"sources/eden/src/dynarmic/src/dynarmic"/rel)==pre,rel
 assert blob(ROOT/"encore-active/eden-core/src/dynarmic"/rel)==post,rel
 print("R23_A32_FULL_SOURCE_BLOB_PASS",rel)
inc=(ROOT/"encore-active/eden-core/src/dynarmic/frontend/A32/decoder/asimd.inc").read_text()
header=(ROOT/"encore-active/eden-core/src/dynarmic/frontend/A32/translate/impl/a32_translate_impl.h").read_text()
three=(ROOT/"encore-active/eden-core/src/dynarmic/frontend/A32/translate/impl/asimd_three_regs.cpp").read_text()
two=(ROOT/"encore-active/eden-core/src/dynarmic/frontend/A32/translate/impl/asimd_two_regs_misc.cpp").read_text()
for op in ("SHA1C","SHA1P","SHA1M","SHA1H","SHA1SU0","SHA1SU1"):
 assert inc.count("INST(v8_"+op+",")==1,op
 assert header.count("bool v8_"+op+"(")==1,op
 assert (three+two).count("TranslatorVisitor::v8_"+op+"(")==1,op
print("R23_11_A32_SHA1_SOURCE_LINKAGE_PASS ops=6 count=4 native_E3_not_attested=1")
