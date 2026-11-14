#!/usr/bin/env python3
"""Check the exact native-only invalid-IR definition and fatal behavior witness."""
from pathlib import Path
import hashlib
import shutil
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
active = root / "encore-active/eden-core/src/dynarmic/backend/x64"
pinned = root / "sources/eden/src/dynarmic/src/dynarmic/backend/x64/emit_x64.cpp"
assert_header = root / "sources/eden/src/common/assert.h"
def blob(path):
    data = path.read_bytes()
    return hashlib.sha1(b"blob " + str(len(data)).encode() + b"\0" + data).hexdigest()

assert blob(pinned) == "b9df3987ca5cc4ea960142f186848202c45dd666"
assert blob(active / "emit_x64_host.cpp") == "b2ec3288f0b4d939422ff70d5a3716d722e6b842"
assert blob(active / "emit_x64_native.cpp") == "46dfa1e0da81d15d5e6a0e5b6020dbf51c84b4ef"
assert blob(assert_header) == "298af3071a9976a97ae52a4a0d3dabd7b9dd5935"
before = "void EmitX64::EmitInvalid(EmitContext&, IR::Inst* inst) {\n    UNREACHABLE();\n}"
after = "void EmitX64::EmitInvalid(EmitContext&, IR::Inst*) {\n    UNREACHABLE();\n}"
native = (active / "emit_x64_native.cpp").read_text()
assert native.count(after) == 1 and before not in native
old_native = native.replace(after, before, 1).encode()
assert hashlib.sha1(b"blob " + str(len(old_native)).encode() + b"\0" + old_native).hexdigest() == "205aa034f0ec2330aec5a81a628ce3ae326856ab"
assert before in pinned.read_text()
assert before in (active / "emit_x64_host.cpp").read_text()
header = assert_header.read_text()
assert '#define UNREACHABLE() UNREACHABLE_MSG("")' in header
assert "[[noreturn]] void AssertFatalImpl();" in header
assert "AssertFatalImpl();" in header.split("#define UNREACHABLE_MSG", 1)[1].split("#define UNREACHABLE()", 1)[0]

# Compile the exact definition against its unchanged declaration and a fatal
# stand-in. This tests the signature and call, not the entire Dynarmic TU.
fixture = """#include <cstdlib>
namespace IR { struct Inst {}; }
struct EmitContext {};
struct EmitX64 { void EmitInvalid(EmitContext&, IR::Inst* inst); };
[[noreturn]] void AssertFatalImpl() { std::exit(42); }
#define UNREACHABLE() AssertFatalImpl()
""" + after + """
int main() { EmitX64 emitter; EmitContext ctx; IR::Inst inst;
    emitter.EmitInvalid(ctx, &inst); return 0; }
"""
compilers = [c for c in (shutil.which("clang++"), shutil.which("g++")) if c]
assert compilers
with tempfile.TemporaryDirectory(prefix="encore-native-invalid-ir-") as directory:
    tmp = Path(directory)
    src = tmp / "test.cpp"
    src.write_text(fixture)
    for index, compiler in enumerate(compilers):
        for mode in ("debug", "release"):
            out = tmp / (str(index) + mode)
            flags = ["-O2", "-DNDEBUG"] if mode == "release" else ["-O0"]
            subprocess.run([compiler, "-std=c++20", "-Wall", "-Wextra",
                            "-Werror=unused-parameter", *flags,
                            str(src), "-o", str(out)], check=True, timeout=40)
            result = subprocess.run([str(out)], timeout=10)
            assert result.returncode == 42
print("NATIVE_EMIT_INVALID_WARNING_HOST_PASS exact_native_delta=1 fatal_call=1 host_unchanged=1")
