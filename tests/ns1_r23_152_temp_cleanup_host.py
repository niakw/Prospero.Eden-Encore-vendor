#!/usr/bin/env python3
"""R23 #152: actual maintained temp-cleanup callsite and two-program fixture."""
from pathlib import Path
import hashlib
import os
import subprocess
import tempfile

ROOT=Path(__file__).resolve().parents[1]/"encore-active/eden-core/src"
factory=ROOT/"core/file_sys/savedata_factory.cpp"
controller=ROOT/"core/hle/service/filesystem/filesystem.cpp"
f=factory.read_text()
c=controller.read_text()
old_factory='    // Delete all temporary storages\n    // On hardware, it is expected that temporary storage be empty at first use.\n    dir->DeleteSubdirectoryRecursive("temp");'
new_factory='    // Temporary save storage is prepared once during application filesystem\n    // initialization, not on every (possibly bundled) program registration.'
old_controller='    const auto rw_mode = FileSys::OpenMode::ReadWrite;\n\n    auto nand_directory ='
new_controller='''    const auto rw_mode = FileSys::OpenMode::ReadWrite;

    // Switch temporary save storage starts empty for an application, but
    // multiple bundled programs share the same storage lifetime. Preparing
    // it in every SaveDataFactory ctor deletes a sibling program's live data.
    // CreateFactories is initialized once for the application controller;
    // overwrite explicitly starts a new generation and resets bis_factory.
    if (bis_factory == nullptr) {
        const auto save_directory =
            vfs.OpenDirectory(Common::FS::GetEdenPathString(EdenPath::SaveDir), rw_mode);
        if (save_directory != nullptr) {
            save_directory->DeleteSubdirectoryRecursive("temp");
        }
    }

    auto nand_directory ='''
def sha(s):
    data=s.encode()
    return hashlib.sha1(b"blob "+str(len(data)).encode()+bytes([0])+data).hexdigest()
assert f.count(new_factory)==1
assert 'dir->DeleteSubdirectoryRecursive("temp");' not in f
assert "GetSaveDataPath(" in f and "GetParentApplicationId" in f, "lost parent-program save mapping"
assert c.count(new_controller)==1
assert sha(c.replace(new_controller,old_controller,1))=="6e82315d25103e40859321ac9d785aaa3ba4cfa2"
start=new_controller.index('    if (bis_factory == nullptr) {')
end=new_controller.index('\n\n    auto nand_directory =',start)
method=new_controller[start:end]
test=r"""
#include <cassert>
#include <memory>
#include <string>
namespace FileSys {enum class OpenMode {ReadWrite};}
namespace Common::FS {
enum class EdenPath {SaveDir};
std::string GetEdenPathString(EdenPath) {return "save";}
}
struct Dir {
 int clears=0;
 bool DeleteSubdirectoryRecursive(const char* name) {
  assert(std::string(name)=="temp");
  ++clears; return true;
 }
};
struct VFS {
 std::shared_ptr<Dir> saved{std::make_shared<Dir>()};
 std::shared_ptr<Dir> OpenDirectory(const std::string& name,FileSys::OpenMode) {
  assert(name=="save"); return saved;
 }
};
struct Controller {
 std::unique_ptr<int> bis_factory;
 void Init(VFS& vfs) {
  const auto rw_mode = FileSys::OpenMode::ReadWrite;
""" + method + r"""
  if(!bis_factory) bis_factory=std::make_unique<int>(1);
 }
 void NewApplication() {bis_factory.reset();}
};
int main() {
 VFS vfs;
 Controller c;
 c.Init(vfs);
 assert(vfs.saved->clears==1);
 c.Init(vfs); // second bundled program cannot delete first's temporary data
 assert(vfs.saved->clears==1);
 c.NewApplication();
 c.Init(vfs);
 assert(vfs.saved->clears==2);
 c.NewApplication();
 vfs.saved.reset();
 c.Init(vfs); // no temp path cannot cause null dereference
}
"""
with tempfile.TemporaryDirectory(prefix="r23-temp-") as temp:
 p=Path(temp)
 (p/"test.cpp").write_text(test)
 subprocess.run([os.getenv("CXX","clang++"),"-std=c++20","-g","-O1",
                 "-fsanitize=address,undefined",str(p/"test.cpp"),"-o",str(p/"test")],
                check=True,timeout=60)
 subprocess.run([str(p/"test")],check=True,timeout=25,
                env={**os.environ,"ASAN_OPTIONS":"detect_leaks=1:halt_on_error=1"})
print("R23_152_TEMP_ONCE_PER_APPLICATION_ASAN_UBSAN_PASS bunded_program=2 reset=1")
