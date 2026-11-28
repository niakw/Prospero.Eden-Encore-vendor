#!/usr/bin/env python3
"""Deterministic, offline Vulkan compute translation of MIT Anime4K v4 3D US CNN.
Pinned shader: bloc97/Anime4K 7684e9586f8dcc738af08a1cdceb024cc184f426.
The upstream mpv !HOOK interface is translated into 3 convolutions plus 2x
depth-to-space; no guessed weights, invented inference or dynamic GLSL runtime.
"""
import argparse
import hashlib
import re
import subprocess
from pathlib import Path

EXPECTED_NAMES = (
    "Conv-4x3x3x3", "Conv-4x3x3x8", "Conv-4x3x3x4", "Depth-to-Space"
)
SOURCE = Path(__file__).resolve().parent / "Anime4K_3DGraphics_Upscale_x2_US.glsl"
# Exact MIT source/weights from bloc97/Anime4K 7684e9586f8dcc738af08a1cdceb024cc184f426.
PINNED_SHA256 = "ccfd16fa65908fbafe27114cf65ddc043bc746d4086716c2a85e97a283d9fe5e"


def convert(index: int, section: str) -> str:
    if EXPECTED_NAMES[index] not in section.splitlines()[0]:
        raise ValueError(f"Unexpected CNN stage {index}: {section.splitlines()[0]}")
    begin = section.find("#define go_0") if index != 3 else section.find("vec4 hook()")
    if begin < 0:
        raise ValueError(f"Missing shader function at stage {index}")
    body = section[begin:].strip()
    if body.count("vec4 hook()") != 1 or body.count("return result;") != (0 if index == 3 else 1):
        raise ValueError(f"Unexpected shader structure stage {index}")
    head = [
        "#version 450",
        "// MIT (c) 2019-2021 bloc97, faithfully ported to Vulkan compute.",
        "// Full original MIT notice is in Anime4K_3DGraphics_Upscale_x2_US.glsl.",
        "layout(local_size_x=8, local_size_y=8, local_size_z=1) in;",
        "layout(set=0, binding=0) uniform sampler2D u_source;",
    ]
    if index == 3:
        head.extend([
            "layout(set=0, binding=1) uniform sampler2D u_features;",
            "layout(set=0, binding=2, rgba16f) uniform writeonly image2D u_output;",
            "#define conv2d_last_tf_pos ((vec2(gl_GlobalInvocationID.xy) + vec2(0.5)) / vec2(imageSize(u_output)))",
            "#define conv2d_last_tf_size (vec2(textureSize(u_features, 0)))",
            "#define conv2d_last_tf_pt (vec2(1.0) / conv2d_last_tf_size)",
            "#define conv2d_last_tf_tex(pos) texture(u_features, (pos))",
            "#define MAIN_pos conv2d_last_tf_pos",
            "#define MAIN_tex(pos) texture(u_source, (pos))",
        ])
    else:
        head.extend([
            "layout(set=0, binding=1, rgba16f) uniform writeonly image2D u_output;",
            "vec4 read_input(vec2 offset) {",
            "    ivec2 size_ = textureSize(u_source, 0);",
            "    ivec2 pixel_ = clamp(ivec2(gl_GlobalInvocationID.xy) + ivec2(offset),",
            "                         ivec2(0), size_ - ivec2(1));",
            "    return texelFetch(u_source, pixel_, 0);",
            "}",
        ])
        prefix = ["MAIN", "conv2d_tf", "conv2d_1_tf"][index]
        head.append(f"#define {prefix}_texOff(offset) read_input(offset)")
    head.append(body)
    head.append("void main() {")
    head.append("    const ivec2 pos = ivec2(gl_GlobalInvocationID.xy);")
    head.append("    if (any(greaterThanEqual(pos, imageSize(u_output)))) return;")
    head.append("    vec4 result = hook();")
    if index == 3:
        # The mpv shader's 3D x2 output adds the same CNN residual to A.
        # UI transparency is not scene detail: preserve the source alpha.
        head.append("    result.a = texture(u_source, conv2d_last_tf_pos).a;")
    head.append("    imageStore(u_output, pos, result);")
    head.append("}")
    return "\n".join(head) + "\n"

def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--validate", action="store_true")
    args = parser.parse_args()
    raw = SOURCE.read_text(encoding="utf-8")
    if not raw.startswith("// MIT License") or "Copyright (c) 2019-2021 bloc97" not in raw:
        raise ValueError("Upstream license missing")
    digest = hashlib.sha256(raw.encode()).hexdigest()
    if digest != PINNED_SHA256:
        raise ValueError("Anime4K 3D CNN source/model weights changed: review and repin first")

    sections = raw.split("//!DESC ")[1:]
    if len(sections) != 4:
        raise ValueError(f"Expected 4 CNN passes, got {len(sections)}")
    args.output.mkdir(parents=True, exist_ok=True)
    for index, section in enumerate(sections):
        result = convert(index, section)
        shader = args.output / f"anime4k_3d_us_stage_{index}.comp"
        shader.write_text(result, encoding="utf-8")
        if args.validate:
            spv = args.output / f"anime4k_3d_us_stage_{index}.spv"
            subprocess.run(["glslangValidator", "-V", "--target-env", "vulkan1.1",
                            "-S", "comp", "-o", str(spv), str(shader)], check=True)
            if spv.stat().st_size <= 64:
                raise ValueError(f"Invalid shader {index}")
            subprocess.run(["spirv-val", "--target-env", "vulkan1.1", str(spv)], check=True)
            asm = subprocess.check_output(["spirv-dis", str(spv)], text=True)
            expected = {"u_source": 0, "u_output": (2 if index == 3 else 1)}
            if index == 3:
                expected["u_features"] = 1
            # Descriptor ABI in SPIR-V MUST match the native Vulkan layouts.
            for name, binding in expected.items():
                pattern = rf"OpDecorate %{name} Binding {binding}(?:\s|$)"
                if not re.search(pattern, asm):
                    raise ValueError(f"Shader {index} descriptor ABI mismatch: {name}:{binding}")
                if f"OpDecorate %{name} DescriptorSet 0" not in asm:
                    raise ValueError(f"Shader {index} descriptor set changed: {name}")
            if len(re.findall(r"OpDecorate %u_\w+ Binding", asm)) != len(expected):
                raise ValueError(f"Shader {index} adds unknown descriptors")

            import struct
            contents = spv.read_bytes()
            words = struct.unpack("<" + "I" * (len(contents) // 4), contents)
            var = f"ANIME4K_3D_US_STAGE_{index}_COMP_SPV"
            text = [
                "// SPDX-License-Identifier: MIT",
                "// Copyright (c) 2019-2021 bloc97; CNN model / weights MIT.",
                "#pragma once",
                "#include <array>",
                "#include <cstdint>",
                f"inline constexpr std::array<std::uint32_t, {len(words)}> {var} = {{",
            ]
            for off in range(0, len(words), 8):
                text.append("    " + ", ".join(f"0x{w:08x}u" for w in words[off:off+8]) + ",")
            text.append("};")
            (args.output / f"anime4k_3d_us_stage_{index}_comp_spv.h").write_text(
                "\n".join(text) + "\n", encoding="utf-8")
    manifest = args.output / "source.sha256"
    manifest.write_text(hashlib.sha256(raw.encode()).hexdigest() + "\n")
    print("Anime4K 3D x2 four-stage Vulkan shader translation PASS")

if __name__ == "__main__":
    main()
