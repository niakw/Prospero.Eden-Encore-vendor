#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Test the actual maintained VFS methods and PFS/HFS constructor, with minimal host dependencies."""
from pathlib import Path
import subprocess
import tempfile
ROOT=Path(__file__).resolve().parents[1]
active=ROOT/"encore-active/eden-core/src/core/file_sys"
original=ROOT/"sources/eden/src/core/file_sys"
def extract(path,start,end):
    s=path.read_text()
    assert s.count(start)==1
    return start+s.split(start,1)[1].split(end,1)[0]
def bodies(root):
    return extract(root/"vfs/vfs.cpp","std::vector<u8> VfsFile::ReadBytes(","\nstd::vector<u8> VfsFile::ReadAllBytes(")+extract(root/"vfs/vfs_vector.cpp","std::size_t VectorVfsFile::Read(","\nstd::size_t VectorVfsFile::Write(")+extract(root/"partition_filesystem.cpp","PartitionFilesystem::PartitionFilesystem(","\nPartitionFilesystem::~PartitionFilesystem(")
xci=(active/"card_image.cpp").read_text()
assert '#include "ncz_vfs.h"' in xci
assert xci.index("header.hfs_offset > file->GetSize()") < xci.index("file->GetSize() - header.hfs_offset")
assert xci.index("if (secure_raw == nullptr)") < xci.index("std::make_shared<NSP>(std::move(secure_raw)")
pre=r"""
#include <algorithm>
#include <array>
#include <cassert>
#include <cstdint>
#include <cstring>
#include <limits>
#include <map>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>
using u8=uint8_t; using u16=uint16_t; using u32=uint32_t; using u64=uint64_t;
namespace Common { constexpr u32 MakeMagic(char a,char b,char c,char d){
 return u32(a)|(u32(b)<<8)|(u32(c)<<16)|(u32(d)<<24);
}}
namespace Loader {enum class ResultStatus {Success,ErrorBadPFSHeader,ErrorIncorrectPFSFileSize};}
struct VfsFile {
 virtual ~VfsFile()=default;
 virtual size_t GetSize() const=0;
 virtual size_t Read(u8*,size_t,size_t) const=0;
 std::vector<u8> ReadBytes(size_t size,size_t offset=0) const;
 template<class T> size_t ReadObject(T* t) const{return Read(reinterpret_cast<u8*>(t),sizeof(T),0);}
};
using VirtualFile=std::shared_ptr<VfsFile>;
struct VectorVfsFile:VfsFile{
 std::vector<u8> data;
 explicit VectorVfsFile(std::vector<u8> d):data(std::move(d)){}
 size_t GetSize() const override{return data.size();}
 size_t Read(u8*,size_t,size_t) const override;
};
struct OffsetVfsFile:VfsFile{
 VirtualFile file; size_t size,off; std::string name;
 OffsetVfsFile(VirtualFile f,size_t n,size_t o,std::string s): file(f),size(n),off(o),name(std::move(s)){}
 size_t GetSize() const override{return size;}
 size_t Read(u8*,size_t,size_t) const override{return 0;}
};
struct PartitionFilesystem {
 struct Header {u32 magic,num_entries,strtab_size,reserved; bool HasValidMagicValue() const{
  return magic==Common::MakeMagic('P','F','S','0')||magic==Common::MakeMagic('H','F','S','0');}};
 #pragma pack(push,1)
 struct FSEntry{u64 offset,size;u32 strtab_offset;};
 struct PFSEntry{FSEntry fs_entry;u32 padding;};
 struct HFSEntry{FSEntry fs_entry;u32 hash_region_size;u64 padding;std::array<char,32> hash;};
 #pragma pack(pop)
 static_assert(sizeof(Header)==16 && sizeof(FSEntry)==20 && sizeof(PFSEntry)==24 && sizeof(HFSEntry)==64);
 Loader::ResultStatus status{};
 Header pfs_header{};
 bool is_hfs=false; size_t content_offset=0;
 std::map<std::string,u64> offsets,sizes;
 std::vector<VirtualFile> pfs_files;
 explicit PartitionFilesystem(VirtualFile file);
};
"""
post=r"""
std::shared_ptr<VectorVfsFile> image(u32 count,u32 strtab,u32 bad_name=0,bool hfs=false){
 using P=PartitionFilesystem;
 P::Header header{hfs?Common::MakeMagic('H','F','S','0'):Common::MakeMagic('P','F','S','0'),count,strtab,0};
 const size_t stride=hfs?64:24;
 std::vector<u8> raw(16+size_t(count)*stride+strtab);
 memcpy(raw.data(),&header,16);
 for (u32 i=0;i<count;++i){P::FSEntry e{0,0,i==count-1?bad_name:0};
  memcpy(raw.data()+16+i*stride,&e,sizeof(e));}
 if(strtab) raw[16+size_t(count)*stride]='x';
 return std::make_shared<VectorVfsFile>(std::move(raw));
}
int main(int argc,char** argv){
 using S=Loader::ResultStatus;
 if(argc>1){ auto v=image(1,2,9999); PartitionFilesystem p(v); return 0; }
 auto v=std::make_shared<VectorVfsFile>(std::vector<u8>{1,2,3,4});
 assert(v->ReadBytes(std::numeric_limits<size_t>::max(),2)==std::vector<u8>({3,4}));
 assert(v->ReadBytes(10,4).empty()&&v->ReadBytes(10,999).empty()&&v->ReadBytes(0,0).empty());
 u8 x=42; assert(v->Read(&x,1,5)==0&&x==42); assert(v->Read(nullptr,0,0)==0);
 auto shortfile=image(1,2); shortfile->data.resize(16);
 assert(PartitionFilesystem(shortfile).status==S::ErrorIncorrectPFSFileSize);
 for(bool hfs:{false,true}){
  PartitionFilesystem valid(image(1,2,0,hfs)); assert(valid.status==S::Success&&valid.pfs_files.size()==1&&valid.offsets.contains("x"));
  PartitionFilesystem bad(image(2,2,100000,hfs)); assert(bad.status==S::ErrorBadPFSHeader&&bad.pfs_files.empty()&&bad.offsets.empty()&&bad.sizes.empty());
  PartitionFilesystem bounded(image(1,1,0,hfs)); assert(bounded.status==S::Success&&bounded.offsets.contains("x"));
 }
 PartitionFilesystem many(image(65536,2)); assert(many.status==S::Success&&many.pfs_files.size()==65536);
}
"""
with tempfile.TemporaryDirectory(prefix="vfs-pfs-bounds-") as tmp:
    work=Path(tmp)
    for label,root in (("original",original),("fixed",active)):
        src=work/(label+".cpp");src.write_text(pre+bodies(root)+post)
        exe=work/label
        subprocess.run(["clang++","-std=c++20","-O1","-g","-fsanitize=address,undefined","-fno-sanitize-recover=all",str(src),"-o",str(exe)],check=True,timeout=90)
        r=subprocess.run([str(exe)]+(["negative"] if label=="original" else []),capture_output=True,text=True,timeout=30)
        if label=="original":
            assert r.returncode!=0 and "AddressSanitizer" in r.stderr,r.stderr
        else:
            assert r.returncode==0,r.stderr
print("VFS_PFS_HFS_BOUNDS_HOST_PASS original_asan=1 bounded_read=1 offset=1 partial_cleanup=1 pfs=1 hfs=1 entries65536=1 xci_source_only=1")
