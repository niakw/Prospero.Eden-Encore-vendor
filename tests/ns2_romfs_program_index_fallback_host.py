#!/usr/bin/env python3
"""Compile the selected RomFSFactory method: upstream Lichii #159 / PS5 NUP cases."""
from pathlib import Path
import shutil
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
path = ROOT / "encore-active/eden-core/src/core/file_sys/romfs_factory.cpp"
source = path.read_text()
marker = "VirtualFile RomFSFactory::OpenPatchedRomFSWithProgramIndex("
start = source.index(marker)
end = source.index("\n}\n", start) + 2
method = source[start:end]
assert "program_index == 0 && type == ContentRecordType::Program" in method
assert "!updatable" in method
assert "return file;" in method
assert method.index("OpenPatchedRomFS(res_title_id, type)") < method.index("return file;")
cpp = r"""
#include <cassert>
#include <cstdint>
#include <memory>
using u8 = std::uint8_t;
using u64 = std::uint64_t;
struct File { int id; };
using VirtualFile = std::shared_ptr<File>;
enum class ContentRecordType { Program, Data };
static u64 GetBaseTitleIDWithProgramIndex(u64 id, u8 index) { return id + index; }
class RomFSFactory {
public:
    VirtualFile file;
    VirtualFile provider;
    bool updatable = false;
    VirtualFile OpenPatchedRomFS(u64, ContentRecordType) const { return provider; }
    VirtualFile OpenPatchedRomFSWithProgramIndex(u64, u8, ContentRecordType) const;
};
""" + method + r"""
int main() {
    RomFSFactory f;
    f.file = std::make_shared<File>(File{1});
    f.provider = std::make_shared<File>(File{2});
    assert(f.OpenPatchedRomFSWithProgramIndex(0x040074a01bf12000ULL,0,ContentRecordType::Program)->id==2);
    f.provider.reset();
    assert(f.OpenPatchedRomFSWithProgramIndex(0x040074a01bf12000ULL,0,ContentRecordType::Program)->id==1);
    assert(!f.OpenPatchedRomFSWithProgramIndex(0x040074a01bf12000ULL,1,ContentRecordType::Program));
    assert(!f.OpenPatchedRomFSWithProgramIndex(0x040074a01bf12000ULL,0,ContentRecordType::Data));
    f.updatable=true;
    assert(!f.OpenPatchedRomFSWithProgramIndex(0x040074a01bf12000ULL,0,ContentRecordType::Program));
    f.updatable=false;
    f.file.reset();
    assert(!f.OpenPatchedRomFSWithProgramIndex(0x040074a01bf12000ULL,0,ContentRecordType::Program));
}
"""
compilers = tuple(dict.fromkeys(filter(None, [shutil.which("clang++"),shutil.which("g++")])))
assert compilers, "host C++ compiler required"
with tempfile.TemporaryDirectory(prefix="romfs-program-index-") as td:
    source_path = Path(td) / "check.cpp"
    source_path.write_text(cpp)
    for compiler in compilers:
        for optimize in ("-O0", "-O2"):
            binary = Path(td) / ("check-" + str(len(compiler)) + optimize)
            subprocess.run([compiler, "-std=c++20", optimize, "-Wall", "-Wextra",
                            "-Werror", "-fsanitize=undefined", "-fno-sanitize-recover=all",
                            str(source_path), "-o", str(binary)],
                           check=True, timeout=45)
            subprocess.run([str(binary)], check=True, timeout=15)
print("NS2_ROMFS_PROGRAM_INDEX_0_FALLBACK_PASS source=selected code runtime=host PS5=unverified")
