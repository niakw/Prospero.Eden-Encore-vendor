#!/usr/bin/env python3
"""C01 original-source provenance + source-extracted RomFS teardown stress.

Only a host structural test; full maintained native TU and PS5 FS test pending.
"""
from pathlib import Path
import hashlib
import os
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
PATH = ROOT / "encore-active/eden-core/src/core/file_sys/fsmitm_romfsbuild.cpp"
source = PATH.read_text()
before = "RomFSBuildContext::~RomFSBuildContext() = default;"
after = """// Cut sibling/child shared-owner chains before vectors release the nodes.
// Parents are non-owning; every child remains owned by the builder vectors.
// Destruction is O(nodes) without recursive shared_ptr teardown stack growth.
RomFSBuildContext::~RomFSBuildContext() {
    for (auto& file : files) {
        file->sibling.reset();
    }
    for (auto& dir : directories) {
        dir->child.reset();
        dir->sibling.reset();
        dir->file.reset();
    }
}"""
assert source.count(after) == 1
recovered = source.replace(after, before, 1)
payload = recovered.encode()
git_blob = hashlib.sha1(b"blob " + str(len(payload)).encode() + bytes([0]) + payload).hexdigest()
assert git_blob == "94cbb31b48054a62d88446a3920ecef734861b18", git_blob
assert before not in source
print("C01_FULL_MAINTAINED_SOURCE_PARITY_PASS")

# Test the exact authored destructor from the complete C++ source, against
# representative shared-owned sibling/child/file edges on a small stack.
destructor = after.split("RomFSBuildContext::~RomFSBuildContext()", 1)[1]
destructor = "RomFSBuildContext::~RomFSBuildContext()" + destructor
fixture = r"""
#include <memory>
#include <vector>
#include <cassert>
struct File {
    std::shared_ptr<File> sibling;
};
struct Dir {
    std::shared_ptr<Dir> child, sibling;
    std::shared_ptr<File> file;
};
struct RomFSBuildContext {
    std::vector<std::shared_ptr<File>> files;
    std::vector<std::shared_ptr<Dir>> directories;
    ~RomFSBuildContext();
};
""" + destructor + r"""
int main() {
    constexpr int N = 32768;
    auto ctx = std::make_unique<RomFSBuildContext>();
    for (int i=0;i<N;i++) {
        ctx->directories.emplace_back(std::make_shared<Dir>());
        ctx->files.emplace_back(std::make_shared<File>());
    }
    for (int i=0;i<N-1;i++) {
        ctx->directories[i]->sibling = ctx->directories[i+1];
        ctx->files[i]->sibling = ctx->files[i+1];
        ctx->directories[i]->child = ctx->directories[i+1];
    }
    for (int i=0;i<N;i++)
        ctx->directories[i]->file = ctx->files[i];
    ctx.reset();
    return 0;
}
"""
with tempfile.TemporaryDirectory(prefix="c01-romfs-") as tmp:
    cpp = Path(tmp) / "romfs.cpp"
    exe = Path(tmp) / "romfs"
    cpp.write_text(fixture)
    compiler = os.getenv("CXX", "clang++")
    subprocess.run([compiler, "-std=c++20", "-O1", "-g", "-fsanitize=address,undefined",
                    "-fno-omit-frame-pointer", str(cpp), "-o", str(exe)],
                   check=True, timeout=60)
    subprocess.run([str(exe)], check=True, timeout=45,
                   env={**os.environ, "ASAN_OPTIONS": "halt_on_error=1:detect_leaks=1"})
print("C01_ROMFS_EXTRACTED_HOST_SANITIZER_PASS long_sibling_chains=32768 native_ps5_unproven=1")
