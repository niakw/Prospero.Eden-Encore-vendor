#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Fail-closed footprint review of the TWO ACTUAL ViT QKV GPU shaders.

Only validates the native fixed E4M3FN packed [token,head=32,dim=32]
address expressions. It does not prove descriptor lifetimes, correct
inference, no hidden dynamic out-of-bounds, or PS5 Vulkan safety.
"""
from __future__ import annotations

from hashlib import sha1
from pathlib import Path
import re

HERE=Path(__file__).resolve().parents[1]/"encore-active/eden-vulkan/r09_rdna2_nr"
QKV="vit_qkv_norm_rdna2.comp"
ATT="vit_attention_rdna2.comp"
PIN={
    QKV:"5c2e575597ca566511cf3030024178c5020249d1",
    ATT:"dad1dceb8cfa241ec363dff9671d18dab73e9a01",
}

def canonical(src: str) -> str:
    src=re.sub(r"/\*.*?\*/|//[^\n]*","",src,flags=re.S)
    return re.sub(r"\s+","",src)

def require(code: str,pattern: str) -> None:
    if pattern not in code:
        raise ValueError("ViT shader address contract drift: "+pattern)

def check(qkv: str,att: str) -> None:
    q=canonical(qkv)
    a=canonical(att)
    require(q,"layout(local_size_x=8,local_size_y=1,local_size_z=1)in;")
    require(q,"layout(set=0,binding=2,std430)writeonlybufferQuery{uintwords[];}query_out;")
    require(q,"layout(set=0,binding=3,std430)writeonlybufferKey{uintwords[];}key_out;")
    require(q,"layout(set=0,binding=4,std430)writeonlybufferValue{uintwords[];}value_out;")
    require(q,"uintlane=gl_LocalInvocationID.x;")
    require(q,"uinttoken=gl_WorkGroupID.x;")
    require(q,"uinthead=gl_WorkGroupID.y;")
    require(q,"if(token>=pc.tokens||head>=32u)return;")
    require(q,"uintword=(token*32u+head)*8u+lane;")
    for name,base,value in (
        ("query_out","q_base_bytes","qw"),
        ("key_out","k_base_bytes","kw"),
        ("value_out","v_base_bytes","vw")):
        pattern=f"{name}.words[(pc.{base}>>2u)+word]={value};"
        require(q,pattern)
        if q.count(name+".words[")!=1:
            raise ValueError("Unexpected extra QKV output write: "+name)
    require(a,"layout(local_size_x=32,local_size_y=1,local_size_z=1)in;")
    require(a,"constuintquery=gl_WorkGroupID.x*32u+gl_LocalInvocationID.x;")
    require(a,"constuinthead=gl_WorkGroupID.y;")
    require(a,"if(query>=pc.tokens||head>=32u)return;")
    require(a,"constuintrow=(query*32u+head)*8u;")
    require(a,"for(uintkey=0u;key<pc.tokens;++key)")
    require(a,"uintkeyrow=(key*32u+head)*8u;")
    require(a,"for(uinti=0u;i<8u;++i)")
    for buffer,offset,base in (
        ("q_values","q_base_bytes","row"),
        ("k_values","k_base_bytes","keyrow"),
        ("v_values","v_base_bytes","keyrow")):
        pattern=f"{buffer}.words[(pc.{offset}>>2u)+{base}+i]"
        require(a,pattern)
        if a.count(buffer+".words[")!=1:
            raise ValueError("Unexpected extra attention tensor read: "+buffer)
    # One token has 32 heads * 8 uint32 words = 256 words = 1024B.
    for tokens in (1,64,960,2560,4096):
        last_word=((tokens-1)*32+31)*8+7
        if (last_word+1)*4!=tokens*1024:
            raise ValueError("One-plane packed extent mismatch")

def blob_sha(contents: bytes) -> str:
    return sha1(b"blob "+str(len(contents)).encode()+b"\0"+contents).hexdigest()

def negatives(q: str,a: str) -> int:
    # Must specifically reject unsafe source *address/guard* changes,
    # not merely fail the Git SHA pin test.
    mutations=(
      ("producer word stride", "q",
       "uint word=(token*32u+head)*8u+lane;",
       "uint word=(token*32u+head)*9u+lane;"),
      ("producer head guard", "q",
       "if(token>=pc.tokens||head>=32u)return;",
       "if(token>=pc.tokens)return;"),
      ("producer local workgroup", "q",
       "layout(local_size_x=8,local_size_y=1,local_size_z=1) in;",
       "layout(local_size_x=16,local_size_y=1,local_size_z=1) in;"),
      ("consumer query stride", "a",
       "const uint row=(query*32u+head)*8u;",
       "const uint row=(query*32u+head)*9u;"),
      ("consumer key stride", "a",
       "uint keyrow=(key*32u+head)*8u;",
       "uint keyrow=(key*32u+head)*9u;"),
      ("consumer key loop bound", "a",
       "key<pc.tokens;++key", "key<=pc.tokens;++key"),
      ("consumer query guard", "a",
       "if(query>=pc.tokens||head>=32u)return;",
       "if(query>=pc.tokens)return;"),
      ("consumer read overrun", "a",
       "q_values.words[(pc.q_base_bytes>>2u)+row+i]",
       "q_values.words[(pc.q_base_bytes>>2u)+row+i+1u]"),
    )
    for label,which,old,new in mutations:
        original=q if which=="q" else a
        if old not in original:
            raise ValueError("Mutation fixture drift: "+label)
        changed=original.replace(old,new,1)
        try:
            check(changed if which=="q" else q,
                  changed if which=="a" else a)
        except ValueError:
            continue
        raise ValueError("Unsafe mutation accepted: "+label)
    return len(mutations)

def main() -> None:
    data={name:(HERE/name).read_bytes() for name in PIN}
    raw={name:data[name].decode("utf-8") for name in PIN}
    check(raw[QKV],raw[ATT])
    n=negatives(raw[QKV],raw[ATT])
    for name,expected in PIN.items():
        observed=blob_sha(data[name])
        if observed!=expected:
            raise ValueError(f"Unreviewed shader drift: {name}: {observed}")
        print(f"R09_MIN_VIT_GPU_SHADER_PIN_PASS {name} {observed}")
    print(f"R09_MIN_VIT_QKV_ONE_PLANE_ADDRESS_GUARD_PASS negatives={n}")
    print("R09_MIN_VIT_SHADER_SOURCE_GUARD_NOT_REAL_GPU_PROOF")

if __name__=="__main__":
    try:
        main()
    except (ValueError,OSError,UnicodeError) as error:
        print("R09_MIN_VIT_GPU_SHADER_FOOTPRINT_FAIL",error)
        raise SystemExit(1)
