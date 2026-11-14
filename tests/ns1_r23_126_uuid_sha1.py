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
end="    *out_pseudo_device_id = Common::UUID::MakeRFC4122V5(std::span<u8, 16>{hash, 16});"
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
selected=root/"encore-active/ns2-runtime/am/core/hle/service/am/service/application_functions.cpp"
selected_bytes=selected.read_bytes()
selected_blob=hashlib.sha1(b"blob "+str(len(selected_bytes)).encode()+bytes([0])+selected_bytes).hexdigest()
# The selected AM unit also carries the independently reviewed NS2 CMIF
# save-account UID alignment fix (Eglantine #80). Preserve this exact
# post-change blob; the NS1 SHA1 pseudo-device implementation is unchanged.
assert selected_blob=="b8787c322258fcccd7c5d53a74097a5619e2211c",selected_blob
# Reverse only the reviewed diagnostic additions and prove the exact original
# CMIF/UUID owner is recovered. Keep the preceding historical receipt intact.
selected_src=selected_bytes.decode()
inc="#include <cstdio>\n#include <mutex>\n#include <optional>\n\n"
helper=selected_src.split("// Diagnostic only: no changes",1)[1].split("FileSys::PatchManager::Metadata GetApplicationMetadata",1)[0]
helper="// Diagnostic only: no changes"+helper
call=selected_src.split("    const GuestLanguageReply reply{",1)[1].split("    R_SUCCEED();",1)[0]
call="    const GuestLanguageReply reply{"+call
assert selected_src.count(inc)==selected_src.count(helper)==selected_src.count(call)==1
prior=selected_src.replace(inc,"",1).replace(helper,"",1).replace(call,"",1).encode()
prior_blob=hashlib.sha1(b"blob "+str(len(prior)).encode()+bytes([0])+prior).hexdigest()
assert prior_blob=="1fa9b3437d72c4ad36d61ca4c190ece64149c75c",prior_blob
# Execute the actual bounded diagnostic decision function, not a copy.
assert selected_src.index("    R_TRY(app_man->ConvertApplicationLanguageToLanguageCode(out_language_code, desired_language));") < selected_src.index("    const GuestLanguageReply reply{")
assert "*out_language_code, res.first != nullptr" in call
assert "reply_code=%016llX reply_text=%s" in call
assert "static_cast<char>((reply.language_code >> (8u * i)) & 0xffu)" in call
source_witness=r"""
#include <cassert>
#include <cstdint>
#include <mutex>
#include <optional>
#include <thread>
#include <atomic>
#include <vector>
using u32=std::uint32_t;
using u64=std::uint64_t;
""" + helper + r"""
int main() {
    GuestLanguageReply fr{1,8,2,0x7266,true};
    assert(ShouldReportGuestLanguage(fr));
    for(int i=0;i<1000;++i) assert(!ShouldReportGuestLanguage(fr));
    auto ca=fr; ca.configured_index=13; ca.language_code=0x41432d7266ULL;
    assert(ShouldReportGuestLanguage(ca)); assert(!ShouldReportGuestLanguage(ca));
    auto unknown=ca; unknown.has_metadata=false; unknown.supported_languages=0;
    assert(ShouldReportGuestLanguage(unknown));
    auto next=unknown; next.program_id=2;
    std::atomic<unsigned> accepted{0};
    std::vector<std::thread> threads;
    for(int i=0;i<8;++i) threads.emplace_back([&]{
        for(int j=0;j<1000;++j) if(ShouldReportGuestLanguage(next)) ++accepted;
    });
    for(auto& thread:threads) thread.join();
    assert(accepted==1);
    for(unsigned i=0;i<28;++i) {
        next.program_id=3+i;
        assert(ShouldReportGuestLanguage(next));
    }
    next.program_id=100;
    assert(!ShouldReportGuestLanguage(next)); // hard process cap: 32
}
"""
with tempfile.TemporaryDirectory(prefix="guest-language-reply-") as tmp:
    p=Path(tmp);(p/"t.cpp").write_text(source_witness)
    for opt in ("-O0","-O2"):
        subprocess.run([os.getenv("CXX","clang++"),"-std=c++20",opt,"-Wall","-Wextra","-Werror",
                        "-pthread",str(p/"t.cpp"),"-o",str(p/"test")],check=True,timeout=60)
        subprocess.run([str(p/"test")],check=True,timeout=20)
print("EDEN_GUEST_LANGUAGE_REPLY_WITNESS_HOST_PASS exact_ancestor=1 duplicate=1 concurrent=1 cap=32")
assert "FileSys::SaveDataType type, u128 user_id)" in selected.read_text()
assert 'std::span<u8, 16>{hash, 16}' in selected.read_text()
assert 'std::span<u8, 20>{hash, std::size(hash)}' not in selected.read_text()

for rel, blob in [
 ("src/common/uuid.cpp","4b992c5df89897107226bbfa591ed29b332d968b"),
 ("include/common/uuid.h","465b5be58d7c8a948a2b5acba640ce47bd4bd73b"),
]:
    p=root/"encore-active/eden-core"/rel
    raw=p.read_bytes()
    actual=hashlib.sha1(b"blob "+str(len(raw)).encode()+bytes([0])+raw).hexdigest()
    assert actual==blob,(rel,actual)
assert "std::span<u8, 16> sha1" in (root/"encore-active/eden-core/include/common/uuid.h").read_text()

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
[[maybe_unused]] constexpr int ResultUnknown=1;
#define R_UNLESS(c,e) if(!(c)) return false
namespace Common {
struct UUID {
 std::array<u8,16> uuid{};
 static UUID MakeRFC4122V5(std::span<u8,16> sha1) {
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
print("NS1_R23_126_UUID_SHA1_SPAN_HOST_PASS deterministic=1 digest_len=20 uuid_v5_prefix=16 missing_NACP=1")
