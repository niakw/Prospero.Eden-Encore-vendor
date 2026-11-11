#!/usr/bin/env python3
"""NS1 #126 maintained AM pseudo-device ID: exact SHA-1 and fixed-span proof."""
from pathlib import Path
import hashlib
import os
import subprocess
import tempfile

root=Path(__file__).resolve().parents[1]
f=root/"encore-active/eden-core/src/core/hle/service/am/service/application_functions.cpp"
source=f.read_text()
start="    u8 hash[EVP_MAX_MD_SIZE]{};"
end="    *out_pseudo_device_id = Common::UUID::MakeRFC4122V5(std::span<u8, 20>{hash, 20});"
assert source.count(start)==1 and source.count(end)==1
new=start+source.split(start,1)[1].split(end,1)[0]+end
old="""    u8 hash[EVP_MAX_MD_SIZE];
    unsigned int hash_len = 0;
    auto const seed = res.first != nullptr ? res.first->raw.seed_for_pseudo_device_id : u64_le{0};
    EVP_MD_CTX *ctx = EVP_MD_CTX_new();
    auto const algorithm = EVP_sha1();
    EVP_DigestInit_ex(ctx, algorithm, nullptr);
    EVP_DigestUpdate(ctx, &seed, sizeof(seed));
    EVP_DigestFinal_ex(ctx, hash, &hash_len);
    EVP_MD_CTX_free(ctx);
    *out_pseudo_device_id = Common::UUID::MakeRFC4122V5(std::span<u8, 20>{hash, std::size(hash)});"""
recovered=source.replace(new,old,1).encode()
sha=hashlib.sha1(b"blob "+str(len(recovered)).encode()+bytes([0])+recovered).hexdigest()
assert sha=="6f80d50d4c732004bef6ff387dbbc68e0778d1e3",sha
assert "R_UNLESS(ok && hash_len == 20, ResultUnknown);" in new
assert "R_UNLESS(ctx != nullptr, ResultUnknown);" in new
test=r"""
#include <array>
#include <cassert>
#include <cstdint>
#include <memory>
#include <span>
#include <cstring>
#include <openssl/evp.h>
using u8=std::uint8_t;
using u64_le=std::uint64_t;
struct Nacp { struct {u64_le seed_for_pseudo_device_id{0};} raw; };
struct Metadata {std::shared_ptr<Nacp> first;};
constexpr int ResultUnknown=1;
#define R_UNLESS(c,e) if(!(c)) return false
namespace Common {
struct UUID {
 std::array<u8,16> uuid{};
 static UUID MakeRFC4122V5(std::span<u8,20> sha1) {
  UUID v;
  std::memcpy(v.uuid.data(),sha1.data(),v.uuid.size());
  v.uuid[8]=0x80 | (v.uuid[8] & 0x3F);
  v.uuid[6]=0x50 | (v.uuid[6] & 0x0F);
  return v;
 }
};
}
bool original(Method) {return true;}
"""
test=test.replace("bool original(Method) {return true;}", "bool compute(const Metadata& res, Common::UUID* out_pseudo_device_id) {\n"+new+"\n return true;\n}\n"+r"""
int main() {
 Metadata none{};
 Common::UUID zero{}, one{}, second{};
 assert(compute(none,&zero));
 auto n=std::make_shared<Nacp>();
 n->raw.seed_for_pseudo_device_id=0x123456789abcedffULL;
 Metadata data{n};
 assert(compute(data,&one));
 assert(compute(data,&second));
 assert(one.uuid==second.uuid);
 assert(one.uuid!=zero.uuid);
 assert((one.uuid[6]&0xF0)==0x50);
 assert((one.uuid[8]&0xC0)==0x80);
}
""")
with tempfile.TemporaryDirectory(prefix="r23-uuid-") as tmp:
 p=Path(tmp);(p/"t.cpp").write_text(test)
 subprocess.run([os.getenv("CXX","clang++"),"-std=c++20","-O1","-g","-Wall","-Wextra","-Werror",
   "-fsanitize=address,undefined",str(p/"t.cpp"),"-lcrypto","-o",str(p/"test")],check=True,timeout=60)
 subprocess.run([str(p/"test")],check=True,timeout=20,env={**os.environ,"ASAN_OPTIONS":"detect_leaks=1:halt_on_error=1"})
print("NS1_R23_126_UUID_SHA1_SPAN_HOST_PASS deterministic=1 digest_len=20 missing_NACP=1")
