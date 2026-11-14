#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Focused semantic/source proof for Lichii #1320 / Eglantine #825 SSBO failed-search pruning."""
from collections import deque
from pathlib import Path
import hashlib
import random

ROOT=Path(__file__).resolve().parents[1]
BFS=ROOT/"encore-active/eden-shader/include/shader_recompiler/frontend/ir/breadth_first_search.h"
ACTIVE=ROOT/"encore-active/eden-shader/src/shader_recompiler/ir_opt/global_memory_to_storage_buffer_pass.cpp"
PINNED=ROOT/"sources/eden/src/shader_recompiler/ir_opt/global_memory_to_storage_buffer_pass.cpp"

def blob(path: Path) -> str:
    raw=path.read_bytes()
    return hashlib.sha1(b"blob "+str(len(raw)).encode()+b"\0"+raw).hexdigest()

assert blob(PINNED)=="4ce1ff6c19c4937855d4460a515aec6a8a1e07e0"
assert blob(BFS)=="05a5f9c9c99ca92530bb80f079ae045951c43dae"
assert blob(ACTIVE)=="41ad0f5cef1e070e552b43ea49648d5e74f10bad"
bfs=BFS.read_text()
active=ACTIVE.read_text()
for needle in (
    "BreadthFirstSearchPruned(Value value",
    "exhausted.contains(resolved.InstRecursive())",
    "exhausted.insert(visited.begin(), visited.end())",
):
    assert bfs.count(needle)==1, needle
for needle in (
    "biased_tracks", "unbiased_tracks", "biased_exhausted", "unbiased_exhausted",
    "BreadthFirstSearchPruned(value, pred, exhausted)",
    "cached_track(&nvn_bias, info.biased_tracks, info.biased_exhausted)",
    "cached_track(nullptr, info.unbiased_tracks, info.unbiased_exhausted)",
):
    assert needle in active, needle
assert "BreadthFirstSearchPruned" not in PINNED.read_text()
assert "biased_exhausted" not in PINNED.read_text()

# Model the exact invariant used by the C++ wrapper: immutable graph, same
# rightmost-first BFS, and exhausted sets isolated per predicate domain.
def bfs(graph, root, accepts, blocked=frozenset()):
    if root is None:
        return None, []
    q=deque([root])
    visited_small=[]
    seen=set()
    visited=[]
    while q:
        node=q.popleft()
        visited.append(node)
        if accepts(node):
            return node, visited
        for child in reversed(graph[node]):
            if child is None or child in blocked:
                continue
            if seen:
                if child in seen:
                    continue
                seen.add(child)
            else:
                if child in visited_small:
                    continue
                visited_small.append(child)
                if len(visited_small)>64:
                    seen.update(visited_small)
            q.append(child)
    return None, visited

def pruned(graph, root, accepts, exhausted):
    result,visited=bfs(graph,root,accepts,exhausted)
    if result is None:
        exhausted.update(visited)
    return result

rng=random.Random(1320)
for case in range(200):
    n=8+rng.randrange(80)
    graph=[]
    for i in range(n):
        children=[]
        for _ in range(rng.randrange(5)):
            children.append(rng.randrange(n) if rng.randrange(5) else None)
        graph.append(children)
    # Two distinct domains emulate NVN-biased and unbiased SSBO predicates.
    biased_targets={i for i in range(n) if i%17==3}
    unbiased_targets=biased_targets|{i for i in range(n) if i%11==5}
    roots=[rng.randrange(n) for _ in range(50)]
    bex=set(); uex=set()
    for root in roots:
        base_b,_=bfs(graph,root,biased_targets.__contains__)
        got_b=pruned(graph,root,biased_targets.__contains__,bex)
        assert got_b==base_b,(case,root,"biased",got_b,base_b)
        base_u,_=bfs(graph,root,unbiased_targets.__contains__)
        got_u=pruned(graph,root,unbiased_targets.__contains__,uex)
        assert got_u==base_u,(case,root,"unbiased",got_u,base_u)
    # Sharing the two exhausted sets would be invalid whenever a node is
    # rejected only by the biased predicate; the implementation keeps them separate.
    assert bex is not uex

print("FC27_SSBO_SEARCH_PRUNING_HOST_PASS cases=200 roots=10000 domains=2 pinned_immutable=1 scalar_cache_separate=1 gameplay_unverified=1")
