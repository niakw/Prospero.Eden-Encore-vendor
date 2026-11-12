#!/usr/bin/env python3
"""NS1 R23 #33: extract and run real native ConstantPool method under sanitizers."""
from pathlib import Path
import hashlib
import os
import subprocess
import tempfile

root=Path(__file__).resolve().parents[1]
cpp=root/"encore-active/eden-core/src/dynarmic/backend/x64/constant_pool.cpp"
h=root/"encore-active/eden-core/include/dynarmic/backend/x64/constant_pool.h"
s=cpp.read_text()
head=h.read_text()
def gitsha(value):
 raw=value.encode()
 return hashlib.sha1(b"blob "+str(len(raw)).encode()+bytes([0])+raw).hexdigest()
assert '::Common::unordered_map<ConstantT, u32, ConstantHash> constant_info;' in head
assert 'writableAddress(&target_constant)' in s
assert 'frame[code.rip + static_cast<void*>(&pool[it->second])]' in s
assert 'code.int3();' in s
assert 'ASSERT(next_slot < pool.size())' in s
old="""    auto it = constant_info.find(constant);
    if (it == constant_info.end()) {
        ASSERT(insertion_point < pool.size());
        ConstantT& target_constant = pool[insertion_point];
        *reinterpret_cast<ConstantT*>(code.writableAddress(&target_constant)) = constant;
        it = constant_info.insert({constant, &target_constant}).first;
        ++insertion_point;
    }
    return frame[code.rip + it->second];"""
start="    // Track the slot index rather than a duplicate pointer."
end="    return frame[code.rip + static_cast<void*>(&pool[it->second])];"
new=start+s.split(start,1)[1].split(end,1)[0]+end
pre=s.replace(new,old,1)
assert gitsha(pre)=="c51408a77701705c8bbc341c074df1e582495104",gitsha(pre)
original_header=head.replace("::Common::unordered_map<ConstantT, u32, ConstantHash>", "::Common::unordered_map<ConstantT, void*, ConstantHash>",1)
assert gitsha(original_header)=="d3bad95d701c75b3a3e616a46a916590387e867e"
methodstart="Xbyak::Address ConstantPool::GetConstant("
method=methodstart+s.split(methodstart,1)[1].split("\n}\n",1)[0]+"\n}"
cpp_test=r"""
#include <cassert>
#include <array>
#include <cstdint>
#include <bit>
#include <cstddef>
#include <span>
#include <unordered_map>
#include <utility>
using u32=std::uint32_t;
using u64=std::uint64_t;
#define ASSERT(e) assert((e))
namespace Xbyak {
 using Address=std::uintptr_t;
 struct AddressFrame {
   Address operator[](Address value) const {return value;}
 };
}
namespace Dynarmic::Backend::X64 {
struct BlockOfCode {
 int aliases=0;
 struct Rip {
  uintptr_t operator+(void* value) const {return reinterpret_cast<uintptr_t>(value);}
 } rip;
 void* writableAddress(void* p){++aliases; return p;}
};
class ConstantPool {
public:
 using ConstantT=std::pair<u64,u64>;
 struct ConstantHash {
  size_t operator()(const ConstantT& t) const noexcept {return t.first ^ std::rotl<u64>(t.second,1);}
 };
 std::unordered_map<ConstantT,u32,ConstantHash> constant_info{};
 std::span<ConstantT> pool;
 ConstantPool(std::span<ConstantT> p):pool(p){}
 Xbyak::Address GetConstant(BlockOfCode&,const Xbyak::AddressFrame&,u64,u64);
};
""" + method + r"""
}
int main() {
 using namespace Dynarmic::Backend::X64;
 std::array<ConstantPool::ConstantT,5> memory{};
 ConstantPool p(memory);
 BlockOfCode code;
 Xbyak::AddressFrame frame;
 auto a=p.GetConstant(code,frame,0x1122334455667788ULL,0xFEDCBA9876543210ULL);
 auto b=p.GetConstant(code,frame,0x1122334455667788ULL,0xFEDCBA9876543210ULL);
 assert(a==b && code.aliases==1);
 assert(memory[0].first==0x1122334455667788ULL);
 assert(memory[0].second==0xFEDCBA9876543210ULL);
 auto c=p.GetConstant(code,frame,0x1122334455667788ULL,0);
 assert(c!=a && code.aliases==2);
 for(int i=0;i<3;i++) p.GetConstant(code,frame,u64(i+100),u64(i+200));
 assert(code.aliases==5);
 assert(p.constant_info.size()==5);
}
""";
with tempfile.TemporaryDirectory(prefix="ns1-cpool-") as tmp:
 p=Path(tmp)
 (p/"t.cpp").write_text(cpp_test)
 subprocess.run([os.environ.get("CXX","clang++"),"-std=c++20","-O1","-g",
    "-fsanitize=address,undefined","-fno-omit-frame-pointer",str(p/"t.cpp"),
    "-o",str(p/"test")],check=True,timeout=90)
 subprocess.run([str(p/"test")],check=True,timeout=20,
    env={**os.environ,"ASAN_OPTIONS":"detect_leaks=1:halt_on_error=1"})
print("R23_33_POOL_INDEX_AND_WX_ALIAS_ASAN_UBSAN_PASS duplicates=1 high_64bit=1 five_slots=1")
