#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Source-derived ownership and legal read-tail tests; no keys/game data."""
from pathlib import Path
import subprocess
import tempfile
ROOT = Path(__file__).resolve().parents[1]
path = ROOT/"encore-active/eden-core/src/core/file_sys/fssystem"
def extract(name, start, end):
    src = (path/name).read_text()
    assert src.count(start) == 1
    return start+src.split(start,1)[1].split(end,1)[0]
driver=extract("fssystem_nca_file_system_driver.cpp","Result NcaFileSystemDriver::CreateAesXtsStorage(","\nResult NcaFileSystemDriver::CreateSparseStorageMetaStorage(")
body=extract("fssystem_nca_file_system_driver.cpp","Result NcaFileSystemDriver::CreateBodySubStorage(","\nResult NcaFileSystemDriver::")
assert "offset <= body_size && size <= body_size - offset" in body
read=extract("fssystem_integrity_verification_storage.cpp","size_t IntegrityVerificationStorage::Read(","\nsize_t IntegrityVerificationStorage::GetSize(")
pre=r"""
#include <algorithm>
#include <array>
#include <cassert>
#include <cstdint>
#include <cstring>
#include <limits>
#include <memory>
#include <vector>
using u8=uint8_t; using s64=int64_t; using Result=int;
#define ASSERT(x) assert(x)
#define R_UNLESS(cond,err) do { if (!(cond)) return err; } while(0)
#define R_SUCCEED() return 0
constexpr Result ResultAllocationMemoryFailedAllocateShared=1;
namespace Common { constexpr size_t AlignUp(size_t v,size_t a){return (v+a-1)&~(a-1);} }
struct File {
    virtual ~File()=default;
    virtual size_t GetSize() const=0;
    virtual size_t Read(u8*,size_t,size_t) const=0;
};
using VirtualFile=std::shared_ptr<File>;
struct Bytes:File {
    std::vector<u8> content{1,2,3,4,5,6,7};
    size_t GetSize() const override{return content.size();}
    size_t Read(u8* out,size_t n,size_t off) const override {
        assert(off<=content.size()&&n<=content.size()-off);
        if(n) std::memcpy(out, content.data()+off,n);
        return n;
    }
};
struct NcaHeader {enum{DecryptionKey_AesXts1,DecryptionKey_AesXts2};static constexpr size_t XtsBlockSize=512;};
struct Reader {
    std::array<u8,16> unused{};
    const u8* GetDecryptionKey(int) const{return unused.data();}
};
struct AesXtsStorage:File {
    static constexpr size_t IvSize=16,KeySize=32;
    VirtualFile parent;
    static void MakeAesXtsIv(void*,size_t,s64,size_t){}
    AesXtsStorage(VirtualFile f,const void*,const void*,size_t,const void*,size_t,size_t):parent(std::move(f)){}
    size_t GetSize() const override{return parent->GetSize();}
    size_t Read(u8* out,size_t n,size_t off) const override{return parent->Read(out,n,off);}
};
template<size_t D,size_t B>struct AlignmentMatchingStorage:File {
    VirtualFile parent;
    explicit AlignmentMatchingStorage(VirtualFile p):parent(std::move(p)){}
    size_t GetSize()const override{return parent->GetSize();}
    size_t Read(u8* out,size_t n,size_t off)const override{return parent->Read(out,n,off);}
};
struct NcaFileSystemDriver {
    std::shared_ptr<Reader> m_reader=std::make_shared<Reader>();
    Result CreateAesXtsStorage(VirtualFile*,VirtualFile,s64);
};
struct IntegrityVerificationStorage {
    VirtualFile m_data_storage;
    s64 m_verification_block_size;
    size_t Read(u8*,size_t,size_t) const;
};
"""
post=r"""
int main(){
    auto bytes=std::make_shared<Bytes>();
    NcaFileSystemDriver d;
    VirtualFile storage;
    assert(d.CreateAesXtsStorage(&storage,bytes,0)==0);
    assert(storage && storage->GetSize()==7);
    assert((std::dynamic_pointer_cast<AlignmentMatchingStorage<512,1>>(storage)));
    std::array<u8,2> sample{};
    assert(storage->Read(sample.data(),2,1)==2 && sample[0]==2 && sample[1]==3);
    IntegrityVerificationStorage iv{bytes,4};
    std::array<u8,4> tail{99,99,99,99};
    assert(iv.Read(tail.data(),4,4)==3);
    assert((tail==std::array<u8,4>{5,6,7,0}));
    std::array<u8,2> partial{99,99};
    assert(iv.Read(partial.data(),1,6)==1 && partial[0]==7 && partial[1]==99);
    assert(iv.Read(partial.data(),0,7)==0);
}
"""
with tempfile.TemporaryDirectory(prefix="nca-storage-contract-") as tmp:
    p=Path(tmp)/"check.cpp"
    p.write_text(pre+driver+read+post)
    exe=Path(tmp)/"check"
    subprocess.run(["clang++","-std=c++20","-O1","-g","-fsanitize=address,undefined","-fno-sanitize-recover=all",str(p),"-o",str(exe)],check=True,timeout=90)
    subprocess.run([str(exe)],check=True,timeout=30)
print("NCA_STORAGE_CONTRACT_HOST_PASS aligned_wrapper=1 valid_tail=1 body_range_source=1 actual_crypto=unverified")
