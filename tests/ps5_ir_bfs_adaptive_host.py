#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Compare the exact selected adaptive BFS against its byte-verified predecessor."""
from pathlib import Path
import hashlib
import json
import subprocess
import tempfile

ROOT=Path(__file__).resolve().parents[1]
header=ROOT/"encore-active/eden-shader/include/shader_recompiler/frontend/ir/breadth_first_search.h"
source=header.read_text()
baseline=source
for new,old in json.loads("[[\"#include <algorithm>\\n#include <functional>\\n#include <optional>\\n#include <type_traits>\\n#include <queue>\\n#include <unordered_set>\",\"#include <functional>\\n#include <optional>\\n#include <type_traits>\\n#include <queue>\"],[\"    boost::container::small_vector<const Inst*, 2> visited;\\n    // Lichii nx2: keep the small shader path, but avoid quadratic membership\\n    // scans on large IR graphs. The queue and rightmost-first order are unchanged.\\n    std::unordered_set<const Inst*> visited_set;\\n    const auto first_visit = [&](const Inst* inst) {\\n        if (!visited_set.empty()) {\\n            return visited_set.insert(inst).second;\\n        }\\n        if (std::ranges::find(visited, inst) != visited.end()) {\\n            return false;\\n        }\\n        visited.push_back(inst);\\n        if (visited.size() > 64) {\\n            visited_set.insert(visited.begin(), visited.end());\\n        }\\n        return true;\\n    };\\n    std::queue<const Inst*> queue;\",\"    boost::container::small_vector<const Inst*, 2> visited;\\n    std::queue<const Inst*> queue;\"],[\"            if (first_visit(arg_inst)) {\\n                queue.push(arg_inst);\\n            }\",\"            if (std::ranges::find(visited, arg_inst) == visited.end()) {\\n                visited.push_back(arg_inst);\\n                queue.push(arg_inst);\\n            }\"]]"):
    assert baseline.count(new)==1
    baseline=baseline.replace(new,old,1)
raw=baseline.encode()
assert hashlib.sha1(b"blob "+str(len(raw)).encode()+bytes([0])+raw).hexdigest()=="afe153f18075d00b8e822a1d992dcd8f5cad9a8c"
mock=r"""
#pragma once
#include <cstddef>
#include <vector>
namespace Shader::IR {
struct Inst;
struct Value {
    const Inst* ptr{};
    bool IsImmediate() const { return ptr==nullptr; }
    const Inst* InstRecursive() const { return ptr; }
};
struct Inst {
    size_t id{};
    std::vector<Value> args;
    size_t NumArgs() const { return args.size(); }
    Value Arg(size_t n) const { return args.at(n); }
};
}
"""
code=r"""
#include <algorithm>
#include <cassert>
#include <cstdio>
#include <random>
#include <vector>
#define BreadthFirstSearch BaselineBreadthFirstSearch
#include "baseline.h"
#undef BreadthFirstSearch
#include "candidate.h"
using namespace Shader::IR;
struct Result {
    std::optional<size_t> match;
    std::vector<size_t> visited;
    bool operator==(const Result&) const = default;
};
template<bool Old,typename Resolve>
Result run(Value root, size_t wanted, Resolve resolve) {
    Result out;
    auto pred=[&](const Inst* x)->std::optional<size_t>{
        out.visited.push_back(x->id);
        if(x->id==wanted) return x->id;
        return std::nullopt;
    };
    if constexpr(Old) out.match=BaselineBreadthFirstSearch(root,pred,resolve);
    else out.match=BreadthFirstSearch(root,pred,resolve);
    return out;
}
int main() {
    auto identity=[](Value v){return v;};
    for(size_t n:{1u,2u,63u,64u,65u,66u,127u,512u,4096u}){
        std::vector<Inst> nodes(n);
        for(size_t i=0;i<n;++i){
            nodes[i].id=i;
            if(i+1<n) nodes[i].args.push_back({&nodes[i+1]});
            if(i+2<n) nodes[i].args.push_back({&nodes[i+2]});
            nodes[i].args.push_back({});
        }
        // Shared subgraphs and a cycle; preserve the predecessor's exact
        // visit sequence including any revisit of the initial node.
        nodes.back().args.push_back({&nodes.front()});
        auto resolve=[&](Value v){
            return v.ptr && v.ptr->id==2 && n>3 ? Value{&nodes[3]}: v;
        };
        for(auto wanted:{size_t{0},n/2,n-1,n}){
            assert(run<true>({&nodes[0]},wanted,identity)==run<false>({&nodes[0]},wanted,identity));
            assert(run<true>({&nodes[0]},wanted,resolve)==run<false>({&nodes[0]},wanted,resolve));
        }
        assert(run<true>({},n,identity)==run<false>({},n,identity));
    }
    std::mt19937 rng(2718);
    for(unsigned seed=0;seed<20;++seed) {
        std::vector<Inst> nodes(128);
        for(size_t i=0;i<nodes.size();++i) {
            nodes[i].id=i;
            for(unsigned j=0;j<3;++j) nodes[i].args.push_back({&nodes[rng()%nodes.size()]});
        }
        assert(run<true>({&nodes[0]},127,identity)==run<false>({&nodes[0]},127,identity));
        assert(run<true>({&nodes[0]},128,identity)==run<false>({&nodes[0]},128,identity));
    }
    std::vector<Inst> large(28000);
    for(size_t i=0;i<large.size();++i){
        large[i].id=i;
        if(i+1<large.size())large[i].args.push_back({&large[i+1]});
    }
    auto result=run<false>({&large[0]},large.size(),identity);
    assert(!result.match && result.visited.size()==large.size());
    for(size_t i=0;i<large.size();++i)assert(result.visited[i]==i);
    std::puts("IR_BFS_ADAPTIVE_HOST_PASS predecessor_exact=1 order_and_match=1 custom_resolver=1 cyclic_shared=1 random_graphs=20 large_nodes=28000 actual_boost=1 guest_shader_corpus=unverified");
}
"""
boost=ROOT/"sources/eden-cpm/boost/boost-1.90.0/libs"
includes=sorted(boost.glob("*/include"))
assert len(includes)>100
with tempfile.TemporaryDirectory(prefix="ir-bfs-order-") as td:
    base=Path(td)
    fake=base/"shader_recompiler/frontend/ir/value.h"
    fake.parent.mkdir(parents=True)
    fake.write_text(mock)
    (base/"baseline.h").write_text(baseline)
    (base/"candidate.h").write_text(source)
    (base/"test.cpp").write_text(code)
    exe=base/"test"
    subprocess.run(["clang++","-std=c++20","-O1","-g","-fsanitize=address,undefined","-fno-sanitize-recover=all",
                    f"-I{base}",*[f"-I{x}" for x in includes],str(base/"test.cpp"),"-o",str(exe)],check=True,timeout=90)
    subprocess.run([str(exe)],check=True,timeout=60)
