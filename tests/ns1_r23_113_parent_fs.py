#!/usr/bin/env python3
"""R23 #113 bundled program source-owner coherence and actual parent resolver mock."""
from pathlib import Path
import hashlib
import os
import subprocess
import tempfile

ROOT=Path(__file__).resolve().parents[1]
ROOT_SRC=ROOT/"encore-active/eden-core"
known={
 "src/core/file_sys/patch_manager.cpp":"e3dea103363ce2212d88f700db787f9ab17188b9",
 "src/core/file_sys/registered_cache.cpp":"2658884ef0332ec7dc579824d9c89925f9168891",
 "src/core/file_sys/savedata_factory.cpp":"825df37c9134b4f2c9ffa80a0ed0e47b8784bb16",
}
def blob(p):
 d=p.read_bytes()
 return hashlib.sha1(b"blob "+str(len(d)).encode()+bytes([0])+d).hexdigest()
for file,expected in known.items():
 assert blob(ROOT_SRC/file)==expected,(file,blob(ROOT_SRC/file))
for name in ("patch_manager","registered_cache","savedata_factory"):
 header=ROOT_SRC/("include/core/file_sys/"+name+".h")
 assert header.is_file(),header
assert "parent_title_id" in (ROOT_SRC/"include/core/file_sys/patch_manager.h").read_text()
assert "GetParentApplicationId(u64 program_id) const" in (ROOT_SRC/"include/core/file_sys/registered_cache.h").read_text()
assert "GetSaveDataPath(" in (ROOT_SRC/"include/core/file_sys/savedata_factory.h").read_text()
patch=(ROOT_SRC/"src/core/file_sys/patch_manager.cpp").read_text()
assert patch.count("GetModificationLoadRoot();")>=6
assert "if (load_dir != nullptr) patch_dirs = load_dir->GetSubdirectories();" in patch
assert "if (sdmc_load_dir != nullptr &&" in patch
assert "const auto disabled = GetDisabledAddons();" in patch
assert "GetMetadataFromBaseOrUpdate" in patch
print("R23_113_COMPLETE_MAINTAINED_FS_SOURCE_OWNER_PASS six_files=6")

code=(ROOT_SRC/"src/core/file_sys/registered_cache.cpp").read_text()
start="std::optional<u64> ContentProvider::GetParentApplicationId(u64 program_id) const {"
end="\nPlaceholderCache::PlaceholderCache("
assert code.count(start)==1
method=start+code.split(start,1)[1].split(end,1)[0]
fixture=r"""
#include <cstdint>
#include <limits>
#include <optional>
#include <vector>
#include <set>
#include <cassert>
using u8=std::uint8_t;
using u64=std::uint64_t;
enum class TitleType{Application,Update};
enum class ContentRecordType{Meta,Program};
constexpr u64 BASE=0x0100000000010000ULL;
u64 GetBaseTitleID(u64 id) {return id & ~u64{255};}
u64 GetUpdateTitleID(u64 id) {return id + u64{2048};}
struct ContentProvider {
 std::set<u64> app_meta,update_meta,programs;
 std::vector<int> ListEntriesFilter(TitleType kind,ContentRecordType record,u64 id) const {
  if(record!=ContentRecordType::Meta) return {};
  bool found=(kind==TitleType::Application?app_meta:update_meta).contains(id);
  return found ? std::vector<int>{1} : std::vector<int>{};
 }
 bool HasEntry(u64 id,ContentRecordType kind) const {
  return kind==ContentRecordType::Program && programs.contains(id);
 }
 std::optional<u64> GetParentApplicationId(u64) const;
};
""" + method + r"""
int main() {
 ContentProvider p;
 assert(!p.GetParentApplicationId(BASE));
 assert(!p.GetParentApplicationId(BASE+1));
 p.app_meta.insert(BASE);
 p.programs.insert(BASE+1);
 assert(p.GetParentApplicationId(BASE+1)==BASE);
 p.app_meta.insert(BASE+1);
 assert(!p.GetParentApplicationId(BASE+1));
 p.app_meta.erase(BASE+1);
 assert(!p.GetParentApplicationId(BASE+256));
 p.app_meta.clear();p.programs.clear();
 p.update_meta.insert(GetUpdateTitleID(BASE));
 p.programs.insert(GetUpdateTitleID(BASE+2));
 assert(p.GetParentApplicationId(BASE+2)==BASE);
 assert(!p.GetParentApplicationId(BASE+3));
}
""";
with tempfile.TemporaryDirectory(prefix="r23-fs-parent-") as temp:
 p=Path(temp)
 (p/"test.cpp").write_text(fixture)
 subprocess.run([os.environ.get("CXX","clang++"),"-std=c++20","-O1","-g",
                 "-fsanitize=address,undefined","-fno-omit-frame-pointer",
                 str(p/"test.cpp"),"-o",str(p/"test")],check=True,timeout=60)
 subprocess.run([str(p/"test")],check=True,timeout=20,
                env={**os.environ,"ASAN_OPTIONS":"detect_leaks=1:halt_on_error=1"})
print("R23_113_PARENT_RESOLUTION_ASAN_UBSAN_PASS base=1 update=1 own_meta=1 no_parent=1")
