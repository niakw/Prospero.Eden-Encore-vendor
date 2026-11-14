#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Positive synthetic RomFS tree/read tests on the exact maintained traversal."""
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
path = ROOT / "encore-active/eden-core/src/core/file_sys/romfs.cpp"
source = path.read_text()
start = source.index("namespace FileSys {")
end = source.index("\nVirtualFile CreateRomFS(", start)
code = source[start:end] + "\n} // namespace FileSys\n"
pre = r"""
#include <algorithm>
#include <array>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>
using u8=uint8_t; using u32=uint32_t; using u64=uint64_t;
using u32_le=u32; using u64_le=u64;
#define LOG_ERROR(...) ((void)0)
namespace FileSys {
struct File;
struct VectorVfsDirectory;
using VirtualFile=std::shared_ptr<File>;
using VirtualDir=std::shared_ptr<VectorVfsDirectory>;
struct File {
    virtual ~File()=default;
    virtual size_t GetSize() const=0;
    virtual std::vector<u8> ReadBytes(size_t,size_t) const=0;
    virtual std::string GetName() const {return {};}
    template<class T> size_t ReadObject(T* p) const {
        auto b=ReadBytes(sizeof(T),0);
        if (b.size()==sizeof(T)) std::memcpy(p,b.data(),sizeof(T));
        return b.size();
    }
};
struct Bytes final:File {
    std::vector<u8> bytes;
    size_t GetSize()const override{return bytes.size();}
    std::vector<u8> ReadBytes(size_t n,size_t o)const override {
        if(o>=bytes.size()) return {};
        n=std::min(n,bytes.size()-o);
        return {bytes.begin()+o,bytes.begin()+o+n};
    }
};
struct OffsetVfsFile final:File {
    VirtualFile owner; size_t length,offset; std::string name;
    OffsetVfsFile(VirtualFile f,size_t n,size_t o,std::string s)
        :owner(std::move(f)),length(n),offset(o),name(std::move(s)){}
    size_t GetSize()const override{return length;}
    std::string GetName()const override{return name;}
    std::vector<u8> ReadBytes(size_t n,size_t o)const override{
        if(o>=length)return {};
        return owner->ReadBytes(std::min(n,length-o),offset+o);
    }
};
struct VectorVfsDirectory {
    std::vector<VirtualFile> files; std::vector<VirtualDir> dirs; std::string name;
    VectorVfsDirectory(std::vector<VirtualFile> f={},std::vector<VirtualDir> d={},std::string n={})
        :files(std::move(f)),dirs(std::move(d)),name(std::move(n)){}
    void AddFile(VirtualFile f){files.push_back(std::move(f));}
    void AddDirectory(VirtualDir d){dirs.push_back(std::move(d));}
    VirtualDir GetSubdirectory(const std::string& n)const {
        for (const auto& d:dirs)if(d->name==n)return d;
        return {};
    }
};
} // namespace FileSys
"""
post = r"""
using namespace FileSys;
template<class T> void put(std::vector<u8>& bytes,size_t offset,const T& value) {
    assert(offset<=bytes.size() && sizeof(T)<=bytes.size()-offset);
    std::memcpy(bytes.data()+offset,&value,sizeof(T));
}
int main(){
    constexpr u32 none=ROMFS_ENTRY_EMPTY;
    auto empty=std::make_shared<Bytes>();
    empty->bytes.resize(sizeof(RomFSHeader)+sizeof(DirectoryEntry));
    RomFSHeader eh{};
    eh.header_size=sizeof(eh);
    eh.directory_meta={sizeof(eh),sizeof(DirectoryEntry)};
    eh.file_meta={empty->bytes.size(),0};
    eh.data_offset=empty->bytes.size();
    put(empty->bytes,0,eh);
    DirectoryEntry er{none,none,none,none,none,0};
    put(empty->bytes,eh.directory_meta.offset,er);
    auto root=ExtractRomFS(empty);
    assert(root && root->dirs.empty() && root->files.empty());

    auto nested=std::make_shared<Bytes>();
    constexpr size_t ds=sizeof(DirectoryEntry)*2+4;
    constexpr size_t fs=sizeof(FileEntry)+4;
    RomFSHeader h{};
    h.header_size=sizeof(h); h.directory_meta={sizeof(h),ds};
    h.file_meta={sizeof(h)+ds,fs};
    h.data_offset=sizeof(h)+ds+fs;
    nested->bytes.resize(h.data_offset+3);
    put(nested->bytes,0,h);
    DirectoryEntry dr{none,none,sizeof(DirectoryEntry),none,none,0};
    DirectoryEntry dc{0,none,none,0,none,4};
    put(nested->bytes,h.directory_meta.offset,dr);
    put(nested->bytes,h.directory_meta.offset+sizeof(dr),dc);
    std::memcpy(nested->bytes.data()+h.directory_meta.offset+2*sizeof(dr),"data",4);
    FileEntry fe{sizeof(DirectoryEntry),none,0,3,none,4};
    put(nested->bytes,h.file_meta.offset,fe);
    std::memcpy(nested->bytes.data()+h.file_meta.offset+sizeof(fe),"test",4);
    std::memcpy(nested->bytes.data()+h.data_offset,"abc",3);
    root=ExtractRomFS(nested);
    assert(root && root->dirs.size()==1 && root->files.empty());
    auto sub=root->GetSubdirectory("data");
    assert(sub && sub->files.size()==1 && sub->files[0]->GetName()=="test");
    assert((sub->files[0]->ReadBytes(3,0)==std::vector<u8>{'a','b','c'}));
    root.reset();
    assert((sub->files[0]->ReadBytes(2,1)==std::vector<u8>{'b','c'}));
    auto short_header=std::make_shared<Bytes>();
    short_header->bytes.resize(4);
    assert(!ExtractRomFS(short_header));
}
"""
with tempfile.TemporaryDirectory(prefix="romfs-owner-") as tmp:
    p = Path(tmp) / "test.cpp"
    p.write_text(pre + code + post)
    out = Path(tmp) / "test"
    subprocess.run(["clang++","-std=c++20","-O1","-g","-fsanitize=address,undefined",
                    "-fno-sanitize-recover=all",str(p),"-o",str(out)],check=True,timeout=90)
    subprocess.run([str(out)],check=True,timeout=30)
print("ROMFS_TREE_CONTRACT_HOST_PASS empty=1 nested=1 data_read=1 owner_retained=1 short_header=1 real_vfs=unverified")
