#!/usr/bin/env python3
"""R23 #120: coherent SSL backend/IPC source + extracted Peek behavior under ASan."""
from pathlib import Path
import hashlib
import os
import subprocess
import tempfile

ROOT=Path(__file__).resolve().parents[1]
DIR=ROOT/"encore-active/eden-core/src/core/hle/service/ssl"
EXPECTED={
 "ssl.cpp":"178c4c690b04f4140538cc2855dd48341a88b4c1",
 "ssl_backend.h":"ef6bf8f9def5f80544e75c180ac72275a0325cc9",
 "ssl_backend_openssl.cpp":"71ce86365b9ff511f8a2fe550afd7d5db957e5c5",
}
def sha(p):
 b=p.read_bytes()
 return hashlib.sha1(b"blob "+str(len(b)).encode()+bytes([0])+b).hexdigest()
for file,expected in EXPECTED.items():
 assert sha(DIR/file)==expected,(file,sha(DIR/file))
print("R23_120_SSL_THREE_SOURCE_PAIR_PASS")
service=(DIR/"ssl.cpp").read_text()
header=(DIR/"ssl_backend.h").read_text()
backend=(DIR/"ssl_backend_openssl.cpp").read_text()
for name in ("Peek","SetRenegotiationMode","GetRenegotiationMode"):
 assert name+"(" in service and name+"(" in header and name+"(" in backend,name
assert "{20, D<&ISslConnection::SetRenegotiationMode>" in service
assert "{21, D<&ISslConnection::GetRenegotiationMode>" in service
assert "SSL_pending(ssl)" in backend

start="    Result Peek(size_t* out_size, std::span<u8> data) override {"
end="\n    Result Write(size_t* out_size"
method=start+backend.split(start,1)[1].split(end,1)[0]
assert method.count('HandleReturn("SSL_peek"')==1
assert "static_cast<size_t>(ret)" in method
fixture=r"""
#include <algorithm>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <span>
using u8=std::uint8_t;
using Result=int;
#define R_SUCCEED() return 0
struct SSL {};
int response=0, calls=0, len_seen=-1;
int SSL_peek(SSL*,void*,int len){++calls;len_seen=len;return response;}
struct Base {virtual Result Peek(size_t*,std::span<u8>)=0;};
struct Test : Base {
 SSL* ssl=nullptr;
 Result HandleReturn(const char*,size_t*,int ret){return ret<0?2:0;}
""" + method + r"""
};
int main(){
 Test t;
 u8 buffer[8]{};
 size_t n=0;
 assert(t.Peek(&n,std::span<u8>{buffer})==0);
 assert(n==0 && calls==0);
 response=3;n=8;
 assert(t.Peek(&n,std::span<u8>{buffer})==0);
 assert(n==3 && calls==1 && len_seen==8);
 response=-1;n=6;
 assert(t.Peek(&n,std::span<u8>{buffer})==2);
 assert(n==0 && len_seen==6);
 response=2;n=4;
 assert(t.Peek(&n,std::span<u8>{buffer})==0);
 assert(n==2 && len_seen==4);
}
""";
with tempfile.TemporaryDirectory(prefix="r23-ssl-") as tmp:
 p=Path(tmp)
 (p/"test.cpp").write_text(fixture)
 subprocess.run([os.getenv("CXX","clang++"),"-std=c++20","-g","-O1",
   "-fsanitize=address,undefined",str(p/"test.cpp"),"-o",str(p/"ssl")],check=True,timeout=60)
 subprocess.run([str(p/"ssl")],check=True,timeout=20,
   env={**os.environ,"ASAN_OPTIONS":"halt_on_error=1:detect_leaks=1"})
print("R23_120_SSL_PEEK_ASAN_UBSAN_PASS zero=1 short_read=1 wouldblock=1")
