#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""R09 candidate's constrained lower-bound proof, source-linked and fail closed.

This proves minimality under the REAL nine-roles-distinct-per-block ABI,
not under a speculative redesign of the learned ViT kernels.
"""
from __future__ import annotations
from pathlib import Path
import re
import sys

ROOT=Path(__file__).resolve().parents[1]
STAGE=ROOT/"encore-active/eden-vulkan"
ROLES=("input","hidden","ffwd","attention_context","output","scratch",
       "query","key","value")
ROLE_PLANES=(1,4,1,1,1,4,4,4,4)
REQUIRED_SLOTS=(1,1,4,1,1,4,4,4,4)
BLOCK_SLOTS=(0,2,3,4,1,5,6,7,8)
REVERSE_SLOTS=(1,2,3,4,0,5,6,7,8)

def squish(s: str) -> str:
    return re.sub(r"\s+","",re.sub(r"/\*.*?\*/|//[^\n]*","",s,flags=re.S))

def check_source(block: str, policy: str, session: str) -> None:
    body=squish(block)
    marker="[[nodiscard]]boolCanOwnVitBuffers("
    assert body.count(marker)==1,"Native nine-buffer guard absent/drifted"
    guard=body.split(marker,1)[1].split("structNrRdna2VitBlock::State",1)[0]
    expected="conststd::array<VkDescriptorBufferInfo,9>all{" + ",".join(
        "buffers."+r for r in ROLES) + "};"
    assert expected in guard, "Native role order/count changed"
    assert "for(std::size_ti=0u;i<all.size();++i)" in guard
    assert "for(std::size_tj=0u;j<i;++j)" in guard
    assert "if(all[j].buffer==b.buffer)returnfalse;" in guard
    assert "if(!b.buffer||b.offset||!b.range||" in guard
    assert "b.range==VK_WHOLE_SIZE||b.range>limit)" in guard
    p=squish(policy)
    assert "kVitMinBlocks=8u" in p and "kVitMinRoles=9u" in p
    assert "kVitMinSlots=9u" in p
    slot_sizes="kVitMinSlotPlanes{" + ",".join(str(x)+"u" for x in REQUIRED_SLOTS) + "}"
    assert slot_sizes in p, "Slot sizes changed"
    assert "if(r[a]==r[b])returnfalse;" in p, "Alias guard removed"
    assert "if(i&&r[0]!=p.block_slots[i-1u][4])returnfalse;" in p
    s=squish(session)
    marker="#ifdefined(EDEN_R09_MIN_VIT_POOL_EXPERIMENTAL)&&EDEN_R09_MIN_VIT_POOL_EXPERIMENTAL"
    assert s.count(marker)==2, "Feature flag or include drifted"
    assert "#else" in s and "#endif" in s
    # The nine roles are passed individually to the real Vulkan producers.
    for index,role in enumerate(ROLES):
        expected="b."+role+"=pool[ids["+str(index)+"u]];"
        assert expected in s,"Native role->slot binding drifted: "+role
    assert "MakeVitMinPoolLayout(vit_route.tokens)" in s
    # Read every original-role allocation scale, not guessed from a budget.
    legacy=s.split("[[nodiscard]]boolMakeVitBuffers(",1)[1].split("#else",1)[1].split("#endif",1)[0]
    scales=("b.input=i?result.back().output:Make(plane);",
            "b.hidden=Make(plane*4u);","b.ffwd=Make(plane);",
            "b.attention_context=Make(plane);","b.output=Make(plane);",
            "b.scratch=Make(plane*4u);","b.query=Make(plane*4u);",
            "b.key=Make(plane*4u);","b.value=Make(plane*4u);")
    for statement in scales:
        assert statement in legacy, "Native legacy allocation ABI drift"

def prove_lower_bound() -> None:
    # Each block contains a complete nine-vertex conflict graph (K9) under
    # the actual CanOwnVitBuffers double loop. Its chromatic lower bound is
    # 9, and sum of live role minimum sizes is exactly 24 planes.
    assert len(ROLES)==9 and len(ROLE_PLANES)==9
    assert sum(ROLE_PLANES)==24
    assert all(x>0 for x in ROLE_PLANES)
    # Each slot has at least the logical role capacity it serves and all
    # nine roles have distinct slots within the SAME block.
    for allocation in (BLOCK_SLOTS,REVERSE_SLOTS):
        assert len(set(allocation))==9
        assert all(REQUIRED_SLOTS[allocation[i]]>=ROLE_PLANES[i]
                   for i in range(9))
    assert BLOCK_SLOTS[4]==REVERSE_SLOTS[0]
    assert REVERSE_SLOTS[4]==BLOCK_SLOTS[0]
    assert sum(REQUIRED_SLOTS)==24
    for tokens in (64,960,2560,4096):
        plane=tokens*1024
        candidate=24*plane
        baseline=185*plane
        assert candidate<baseline and baseline-candidate==161*plane
        assert candidate>=sum(ROLE_PLANES)*plane
    print("R09_MIN_VIT_CONSTRAINED_MINIMUM_9_BUFFERS_24_PLANES_PROVED")
    print("R09_MIN_VIT_NOT_GLOBAL_MODEL_OPTIMALITY_OR_GPU_PROOF")

def negatives(block: str,policy: str,session: str) -> None:
    changes=[
        ("native pairwise alias check",block,
         "if(all[j].buffer==b.buffer)return false;",
         "if(false)return false;","block"),
        ("native number of roles",block,
         "std::array<VkDescriptorBufferInfo,9>",
         "std::array<VkDescriptorBufferInfo,8>","block"),
        ("layout alias check",policy,
         "if(r[a]==r[b])return false;",
         "if(false)return false;","policy"),
        ("layout capacity of hidden",policy,
         "1u,1u,4u,1u,1u,4u,4u,4u,4u",
         "1u,1u,1u,1u,1u,4u,4u,4u,4u","policy"),
        ("native fifth-role output",session,
         "b.output=pool[ids[4u]];",
         "b.output=pool[ids[3u]];","session"),
        ("native legacy full scratch",session,
         "b.scratch=Make(plane*4u);",
         "b.scratch=Make(plane);","session"),
    ]
    for name,orig,old,new,section in changes:
        assert old in orig,"Negative case no longer matches: "+name
        mutated=orig.replace(old,new,1)
        try:
            check_source(mutated if section=="block" else block,
                         mutated if section=="policy" else policy,
                         mutated if section=="session" else session)
        except AssertionError:
            continue
        raise AssertionError("Unsafe code mutation accepted: "+name)
    print(f"R09_MIN_VIT_OPTIMALITY_NEGATIVE_MUTATIONS_PASS count={len(changes)}")

def main() -> int:
    block=(STAGE/"nr_rdna2_vit_block.cpp").read_text()
    policy=(STAGE/"include/rdna2_nr_vit_min_pool_policy.h").read_text()
    session=(STAGE/"nr_rdna2_frame_session.cpp").read_text()
    check_source(block,policy,session)
    prove_lower_bound()
    negatives(block,policy,session)
    return 0

if __name__=="__main__":
    try:
        sys.exit(main())
    except (AssertionError,OSError) as exc:
        print("R09_MIN_VIT_OPTIMALITY_SOURCE_GUARD_FAILED:",exc)
        sys.exit(1)
