#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Extract exactly the native MakeVitBuffers C++ method for isolated host compile.

The generated header supplies small Vulkan-descriptor/allocator stubs, then
uses the *unmodified, preprocessor-controlled production method body*. This
tests the real source syntax and allocator mapping without any PS5 SDK or
real Vulkan buffers. It is NOT a whole-engine translation-unit build.
"""
from __future__ import annotations
import hashlib
import sys
from pathlib import Path

def main() -> None:
    if len(sys.argv)!=3:
        raise SystemExit("usage: extract_r09_min_vit_allocator.py <source> <output>")
    source=Path(sys.argv[1]).read_text()
    needle="    [[nodiscard]] bool MakeVitBuffers("
    if source.count(needle)!=1:
        raise SystemExit("R09_MIN_EXTRACT_FAIL: method count drift")
    start=source.index(needle)
    brace=source.find("{",start)
    if brace<0:
        raise SystemExit("R09_MIN_EXTRACT_FAIL: missing opening brace")
    depth=0
    end=None
    for i in range(brace,len(source)):
        if source[i]=="{":
            depth+=1
        elif source[i]=="}":
            depth-=1
            if depth==0:
                end=i+1
                break
    if end is None:
        raise SystemExit("R09_MIN_EXTRACT_FAIL: unbalanced method")
    native=source[start:end]
    markers=(
        "#if defined(EDEN_R09_MIN_VIT_POOL_EXPERIMENTAL) && EDEN_R09_MIN_VIT_POOL_EXPERIMENTAL",
        "MakeVitMinPoolLayout(vit_route.tokens)",
        "return result.size()==NR::kVitMinBlocks;",
        "#else",
        "b.input=i?result.back().output:Make(plane);",
        "b.value=Make(plane*4u);",
        "#endif",
    )
    if any(marker not in native for marker in markers):
        raise SystemExit("R09_MIN_EXTRACT_FAIL: missing native source contract")
    prefix=r"""// Generated from pinned native vendor source. Do not edit manually.
#pragma once
#include "rdna2_nr_vit_min_pool_policy.h"
#include <array>
#include <cstdint>
#include <vector>
namespace NR=Eden::Graphics::NeuralRDNA2;
struct VkDescriptorBufferInfo {
    std::uint64_t buffer{},offset{},range{};
};
struct NrRdna2VitBlockBuffers {
    VkDescriptorBufferInfo input{},hidden{},ffwd{},attention_context{},output{};
    VkDescriptorBufferInfo scratch{},query{},key{},value{};
};
struct HostVitRoute {std::uint32_t count{},tokens{};};
struct HostFrameState {
    HostVitRoute vit_route{};
    std::uint64_t next_id{},fail_on{};
    std::vector<std::uint64_t> requested{};
    VkDescriptorBufferInfo Make(std::uint64_t bytes) {
        ++next_id;
        if(bytes==0u || (fail_on!=0u&&next_id==fail_on))return {};
        requested.push_back(bytes);
        return {next_id,0u,bytes};
    }
"""
    out=prefix+native+"\n};\n"
    target=Path(sys.argv[2])
    target.parent.mkdir(parents=True,exist_ok=True)
    target.write_text(out)
    print("R09_MIN_NATIVE_ALLOCATOR_EXTRACTED sha256="+
          hashlib.sha256(native.encode()).hexdigest()[:20])
if __name__=="__main__":
    main()
