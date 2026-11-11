#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Bounded source guard for the DISABLED-BY-DEFAULT FSwin scratch experiment.

Must fail if a native handle owner, copy, barrier, descriptor path or
preprocessor isolation changes. This is NOT a Vulkan SYNC validation.
"""
from __future__ import annotations
import re
import sys
from pathlib import Path
ROOT=Path("encore-active/eden-vulkan")
PATHS={
    "tensor":ROOT/"nr_rdna2_fswin_gpu_tensors.cpp",
    "tensor_h":ROOT/"include/video_core/renderer_vulkan/nr_rdna2_fswin_gpu_tensors.h",
    "runtime":ROOT/"nr_rdna2_fswin_layer_runtime.cpp",
    "runtime_h":ROOT/"include/video_core/renderer_vulkan/nr_rdna2_fswin_layer_runtime.h",
    "chain":ROOT/"nr_rdna2_fswin_chain.cpp",
    "chain_h":ROOT/"include/video_core/renderer_vulkan/nr_rdna2_fswin_chain.h",
    "graph":ROOT/"nr_rdna2_fswin_graph.cpp",
    "dense":ROOT/"nr_rdna2_fswin_dense.cpp",
    "repack":ROOT/"nr_rdna2_fswin_window_repack.cpp",
}
GATE="EDEN_R09_MIN_FSWIN_SCRATCH_POOL_EXPERIMENTAL"

def compact(s:str)->str:
    return re.sub(r"\s+","",re.sub(r"//[^\n]*","",s))
def require(s:str,pattern:str,reason:str)->None:
    if pattern not in s:
        raise ValueError(f"R09_FSWIN_SCRATCH_DRIFT {reason}")
def verify(files:dict[str,str])->None:
    if set(files)!=set(PATHS):
        raise ValueError("Unexpected native owner set")
    token={k:compact(v) for k,v in files.items()}
    for name in ("tensor","tensor_h","runtime","runtime_h",
                 "chain","chain_h"):
        require(token[name],GATE,f"missing isolation in {name}")
    for name in ("tensor","runtime","chain"):
        require(token[name],"#else" if name=="chain" else "InitializeWithScratch",
                f"opt-in path not present: {name}")
    require(token["tensor"],"boolNrRdna2FSwinGpuTensors::Initialize(",
            "existing default native tensor method removed")
    require(token["tensor"],"boolNrRdna2FSwinGpuTensors::InitializeWithScratch(",
            "new real borrowed native tensor method missing")
    require(token["tensor"],"shared_scratch_active=true;",
            "published shared handles without flag")
    require(token["tensor"],"shared_scratch[slot].buffer",
            "native descriptor does not point to borrowed owner")
    require(token["tensor"],"created[i]=allocator.CreateBuffer(",
            "per-layer Input/Output owners no longer created")
    require(token["tensor"],
            "i==0u?MemoryUsage::Upload:MemoryUsage::DeviceLocal",
            "per-layer input/output memory types changed")
    require(token["tensor"],"view.range<needed",
            "borrowed descriptor capacity no longer guarded")
    require(token["tensor"],"scratch[j].buffer==view.buffer",
            "scratch role handles no longer distinct")
    require(token["tensor"],"requested[8]!=shape.private_output_bytes",
            "private layer output changed")
    require(token["runtime"],"tensors.Initialize(proposed)",
            "legacy runtime allocation modified")
    require(token["runtime"],"tensors.InitializeWithScratch(proposed,scratch)",
            "runtime no longer registers borrowed descriptors")
    require(token["chain"],"runtime->Initialize(model,spec.prefix",
            "legacy chain path no longer present")
    require(token["chain"],"runtime->InitializeWithScratch(",
            "native chain opt-in path missing")
    require(token["chain"],"chain_shared_scratch=std::move(scratch_owner)",
            "chain no longer retains borrowed handles")
    require(token["chain"],"layers=std::move(prepared)",
            "chain no longer retains borrowed layer owners")
    require(token["chain"],"if(!layers[i]->Record())returnfalse",
            "sequential layer record removed")
    require(token["chain"],"layers[i]->LinkInputFrom(*layers[i-1u])",
            "previous layer input copy removed")
    require(token["runtime"],"if(!src||!dst||src==dst||!size)returnfalse",
            "same-handle VkBuffer copies accepted")
    require(token["runtime"],"cmd.CopyBuffer(src,dst,copy)",
            "native dependency copy removed")
    require(token["runtime"],"VK_ACCESS_TRANSFER_WRITE_BIT",
            "native post-copy visibility removed")
    require(token["graph"],"dense.Record(sets.dense[3],plan.dense[3]",
            "terminal dense epilogue removed")
    require(token["dense"],"cmd.PipelineBarrier(",
            "terminal compute shader barrier removed")
    require(token["repack"],"cmd.PipelineBarrier(",
            "shifted-window compute barrier removed")
    # Scratch must outlive ALL borrowing layers as defined in the C++ ABI:
    header=token["chain_h"]
    a=header.find("std::array<vk::Buffer,4>chain_shared_scratch")
    b=header.find("std::vector<std::unique_ptr<NrRdna2FSwinLayerRuntime>>layers")
    if a<0 or b<=a:
        raise ValueError("R09_FSWIN_SCRATCH_OWNER_DESTRUCTION_ORDER_FAIL")
    # New scratch ownership must be published BEFORE the vector of borrowers.
    chain=token["chain"]
    own=chain.find("chain_shared_scratch=std::move(scratch_owner)")
    borrowers=chain.find("layers=std::move(prepared)")
    if own<0 or borrowers<own:
        raise ValueError("R09_FSWIN_SCRATCH_OWNER_PUBLICATION_ORDER_FAIL")
    # Negative control: never promote source checks into GPU qualification.
    require(token["tensor_h"],"shared_scratch_active",
            "missing explicit borrowed state")

def negatives(source:dict[str,str])->int:
    cases=(
        ("default input/output ownership","tensor",
         "i==0u?MemoryUsage::Upload:MemoryUsage::DeviceLocal",
         "i==0u?MemoryUsage::DeviceLocal:MemoryUsage::Upload"),
        ("real shared descriptor","tensor",
         ".buffer=shared_scratch[slot].buffer",
         ".buffer=VK_NULL_HANDLE"),
        ("scratch role alias","tensor",
         "scratch[j].buffer==view.buffer",
         "scratch[j].buffer!=view.buffer"),
        ("scratch capacity validation","tensor",
         "view.range<needed","view.range>needed"),
        ("chain pool lifetime","chain_h",
         "std::array<vk::Buffer,4> chain_shared_scratch",
         "std::array<vk::Buffer,0> chain_shared_scratch"),
        ("chain owner publication","chain",
         "chain_shared_scratch=std::move(scratch_owner);",
         "chain_shared_scratch={};"),
        ("native copied input","runtime",
         "cmd.CopyBuffer(src,dst,copy);",
         "cmd.CopyBuffer(src,src,copy);"),
        ("terminal compute barrier","dense",
         "cmd.PipelineBarrier(","cmd.NotPipelineBarrier("),
    )
    for name,key,before,after in cases:
        if before not in source[key]:
            raise ValueError("Mutation input drift: "+name)
        mutation=dict(source)
        mutation[key]=source[key].replace(before,after)
        try:verify(mutation)
        except ValueError:continue
        raise ValueError("Unsafe mutation accepted: "+name)
    return len(cases)
def main()->None:
    if any(not p.is_file() for p in PATHS.values()):
        raise SystemExit("R09_FSWIN_SCRATCH_SOURCE_MISSING")
    source={name:p.read_text(encoding="utf-8")
            for name,p in PATHS.items()}
    try:
        verify(source)
        count=negatives(source)
    except ValueError as exc:
        print(exc,file=sys.stderr)
        raise SystemExit(1)
    print("R09_FSWIN_SCRATCH_NATIVE_OWNER_GUARD_PASS "
          f"owners={len(source)} deliberate_rejections={count} "
          "main_unchanged=YES runtime_default=UNMODIFIED")
if __name__=="__main__":
    main()
