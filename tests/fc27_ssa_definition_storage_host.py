#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Source/model proof for Lichii #1320 / Eglantine #825 block-local SSA definition storage."""
from pathlib import Path
import hashlib
import random
import shutil
import subprocess

ROOT=Path(__file__).resolve().parents[1]
BASIC=ROOT/"encore-active/eden/src/shader_recompiler/frontend/ir/basic_block.h"
SSA=ROOT/"encore-active/eden/src/shader_recompiler/ir_opt/ssa_rewrite_pass.cpp"
PIN_BASIC=ROOT/"sources/eden/src/shader_recompiler/frontend/ir/basic_block.h"
PIN_SSA=ROOT/"sources/eden/src/shader_recompiler/ir_opt/ssa_rewrite_pass.cpp"

def blob(path: Path) -> str:
    raw=path.read_bytes()
    return hashlib.sha1(b"blob "+str(len(raw)).encode()+b"\0"+raw).hexdigest()

assert blob(PIN_BASIC)=="d3a66caac9b0955dd72526b23cf7caab8b12976f"
assert blob(PIN_SSA)=="b5cd2339924f0cbd725249140ff67cfb8f79a360"
assert blob(BASIC)=="0a23d1ce0f43524e6195bd8219349ef632cfb719"
assert blob(SSA)=="26c42a9900256e5573735394753c1d5264611664"
basic=BASIC.read_text()
ssa=SSA.read_text()
for needle in (
    "SetSsaPredValue", "SsaPredValue", "enum class SsaVariable",
    "SetSsaValue", "SsaValue", "ssa_pred_values", "ssa_values",
):
    assert needle in basic,needle
for needle in (
    "IR::Block::SsaVariable::ZeroFlag",
    "IR::Block::SsaVariable::IndirectBranch",
    "block->SsaPredValue(variable)",
    "block->SetSsaPredValue(variable, value)",
    "goto_vars.find({block, variable.index})",
    "Common::PairHash",
    "block->SsaValue(Tag::slot)",
):
    assert needle in ssa,needle
assert "std::array<ValueMap, IR::NUM_USER_PREDS>" not in ssa
assert "ValueMap zero_flag" not in ssa
assert "ValueMap indirect_branch_var" not in ssa
assert "SsaPredValue" not in PIN_BASIC.read_text()
assert "Common::PairHash> goto_vars" not in PIN_SSA.read_text()

# Semantic model: compare the old per-variable map representation against
# block-local fixed slots for random set/read sequences. Missing reads are the
# default empty IR::Value in both representations.
class Old:
    def __init__(self):
        self.preds=[{} for _ in range(7)]
        self.flags=[{} for _ in range(5)]
        self.goto={}
    def set_pred(self,b,p,v): self.preds[p][b]=v
    def get_pred(self,b,p): return self.preds[p].get(b,0)
    def set_flag(self,b,f,v): self.flags[f][b]=v
    def get_flag(self,b,f): return self.flags[f].get(b,0)
    def set_goto(self,b,g,v): self.goto[(b,g)]=v
    def get_goto(self,b,g): return self.goto.get((b,g),0)

class New:
    def __init__(self):
        self.block={}
        self.goto={}
    def slot(self,b):
        return self.block.setdefault(b,{"pred":[0]*7,"flag":[0]*5})
    def set_pred(self,b,p,v): self.slot(b)["pred"][p]=v
    def get_pred(self,b,p): return self.slot(b)["pred"][p]
    def set_flag(self,b,f,v): self.slot(b)["flag"][f]=v
    def get_flag(self,b,f): return self.slot(b)["flag"][f]
    def set_goto(self,b,g,v): self.goto[(b,g)]=v
    def get_goto(self,b,g): return self.goto.get((b,g),0)

rng=random.Random(1320)
old,new=Old(),New()
for step in range(50000):
    b=rng.randrange(256); kind=rng.randrange(3); idx=rng.randrange(7 if kind==0 else 5 if kind==1 else 32)
    if rng.randrange(3):
        value=1+rng.randrange(1<<30)
        if kind==0: old.set_pred(b,idx,value); new.set_pred(b,idx,value)
        elif kind==1: old.set_flag(b,idx,value); new.set_flag(b,idx,value)
        else: old.set_goto(b,idx,value); new.set_goto(b,idx,value)
    else:
        if kind==0: assert old.get_pred(b,idx)==new.get_pred(b,idx)
        elif kind==1: assert old.get_flag(b,idx)==new.get_flag(b,idx)
        else: assert old.get_goto(b,idx)==new.get_goto(b,idx)

# Real syntax check of the maintained SSA TU using the maintained basic_block
# header before the immutable Eden tree.
compiler=next((shutil.which(x) for x in ("clang++","g++") if shutil.which(x)),None)
assert compiler,"C++ compiler unavailable"
boost=ROOT/"sources/eden-cpm/boost/boost-1.90.0/libs"
boost_includes=sorted(boost.glob("*/include"))
fmt=ROOT/"sources/eden-cpm/fmt/12.1.0/include"
cmd=[compiler,"-std=c++20","-fsyntax-only","-Wall","-Wextra","-Werror",
     f"-I{ROOT/'encore-active/eden/src'}",
     f"-I{ROOT/'sources/eden/src'}",
     f"-I{fmt}",
     *[f"-I{x}" for x in boost_includes],
     str(SSA)]
subprocess.run(cmd,check=True,timeout=120)
print("FC27_SSA_DEFINITION_STORAGE_HOST_PASS model_steps=50000 syntax=1 pinned_immutable=1 gameplay_unverified=1")
