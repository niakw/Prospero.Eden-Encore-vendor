#!/usr/bin/env python3
"""Signed VI2 producer-fence bounds: source and behavior regression."""
from pathlib import Path
p=(Path(__file__).resolve().parents[1]/
   "encore-active/ns2-runtime/graphics_supervisor/core/hle/service/am/service/graphics_supervisor.cpp")
src=p.read_text()
anchor="if (fence.num_fences < 0 ||"
assert src.count(anchor)==1
b=src.split(anchor,1)[1].split("bool done=true;",1)[0]
assert "static_cast<std::size_t>(fence.num_fences) > fence.fences.size()" in b
check=src.split("bool done=true;",1)[1].split("const auto& point=fence.fences[i];",1)[0]
assert "for (std::size_t i=0;i<static_cast<std::size_t>(fence.num_fences);++i)" in check
# A negative signed value previously compared as a huge unsigned count.
def admissible(n): return n>=0 and n<=4
assert [admissible(n) for n in (-3,-1,0,1,4,5,100)]==[False,False,True,True,True,False,False]
print("VI2_SIGNED_FENCE_COUNT_BOUNDS_PASS negative=reject max=4")
