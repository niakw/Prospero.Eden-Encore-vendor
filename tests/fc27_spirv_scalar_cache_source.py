#!/usr/bin/env python3
"""Exact source + bit-pattern model for Lichii #1320 scalar SPIR-V cache."""
from pathlib import Path
import hashlib
import math
import struct

ROOT=Path(__file__).resolve().parents[1]
PINNED=ROOT/"sources/eden/src/shader_recompiler/backend/spirv/spirv_emit_context.h"
ACTIVE=ROOT/"encore-active/eden/src/shader_recompiler/backend/spirv/spirv_emit_context.h"

def blob_bytes(raw: bytes) -> str:
    return hashlib.sha1(b"blob "+str(len(raw)).encode()+b"\0"+raw).hexdigest()

def blob(path: Path) -> str:
    return blob_bytes(path.read_bytes())

assert blob(PINNED)=="612c7caa820f2465a2364f4350a91c0a759fa3bd"
assert blob(ACTIVE)=="68e14b3a0c4ae990933652baa51183a51eee4f81"
src=ACTIVE.read_text()
original=PINNED.read_text()

# Exact reviewed source contract.
for needle in (
    "#include <bit>",
    '#include "common/container/unordered_map.h"',
    "CachedConstant(u32_constants, U32[1], value)",
    "CachedConstant(f32_constants, F32[1], value)",
    "cache.try_emplace(std::bit_cast<u32>(value))",
    "::Common::unordered_map<u32, Id> u32_constants;",
    "::Common::unordered_map<u32, Id> f32_constants;",
):
    assert src.count(needle)==1,needle

# Reconstruct the immutable input by reversing only the reviewed Lichii hunks.
rev=src
rev=rev.replace("#include <array>\n#include <bit>\n","#include <array>\n",1)
rev=rev.replace('#include <sirit/sirit.h>\n#include "common/container/unordered_map.h"\n#include "common/container/unordered_set.h"\n',
                '#include <sirit/sirit.h>\n#include "common/container/unordered_set.h"\n',1)
rev=rev.replace("        return CachedConstant(u32_constants, U32[1], value);",
                "        return Constant(U32[1], value);",1)
rev=rev.replace("        return CachedConstant(f32_constants, F32[1], value);",
                "        return Constant(F32[1], value);",1)
cache_block='''    /// Sirit deduplicates constants, but hashes a freshly allocated copy of
    /// their words on every call. Shader translation emits scalar immediates
    /// repeatedly, so retain the ids by exact bit pattern.
    template <typename T>
    Id CachedConstant(::Common::unordered_map<u32, Id>& cache, Id type, T value) {
        const auto [it, is_new]{cache.try_emplace(std::bit_cast<u32>(value))};
        if (is_new) {
            it->second = Constant(type, value);
        }
        return it->second;
    }

    ::Common::unordered_map<u32, Id> u32_constants;
    ::Common::unordered_map<u32, Id> f32_constants;

'''
assert rev.count(cache_block)==1
rev=rev.replace(cache_block,"",1)
assert rev==original
assert blob_bytes(rev.encode())=="612c7caa820f2465a2364f4350a91c0a759fa3bd"

# Model the exact key semantics: each type owns its own cache; float keys are
# IEEE-754 bit patterns, not numeric equality. +0/-0 and NaN payloads differ.
def f32(bits: int) -> float:
    return struct.unpack("<f",struct.pack("<I",bits))[0]
def bits32(value: float) -> int:
    return struct.unpack("<I",struct.pack("<f",value))[0]

float_patterns=[0x00000000,0x80000000,0x7fc00000,0x7fc00001,0x3f800000,
                0x80000000,0x7fc00001,0x00000000]
uint_patterns=[5,0x80000000,5]
fcache={}
ucache={}
next_id=1
for pattern in float_patterns:
    key=bits32(f32(pattern))
    if key not in fcache:
        fcache[key]=next_id; next_id+=1
for value in uint_patterns:
    if value not in ucache:
        ucache[value]=next_id; next_id+=1
assert len(fcache)==5
assert len(ucache)==2
assert fcache[0x00000000] != fcache[0x80000000]
assert fcache[0x7fc00000] != fcache[0x7fc00001]
assert fcache[0x80000000] != ucache[0x80000000]
assert math.isnan(f32(0x7fc00000)) and math.isnan(f32(0x7fc00001))

print("FC27_SPIRV_SCALAR_CACHE_SOURCE_PASS reversible=1 float_keys=5 uint_keys=2 bit_exact=1 ps5_performance=unverified")
