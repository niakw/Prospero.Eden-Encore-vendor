#!/usr/bin/env python3
"""PS5 VI2 producer-fence bounds: source ownership and actual u32 ABI host fixture.

Legacy filename is preserved for the existing CI reference. Fence::num_fences
is unsigned in the pinned Eden header, contrary to the previous test's premise.
"""
from pathlib import Path
import shutil
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
supervisor = (root / "encore-active/ns2-runtime/graphics_supervisor"
              "/core/hle/service/am/service/graphics_supervisor.cpp").read_text()
header = (root / "sources/eden/src/core/hle/service/nvnflinger/ui/fence.h").read_text()
assert "u32 num_fences{};" in header
assert "std::array<Service::Nvidia::NvFence, 4> fences{};" in header
assert "static_assert(sizeof(Fence) == 36" in header
assert "const std::size_t fence_count = fence.num_fences;" in supervisor
assert "if (fence_count > fence.fences.size()) continue;" in supervisor
assert "for (std::size_t i=0;i<fence_count;++i)" in supervisor
assert "fence.num_fences < 0" not in supervisor
fence_block = supervisor.split("const std::size_t fence_count = fence.num_fences;", 1)[1].split(
    "c.queued[slot].reset();", 1)[0]
assert fence_block.index("if (fence_count > fence.fences.size()) continue;") < (
    fence_block.index("const auto& point=fence.fences[i];"))
assert "if (!done) continue;" in fence_block
assert "HasSyncpointExpired(point.id,point.value)" in fence_block

fixture = r"""
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <type_traits>
#include "core/hle/service/nvnflinger/ui/fence.h"

using Service::android::Fence;
static_assert(sizeof(Fence) == 36);
static_assert(std::is_unsigned_v<decltype(Fence{}.num_fences)>);

// The shipping fence-count check, with the loop's continue replaced by return.
// The real header's 4-element array is used, not a synthetic length.
static bool CanInspect(const Fence& fence, std::size_t* inspected) {
    const std::size_t fence_count = fence.num_fences;
    if (fence_count > fence.fences.size()) return false;
    for (std::size_t i=0;i<fence_count;++i) {
        (void)fence.fences[i];
        ++*inspected;
    }
    return true;
}

int main() {
    for (std::uint32_t n : {0u, 1u, 2u, 3u, 4u, 5u, 100u,
                             std::numeric_limits<std::uint32_t>::max()}) {
        Fence f{};
        f.num_fences = n;
        std::size_t inspected = 0;
        bool good = CanInspect(f, &inspected);
        assert(good == (n <= 4));
        assert(inspected == (good ? n : 0));
    }
}
"""
compilers = [c for c in (shutil.which("clang++"), shutil.which("g++")) if c]
assert compilers, "C++20 host compiler missing"
with tempfile.TemporaryDirectory(prefix="eden-vi2-fence-u32-") as directory:
    tmp = Path(directory)
    source_dir = tmp / "core/hle/service/nvnflinger/ui"
    source_dir.mkdir(parents=True)
    (source_dir / "fence.h").write_text(header)
    common = tmp / "common"
    common.mkdir()
    (common / "common_types.h").write_text(
        "#pragma once\n#include <cstdint>\nusing u32=std::uint32_t;\n")
    nvdrv = tmp / "core/hle/service/nvdrv"
    nvdrv.mkdir(parents=True)
    (nvdrv / "nvdata.h").write_text(
        "#pragma once\n#include <cstdint>\nnamespace Service::Nvidia {"
        "struct NvFence { std::int32_t id; std::uint32_t value; };}\n")
    cpp = tmp / "fence.cpp"
    cpp.write_text(fixture)
    for compiler in compilers:
        exe = tmp / (Path(compiler).name + "-vi2")
        subprocess.run([compiler, "-std=c++20", "-O2", "-Wall", "-Wextra",
                        "-Werror", "-Wtype-limits", "-fsanitize=undefined",
                        "-fno-sanitize-recover=all", "-I", str(tmp), str(cpp),
                        "-o", str(exe)], check=True, timeout=45)
        subprocess.run([str(exe)], check=True, timeout=20)
print("VI2_UNSIGNED_FENCE_COUNT_BOUNDS_PASS actual_header_u32=1 max=4 "
      "invalid_high_rejected=1 skip_unsafe_release=1")
