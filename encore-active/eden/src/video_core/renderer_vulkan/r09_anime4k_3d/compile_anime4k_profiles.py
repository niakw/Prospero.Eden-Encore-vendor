#!/usr/bin/env python3
"""Compile pinned upstream MIT Anime4K Restore Soft and Upscale Denoise CNN
as Vulkan compute. The proprietary weights are NOT altered or learned anew:
these weights are MIT from bloc97/Anime4K (upstream file content hashes).
"""
import argparse
import hashlib
import re
import subprocess
import struct
from pathlib import Path

ROOT = Path(__file__).resolve().parent
MODELS = (
    ("soft", "Anime4K_Restore_CNN_Soft_S.glsl",
     "9f6867f2ef42786729522d86fe24147cb4ea145418e3974df70496acf52dc392", 4),
    ("denoise", "Anime4K_Upscale_Denoise_CNN_x2_S.glsl",
     "1a45ad3aa20d8368399f2fd46791deed957c2fd0a4afd131cc92516827abab93", 5),
)

def stage_shader(tag: str, stage: int, total: int, block: str) -> str:
    is_last = stage == total - 1
    if block.count("vec4 hook()") != 1:
        raise ValueError("unexpected hook shader")
    if is_last:
        start = block.index("#define go_0") if tag == "soft" else block.index("vec4 hook()")
    else:
        start = block.index("#define go_0")
    shader = block[start:].strip()
    header = [
        "#version 450",
        "// MIT (c) 2019-2021 bloc97, faithful CNN weights.",
        "layout(local_size_x=8, local_size_y=8, local_size_z=1) in;",
        "layout(set=0,binding=0) uniform sampler2D u_source;",
    ]
    if is_last:
        header.extend([
            "layout(set=0,binding=1) uniform sampler2D u_features;",
            "layout(set=0,binding=2,rgba16f) uniform writeonly image2D u_output;",
            "#define MAIN_pos ((vec2(gl_GlobalInvocationID.xy)+vec2(.5)) / vec2(imageSize(u_output)))",
            "#define MAIN_tex(pos) texture(u_source,pos)",
        ])
        if tag == "soft":
            header += [
                "vec4 read_features(vec2 offset) {",
                " ivec2 dims=textureSize(u_features,0);",
                " ivec2 p=clamp(ivec2(gl_GlobalInvocationID.xy)+ivec2(offset),ivec2(0),dims-ivec2(1));",
                " return texelFetch(u_features,p,0);",
                "}",
                "#define conv2d_2_tf_texOff(offset) read_features(offset)",
            ]
        else:
            header += [
                "#define conv2d_last_tf_pos MAIN_pos",
                "#define conv2d_last_tf_size vec2(textureSize(u_features,0))",
                "#define conv2d_last_tf_pt (1.0/conv2d_last_tf_size)",
                "#define conv2d_last_tf_tex(pos) texture(u_features,pos)",
            ]
    else:
        prefix = ("MAIN", "conv2d_tf", "conv2d_1_tf", "conv2d_2_tf")[stage]
        header += [
            "layout(set=0,binding=1,rgba16f) uniform writeonly image2D u_output;",
            "vec4 read_input(vec2 offset) {",
            " ivec2 sz=textureSize(u_source,0);",
            " ivec2 pos=clamp(ivec2(gl_GlobalInvocationID.xy)+ivec2(offset),ivec2(0),sz-ivec2(1));",
            " return texelFetch(u_source,pos,0);",
            "}",
            f"#define {prefix}_texOff(offset) read_input(offset)",
        ]
    header += [
        shader,
        "void main(){",
        " ivec2 pixel=ivec2(gl_GlobalInvocationID.xy);",
        " if(any(greaterThanEqual(pixel,imageSize(u_output)))) return;",
        " vec4 result=hook();",
    ]
    if is_last:
        header.append(" result.a=texture(u_source,MAIN_pos).a;")
    header += [" imageStore(u_output,pixel,result);", "}"]
    return "\n".join(header) + "\n"

def run(out: Path, validate: bool):
    out.mkdir(parents=True, exist_ok=True)
    for tag, filename, digest, count in MODELS:
        raw=(ROOT/filename).read_text(encoding="utf8")
        if hashlib.sha256(raw.encode()).hexdigest() != digest:
            raise ValueError("Unreviewed Anime4K CNN model source: "+filename)
        if not raw.startswith("// MIT License"):
            raise ValueError("MIT license absent")
        sections=raw.split("//!DESC ")[1:]
        if len(sections) != count:
            raise ValueError(f"{tag}: expected {count} passes, got {len(sections)}")
        for i, block in enumerate(sections):
            source=stage_shader(tag,i,count,block)
            stem=f"anime4k_{tag}_stage_{i}"
            comp=out/(stem+".comp")
            comp.write_text(source,encoding="utf8")
            if not validate:
                continue
            spv=out/(stem+".spv")
            subprocess.run(["glslangValidator","-V","--target-env","vulkan1.1",
                            "-S","comp","-o",str(spv),str(comp)],check=True)
            subprocess.run(["spirv-val","--target-env","vulkan1.1",str(spv)],check=True)
            asm=subprocess.check_output(["spirv-dis",str(spv)],text=True)
            expected={"u_source":0,"u_output":2 if i==count-1 else 1}
            if i==count-1:
                expected["u_features"]=1
            for name,slot in expected.items():
                if not re.search(rf"OpDecorate %{name} Binding {slot}(?:\s|$)",asm):
                    raise ValueError(f"ABI mismatch {tag} {i} {name} binding {slot}")
                if f"OpDecorate %{name} DescriptorSet 0" not in asm:
                    raise ValueError(f"ABI mismatch {tag} {i} descriptor set")
            bytes_=spv.read_bytes()
            words=struct.unpack("<"+"I"*(len(bytes_)//4),bytes_)
            var=f"ANIME4K_{tag.upper()}_STAGE_{i}_COMP_SPV"
            header=[
                "// SPDX-License-Identifier: MIT",
                "// MIT weights © 2019-2021 bloc97",
                "#pragma once",
                "#include <array>",
                "#include <cstdint>",
                f"inline constexpr std::array<std::uint32_t, {len(words)}> {var} = {{",
            ]
            for j in range(0,len(words),8):
                header.append("    "+", ".join(f"0x{x:08x}u" for x in words[j:j+8])+",")
            header += ["};"]
            (out/(stem+"_comp_spv.h")).write_text("\n".join(header)+"\n",encoding="utf8")
        print(f"{tag.upper()}_CNN_VULKAN_SHADER_ABI_OK stages={count}")

if __name__=="__main__":
    parser=argparse.ArgumentParser()
    parser.add_argument("--output",type=Path,required=True)
    parser.add_argument("--validate",action="store_true")
    args=parser.parse_args()
    run(args.output,args.validate)
