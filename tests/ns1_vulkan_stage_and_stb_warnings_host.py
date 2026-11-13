#!/usr/bin/env python3
"""Compile actual vendored stb writer and Vulkan stage mapper with warning errors.

The native Vulkan source is extracted without editing its function body; tiny
stand-in enums isolate its stage switch from the full PS5/Mesa SDK.
"""
from pathlib import Path
import shutil
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
STB = ROOT / "sources/eden/externals/stb/stb_image_write.h"
MAXWELL = ROOT / "sources/eden/src/video_core/renderer_vulkan/maxwell_to_vk.cpp"

STB_FIXTURE = r"""
#include <assert.h>
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "sources/eden/externals/stb/stb_image_write.h"
static void store(void *ctx, void *data, int size) {
    (void)data;
    assert(size > 0);
    *(int *)ctx += size;
}
int main(void) {
    unsigned char rgba[4] = { 250, 50, 25, 255 };
    int size = 0;
    assert(stbi_write_tga_to_func(store, &size, 1, 1, 4, rgba) != 0);
    assert(size > 18);
    return 0;
}
"""

STAGE_PREFIX = r"""
#include <cassert>
enum VkShaderStageFlagBits : unsigned int {
    VK_SHADER_STAGE_VERTEX_BIT = 1,
    VK_SHADER_STAGE_TESSELLATION_CONTROL_BIT = 2,
    VK_SHADER_STAGE_TESSELLATION_EVALUATION_BIT = 4,
    VK_SHADER_STAGE_GEOMETRY_BIT = 8,
    VK_SHADER_STAGE_FRAGMENT_BIT = 16,
    VK_SHADER_STAGE_COMPUTE_BIT = 32
};
namespace Shader {
enum class Stage {
    VertexA, VertexB, TessellationControl, TessellationEval,
    Geometry, Fragment, Compute, Task, Mesh
};
}
static unsigned rejected = 0;
#define UNIMPLEMENTED_MSG(...) (++rejected)
"""

STAGE_POSTFIX = r"""
int main() {
    using S = Shader::Stage;
    assert(ShaderStage(S::VertexA) == VK_SHADER_STAGE_VERTEX_BIT);
    assert(ShaderStage(S::VertexB) == VK_SHADER_STAGE_VERTEX_BIT);
    assert(ShaderStage(S::TessellationControl) == VK_SHADER_STAGE_TESSELLATION_CONTROL_BIT);
    assert(ShaderStage(S::TessellationEval) == VK_SHADER_STAGE_TESSELLATION_EVALUATION_BIT);
    assert(ShaderStage(S::Geometry) == VK_SHADER_STAGE_GEOMETRY_BIT);
    assert(ShaderStage(S::Fragment) == VK_SHADER_STAGE_FRAGMENT_BIT);
    assert(ShaderStage(S::Compute) == VK_SHADER_STAGE_COMPUTE_BIT);
    assert(rejected == 0);
    // Must NOT silently report Task or Mesh as a supported Vulkan stage.
    assert(ShaderStage(S::Task) == 0);
    assert(ShaderStage(S::Mesh) == 0);
    assert(rejected == 2);
    return 0;
}
"""


def build_and_run(compiler: str, flags: list[str], text: str, suffix: str) -> None:
    with tempfile.TemporaryDirectory(prefix="encore-stage-") as d:
        source = Path(d) / ("case." + suffix)
        target = Path(d) / "test"
        source.write_text(text, encoding="utf-8")
        subprocess.run(
            [compiler, *flags, "-I", str(ROOT), str(source), "-o", str(target)],
            check=True,
        )
        subprocess.run([str(target)], check=True)


def main() -> None:
    assert STB.is_file() and MAXWELL.is_file()
    source = MAXWELL.read_text(encoding="utf-8")
    start = source.index("VkShaderStageFlagBits ShaderStage(Shader::Stage stage) {")
    end = source.index("\nVkPrimitiveTopology PrimitiveTopology(", start)
    stage_function = source[start:end]
    assert "case Shader::Stage::Task:" in stage_function
    assert "case Shader::Stage::Mesh:" in stage_function
    assert "UNIMPLEMENTED_MSG" in stage_function
    stub = STAGE_PREFIX + stage_function + "\n" + STAGE_POSTFIX
    for compiler in ("g++", "clang++"):
        if shutil.which(compiler):
            build_and_run(compiler, ["-std=c++20", "-Werror=switch", "-Werror=return-type"],
                          stub, "cpp")
            build_and_run(compiler, ["-std=c++17", "-Werror=implicit-fallthrough"],
                          STB_FIXTURE, "cpp")
    for compiler in ("gcc", "clang"):
        if shutil.which(compiler):
            build_and_run(compiler, ["-std=c11", "-Werror=implicit-fallthrough"],
                          STB_FIXTURE, "c")
    print("VENDOR_SHADER_STAGE_AND_STB_FALLTHROUGH_HOST_PASS")


if __name__ == "__main__":
    main()
