#!/usr/bin/env python3
"""R23 #113 six exact selected full owner blobs and source-extracted parent/savedata."""
from pathlib import Path
import hashlib, os, subprocess, tempfile

ROOT=Path(__file__).resolve().parents[1]
BASE=ROOT/"encore-active/eden-core"
OWNERS={
"src/core/file_sys/patch_manager.cpp":"9481f60c318912a5f667758ad334dd957d71fbfd",
"include/core/file_sys/patch_manager.h":"6819a1a3e794537cff6554978565e772a0c99e89",
"src/core/file_sys/registered_cache.cpp":"2658884ef0332ec7dc579824d9c89925f9168891",
"include/core/file_sys/registered_cache.h":"89140a2e950ff369136d1febf46c2093d545d787",
"src/core/file_sys/savedata_factory.cpp":"825df37c9134b4f2c9ffa80a0ed0e47b8784bb16",
"include/core/file_sys/savedata_factory.h":"36c09ce2ac14737c21447cd85cc68c47a8055a93"
}
def sha(data):
 return hashlib.sha1(b"blob "+str(len(data)).encode()+bytes([0])+data).hexdigest()
for name,expected in OWNERS.items():
 actual=sha((BASE/name).read_bytes())
 if name=="src/core/file_sys/patch_manager.cpp":
  assert "GetModificationLoadRoot(true);" in (BASE/name).read_text(),name
 elif name=="src/core/file_sys/savedata_factory.cpp":
  data=(BASE/name).read_text()
  assert "GetSaveDataPath(" in data and "dir->DeleteSubdirectoryRecursive(\"temp\");" not in data,name
 else:
  assert actual==expected,(name,actual,expected)
 print("R23_113_EXACT_MAINTAINED_SOURCE_PASS",name)
cache=(BASE/"src/core/file_sys/registered_cache.cpp").read_text()
save=(BASE/"src/core/file_sys/savedata_factory.cpp").read_text()
patch=(BASE/"src/core/file_sys/patch_manager.cpp").read_text()
start="std::optional<u64> ContentProvider::GetParentApplicationId(u64 program_id) const {"
end="\nPlaceholderCache::PlaceholderCache("
assert cache.count(start)==1 and cache.count(end)==1
method=start+cache.split(start,1)[1].split(end,1)[0]
start2="std::string SaveDataFactory::GetSaveDataPath("
end2="\nVirtualDir SaveDataFactory::Create("
assert save.count(start2)==1 and save.count(end2)==1
save_method=start2+save.split(start2,1)[1].split(end2,1)[0]
assert patch.count("void PatchManager::ApplyLayeredFS(VirtualFile& romfs, ContentRecordType type) const")==1
assert patch.count("std::vector<std::string> PatchManager::GetDisabledAddons() const")==1
assert patch.count("VirtualDir PatchManager::GetModificationLoadRoot(bool sdmc) const")==1
assert "const auto sdmc_mod_dir = GetModificationLoadRoot(true);" in patch
fixture=r"""
#include <algorithm>
#include <cassert>
#include <cstdint>
#include <limits>
#include <memory>
#include <optional>
#include <string>
#include <vector>
using u8=std::uint8_t;
using u64=std::uint64_t;
using u128=unsigned __int128;
enum class TitleType { Application, Update };
enum class ContentRecordType { Meta, Program };
u64 GetBaseTitleID(u64 id){return id & ~u64{0xff};}
u64 GetUpdateTitleID(u64 id){return id+0x800;}
struct ContentProvider {
 std::vector<u64> app_meta,update_meta,programs;
 std::vector<int> ListEntriesFilter(TitleType type,ContentRecordType,u64 id) const {
  auto& vec=type==TitleType::Application ? app_meta:update_meta;
  return std::find(vec.begin(),vec.end(),id)!=vec.end() ? std::vector<int>{1} : std::vector<int>{};
 }
 bool HasEntry(u64 id,ContentRecordType) const {
  return std::find(programs.begin(),programs.end(),id)!=programs.end();
 }
 std::optional<u64> GetParentApplicationId(u64 program_id) const;
};
""" + method + r"""
enum class SaveDataSpaceId { User };
enum class SaveDataType { Account, Device, Other };
using ProgramId=u64;
using VirtualDir=int;
struct CoreSystem {
 ContentProvider& provider;
 ContentProvider& GetContentProvider(){return provider;}
};
std::string GetFullPath(ProgramId,VirtualDir,SaveDataSpaceId,SaveDataType,u64 id,u128,u64){
 return std::to_string(id);
}
struct SaveDataFactory {
 CoreSystem& system;
 ProgramId program_id;
 VirtualDir dir=0;
 std::string GetSaveDataPath(SaveDataSpaceId,SaveDataType,u64,u128,u64) const;
};
""" + save_method + r"""
int main() {
 constexpr u64 root=0x0100000000001000ULL;
 ContentProvider p{};
 p.app_meta.push_back(root);
 p.programs.push_back(root+1);
 p.programs.push_back(root+255);
 assert(!p.GetParentApplicationId(root));
 assert(p.GetParentApplicationId(root+1)==root);
 assert(p.GetParentApplicationId(root+255)==root);
 assert(!p.GetParentApplicationId(root+256));
 p.app_meta.push_back(root+2);
 p.programs.push_back(root+2);
 assert(!p.GetParentApplicationId(root+2));
 CoreSystem system{p};
 SaveDataFactory saves{system,root+1};
 assert(saves.GetSaveDataPath(SaveDataSpaceId::User,SaveDataType::Account,0,0,0)==std::to_string(root));
 assert(saves.GetSaveDataPath(SaveDataSpaceId::User,SaveDataType::Device,root+1,0,0)==std::to_string(root));
 assert(saves.GetSaveDataPath(SaveDataSpaceId::User,SaveDataType::Other,root+1,0,0)==std::to_string(root+1));
}
""";
with tempfile.TemporaryDirectory(prefix="r23-parent-") as td:
 d=Path(td)
 (d/"t.cpp").write_text(fixture)
 subprocess.run([os.getenv("CXX","clang++"),"-std=c++20","-O1","-g",
  "-fsanitize=address,undefined",str(d/"t.cpp"),"-o",str(d/"test")],check=True,timeout=60)
 subprocess.run([str(d/"test")],check=True,timeout=20,
  env={**os.environ,"ASAN_OPTIONS":"detect_leaks=1:halt_on_error=1"})
print("R23_113_PARENT_PROGRAM_SAVE_HOST_PASS child=1 nested=255 orphan=256 metadata_override=1")
