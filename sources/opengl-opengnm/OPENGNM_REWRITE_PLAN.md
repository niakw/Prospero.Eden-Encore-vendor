# opengnm — Clean Rewrite Plan

> **Goal:** A clean-rewrite PS4 GNM library providing `sceGnm*` / `sceGpa*` drop-in API
> compatibility with the Sony official PS4 SDK. Any PS4 app or game written against the
> official SDK headers should compile and link against opengnm unmodified. Usable via
> OpenOrbis on PS4, with a generic host backend for testing.
>
> **Sony ABI reference:** `shadPS4/src/core/libraries/gnmdriver/` (207 `sceGnm*` decls).
> **Firmware modules:** `<firmware-dir>/` (decrypted ELFs).
> **RE documentation:** `tools/gnm_driver_fw900_analysis.md`, `tools/gnm_pm4_shader_analysis.md`,
> `tools/gnm_compositor_analysis.md`.

---

## OpenOrbis SDK Compatibility (binding constraints)

opengnm must build and link against the OpenOrbis PS4 toolchain. Key findings from
auditing the OpenOrbis headers and existing PS4 GNM code:

1. **OpenOrbis headers are incomplete.** `orbis/_types/gnm.h` declares `sceGnm*` as
   `void func()` with no params and uses incomplete types (`u_short`, `u_int`).
   **opengnm does NOT include `orbis/_types/gnm.h`.** We provide our own complete
   `sceGnm*` declarations with proper signatures. Consumers include `<gnm.h>` from
   opengnm instead.

2. **Target triple:** `x86_64-ps4-elf` (clang `--target=x86_64-ps4-elf -fPIC`).

3. **Sysroot:** `-isysroot $(OO_PS4_TOOLCHAIN) -isystem $(TOOLCHAIN)/include`.

4. **Link libraries (orbis):** `-lc -lkernel -lSceGnmDriver` for core GNM;
   `-lSceVideoOut` for present/flip paths (separate, not GNM).

5. **Calling convention:** `PS4_SYSV_ABI = __attribute__((sysv_abi))`. Technically
   redundant on x86_64 (sysv is default) but matches Sony SDK convention and documents
   intent. opengnm defines this in `gnm_types.h`.

6. **Error codes:** Sony `ORBIS_GNM_ERROR_*` pattern (`0x80D11000+` for submission,
   `0x80D12000+` validation warnings, `0x80D13000+` validation errors,
   `0x80D13FFF` not-enabled). Sourced from shadPS4 `gnm_error.h`.

7. **Firmware symbols:** At link time, `sceGnm*` resolve to `libSceGnmDriver.sprx`
   (via `-lSceGnmDriver`). opengnm declares externs with proper signatures; the
   firmware provides the implementations. For functions OpenOrbis doesn't stub,
   we declare them ourselves.

8. **Build config:** `config.orbis.mak` sets `PLATFORM=orbis`,
   `TOOLCHAIN=$(OO_PS4_TOOLCHAIN)`. CMake sets `OPENGNM_PLATFORM=orbis` with
   `PLATFORM_PS4=ON`.

9. **Struct binary compatibility:** All `Gnm*` structs use `_Static_assert` size
   checks (shader registers, RT, DRT, buffer, sampler, command buffer). This ensures
   binary compat with real PS4 data.

10. **Docker build:** `build.sh` wraps the OpenOrbis Docker image
    (`openorbisofficial/toolchain:latest`) for reproducible cross-compiles.

---

## Reverse Engineering Phase

opengnm's implementation must match the real PS4 firmware driver's behavior. The RE
phase provides the ground truth for PM4 packet formats, validation rules, shader
register layouts, and function signatures that cannot be derived from public docs
alone.

### RE Resources

**Firmware module dumps (FW 9.00, already decrypted ELFs):**
```
<firmware-dir>/
├── kernel.bin                                    # PS4 kernel (42MB)
├── modules/system/common/lib/
│   ├── libSceGnmDriver.sprx                      # GNM driver (96KB) — PRIMARY RE TARGET
│   ├── libSceGnmDriver.sprx.exports.txt          # 259 export entries (NID + vaddr + name)
│   ├── libSceGnmDriver.sprx.r2.txt               # radare2 symbol script
│   ├── libSceGnmDriverForNeoMode.sprx            # NEO/Pro variant (96KB)
│   ├── libSceVideoOut.sprx                       # Video output (144KB)
│   ├── libSceVideoOutSecondary.sprx              # Secondary video output
│   └── libkernel.sprx                            # Kernel library
├── modules/system/sys/
│   ├── GnmCompositor.elf                         # Sony's own GNM compositor (291KB)
│   └── gpudump.elf                               # GPU dump utility (128KB)
└── modules/system/priv/lib/
    └── libSceGnmDriver_sys.sprx                  # System-priv GNM driver variant
```

**RE documentation (in workspace `tools/`):**
- `tools/gnm_driver_fw900_analysis.md` (318 lines) — Full RE of `libSceGnmDriver.sprx`:
  binary info, export table structure, NID table (289 NIDs), GPU MMIO register table,
  utility functions, draw function map (12 functions with PM4 opcodes/counts/validation),
  shader setup functions (8 stages with register offsets), validate functions
  (**RESOLVED: all stubs returning 0**), submit functions, initialization, shader
  binary parser, DrawInitDefaultHardwareState.
- `tools/gnm_pm4_shader_analysis.md` (136 lines) — PM4 shader packet layouts for all
  shader stages (PS/VS/ES/GS/HS/LS/CS), register spaces (SH/CONTEXT), exact packet
  byte layouts.
- `tools/gnm_compositor_analysis.md` (85 lines) — Real-world GNM usage from Sony's
  compositor: 308 PM4 type3 headers, opcode distribution, NID imports.

**Cross-reference sources:**
- `shadPS4/src/core/libraries/gnmdriver/gnmdriver.h` — 207 `sceGnm*` function signatures
- `shadPS4/src/core/libraries/gnmdriver/gnm_error.h` — Sony error code constants
- `../rpcsx/rpcsx/gpu/lib/gnm/include/gnm/pm4.hpp` — secondary PM4 opcode
  semantics, especially queue/system packets plus graphics packets
- `../rpcsx/rpcsx/gpu/Pipe.cpp` + `DeviceCtl.cpp` — secondary reference for
  EOP writes, flip-on-EOP matching, and DMA_DATA mode behavior
- `../rpcsx/rpcsx/gpu/Registers.hpp` — typed MMIO/register layout and default
  state cross-checks
- `../rpcsx/rpcsx/gpu/lib/amdgpu-tiler/include/amdgpu/tiler.hpp` — independent
  AMD tiling reference for gpuaddr edge cases
- `../OrbisNet/aerolib.csv` — 97,623 NID→symbol mappings for IDA/Ghidra/r2
- `../OpenOrbis/ps4libdoc/known_names.txt` — 219 known `sceGnm*` NIDs
- `mesa/src/amd/common/sid.h` — AMD GCN register definitions (gfx6-gfx12)
- `mesa/src/amd/registers/gfx8.json` — gfx8 register spec

**RE tooling:**
- **radare2** (6.1.8) at `/opt/homebrew/bin/r2` — disassembly and analysis
- **IDA Pro / Ghidra** — decompilation with aerolib.csv symbol resolution
- **ps4debug** — TCP-based GDB-style debugger for runtime verification on PS4
- **GoldHEN v2.4b18** — kernel access for FW 9.00

**IDA Pro plugins (for automated NID resolution and module loading):**
- **ps4_module_loader** at `<ps4_module_loader-dir>/` —
  IDA loader plugin (Python, 2054 lines) for PS4 module files (.prx, .sprx, .elf, .self).
  Parses PS4-specific ELF types (`ET_SCE_DYNEXEC`, `ET_SCE_DYNAMIC`,
  `PT_SCE_DYNLIBDATA`), all PS4 dynamic tags (`DT_SCE_EXPORT_LIB` = `0x61000013`,
  `DT_SCE_IMPORT_LIB` = `0x61000015`, etc.), resolves NIDs via `aerolib.csv`
  (97,623 entries), sets up IDA segments/imports/exports/function names automatically.
  Includes `ps4_errno_700.til` (PS4 error code type library). Install: copy
  `ps4_module.py` + `aerolib.csv` into IDA loaders directory.
- **ps4_nid_resolver_ida** at `<ps4_nid_resolver_ida-dir>/` —
  IDA plugin (C++) that resolves PS4 NIDs to function names using ps4libdoc JSON
  files. Parses PS4 dynamic tags (`0x61000035` = string table, `0x61000039` = symbol
  table, `0x61000029` = PLT reloc table), looks up each NID in ps4libdoc, renames
  functions in IDA. Use: `Ctrl+F10` to resolve, `Ctrl+Alt+F10` for settings.
- **aerolib.csv** at `<aerolib.csv-path>` (also
  in `ps4_module_loader/aerolib.csv`) — 97,623 NID→symbol mappings. Format:
  `NID symbol_name` (space-delimited, one per line). Covers 219 `sceGnm*` NIDs.
- **ps4libdoc** at `<ps4libdoc-path>` —
  219 known `sceGnm*` NIDs for cross-referencing.

**NID generation (reference):**
- PS4 NIDs are SHA1 hashes of `symbol_name + NID_suffix`, truncated to 8 bytes,
  base64-encoded. The NID default_suffix is `518D64A635DED8C1E6B039B1C3E55230`
  (from [PSDevWiki/Keys](https://www.psdevwiki.com/ps4/Keys)). However, Sony uses
  firmware-version-dependent NID obfuscation, so brute-forcing unmapped NIDs with
  just the suffix does not work. Use aerolib.csv / ps4libdoc for resolution instead.
- 7 NIDs in `libSceGnmDriver.sprx` are unmapped (not in aerolib.csv or ps4libdoc):
  `nSl-NqcCi3E`, `VKLsX6TGJBM`, `otfsenvPebM`, `2T1zOhnddFQ`, `QP7vDGU0xDQ`,
  `XSIZOjHqEUI`, `qhKjy4mQhUo`. These are likely private Sony internal functions.
  They do not block opengnm (all 207 public `sceGnm*` functions are resolved).

**PSDevWiki Keys page** ([https://www.psdevwiki.com/ps4/Keys](https://www.psdevwiki.com/ps4/Keys)):
- System modules keyset revisions per firmware version (FW 9.00 = keyset 5.0)
- SAMU keys, PFS keys, portability EncDec keys, kernel keys
- Not directly needed for opengnm (we work with already-decrypted ELFs at
  `<firmware-dir>/`), but useful reference for
  understanding PS4 module encryption if working with encrypted SELF files.

### RE Deliverables

The RE phase produces verified documentation that Phases 2-6 implement against:

**RE-1: PM4 packet formats for all draw/dispatch functions [DONE]**
- Source: `tools/gnm_driver_fw900_analysis.md` — Draw Function Map
- All 12 draw functions + 4 dispatch functions RE'd with exact PM4 opcodes, counts,
  and validation rules
- Key findings: PM4 count = N-1, shader type bit (bit 1), stage bitmask 0x2B,
  flag validation (`flags & 0x1FFFFFFE == 0`), trailing NOP pattern (`0xC0021000`)

**RE-2: PM4 packet formats for all shader set/update functions [DONE]**
- Source: `tools/gnm_pm4_shader_analysis.md` + `tools/gnm_driver_fw900_analysis.md`
- All 8 shader stages (PS/VS/ES/GS/HS/LS/CS + embedded) RE'd with SH/CONTEXT
  register offsets and exact packet byte layouts
- Shader register mapping: SH regs base 0x2C000 (opcode 0x76), CONTEXT regs base
  0xA000 (opcode 0x69)

**RE-3: Validation function behavior [DONE]**
- Source: `tools/gnm_driver_fw900_analysis.md` — Validate Functions section
- **Finding: all validate functions are stubs returning 0 (success) on FW 9.00**
- `sceGnmValidateCommandBuffers`, `ValidateDrawCommandBuffers`,
  `ValidateDispatchCommandBuffers`, `ValidateGetVersion`, `ValidateOnSubmitEnabled`,
  `ValidateResetState` — all `xor eax, eax; ret`
- Implication: Phase 4 validate implementation can be no-ops on orbis (forward to
  firmware which also no-ops), real validation only on generic backend

**RE-4: Submit function packet format [DONE]**
- Source: `tools/gnm_driver_fw900_analysis.md` — Submit Functions section
- `sceGnmSubmitCommandBuffers` builds INDIRECT_BUFFER packets
  (`0xC0023300` opcode 0x33, `0xC0023F00` opcode 0x3F)
- Kernel masks ib_base with `0x000FFFFF00000000`, ORs `VMID<<52`
- `sceGnmSubmitAndFlipCommandBuffers` = submit + flip

**RE-5: DrawInitDefaultHardwareState [DONE]**
- Source: `tools/gnm_driver_fw900_analysis.md` — DrawInitDefaultHardwareState (RESOLVED) section
- PM4 blob at data table vaddr 0x7E20 (128 dwords), function at vaddr 0x3260
- Full PM4 packet sequence documented: PFP_SYNC_ME, CLEAR_STATE, SET_SH_REG_OFFSET,
  SET_SH_REG, SET_CONTEXT_REG, ACQUIRE_MEM, SET_UCONFIG_REG, CONTEXT_CONTROL
- Variants: 175 (0x3390), 200 (0x3380), 350 (0x34F0+)
- Key difference from shadPS4: real driver has PFP_SYNC_ME + SET_SH_REG_OFFSET

**RE-6: Shader binary parser [DONE]**
- Source: `tools/gnm_driver_fw900_analysis.md` — Shader Binary Parser section
- Metadata field offsets, CRC32 algorithm, resource table layout all RE'd

**RE-7: SDMA function signatures [DONE — stubs on retail]**
- 8 functions: `sceGnmSdmaOpen/Close/CopyLinear/CopyTiled/CopyWindow/ConstFill/Flush/GetMinCmdSize`
- **Finding: ALL 8 are stubs returning `ORBIS_GNM_ERROR_FAILURE` on retail firmware**
- No SDMA error strings in binary, no code references to SDMA NIDs
- shadPS4 confirms: "Not available in retail firmware"
- See `tools/gnm_sdma_debugprof_re_analysis.md`

**RE-8: Debugger/profiler function signatures [DONE — stubs on retail]**
- `sceGnmSqtt*` (25), `sceGnmSpm*` (12), `sceGnmDebugger*` (10) — **ALL stubs**
- All return `ORBIS_GNM_ERROR_FAILURE` on retail firmware
- Require kernel-level GPU access — not available in user-mode
- See `tools/gnm_sdma_debugprof_re_analysis.md`

**RE-9: Resource registration internals [DONE — stubs on retail]**
- `sceGnmRegisterOwner`/`RegisterResource`/`FindResources` — **ALL stubs**
- All return `ORBIS_GNM_ERROR_FAILURE` on retail firmware
- Resource registration is a devkit-only feature
- See `tools/gnm_sdma_debugprof_re_analysis.md`

**RE-10: GnmCompositor real-world patterns [DONE]**
- Source: `tools/gnm_compositor_analysis.md`
- 308 PM4 type3 headers analyzed, opcode distribution confirmed
- SET_CONTEXT_REG (0x69): 142 uses, SET_SH_REG (0x76): 40 uses, NOP (0x10): 90 uses
- Reference for correct command buffer construction patterns

### RE Methodology

```
1. shadPS4 source code FIRST — most signatures and PM4 formats already RE'd
   └─ src/core/libraries/gnmdriver/gnmdriver.h (207 sceGnm* decls)
   └─ src/video_core/amdgpu/ (PM4 regs, liverpool_to_vk, shader parsing)

2. Existing RE docs SECOND — tools/gnm_*_analysis.md (already completed RE)
   └─ Draw/dispatch/shader/validate/submit all documented

3. Firmware module disassembly THIRD — for remaining gaps
   └─ Load libSceGnmDriver.sprx into r2/IDA with aerolib.csv
   └─ Parse PS4 dynamic tags (0x61000013 = NID entries)
   └─ Find functions via NID → trace logic → document findings

4. GnmCompositor.elf FOURTH — real-world usage validation
   └─ Confirm our PM4 patterns match Sony's own code

5. Runtime verification FIFTH — on PS4 via ps4debug/GoldHEN
   └─ Read GPU memory to verify static analysis
   └─ Patch submit path to log command buffers if needed

6. RPCSX cross-audit SIXTH — secondary semantic comparison only
   └─ Compare broad PM4 opcode handling, EOP/flip, DMA_DATA, register defaults
   └─ Keep shadPS4 + firmware RE as authority for Sony ABI and libSceGnmDriver behavior
```

### RE Workflow for Remaining Gaps (RE-5, RE-7, RE-8, RE-9)

```bash
# Load libSceGnmDriver.sprx into radare2 with symbols
r2 -q -c '. <firmware-dir>/modules/system/common/lib/libSceGnmDriver.sprx.r2.txt' \
     <firmware-dir>/modules/system/common/lib/libSceGnmDriver.sprx

# Find function by NID (example: sceGnmSdmaOpen)
# Look up NID in aerolib.csv, then find export vaddr in .exports.txt
grep sceGnmSdmaOpen <firmware-dir>/modules/system/common/lib/libSceGnmDriver.sprx.exports.txt

# Disassemble at the found vaddr
# r2: pdf @ <vaddr>

# Cross-reference with shadPS4
grep -rn "sceGnmSdma" <shadPS4-dir>/src/
```

---

## Current State (API gap)

| Metric | Target (Sony SDK) | Status | Gap |
|--------|-------------------|--------|-----|
| `sceGnm*` functions | 207 total | 207 declared + implemented (Phase 1-3) | — |
| API naming | `sceGnm*` / `sceGpa*` | Done (Phase 1) | — |
| Header layout | Sony SDK convention | Done (Phase 1) | — |
| Calling convention | `PS4_SYSV_ABI` | Done (Phase 1) | — |
| Struct sizes | Binary-compatible | Verified (Phase 1) | — |
| PM4 emission | Match firmware | Done (Phase 2) + RE'd (RE-1 to RE-4) | — |
| Validation | Match firmware | Done (Phase 3: stubs return 0) | — |
| Orbis backend | Firmware delegation | Source complete (Phase 3) | Docker/orbis build + link smoke pass; PS4 hardware smoke pass |
| Generic backend | Host testing | Done (Phase 4) | CMake + Make tests pass |
| gpuaddr | AMD PAL math | Done (Phase 2) | Cross-audited vs RPCSX: no gaps |
| Regression tests | Host behavior + ABI edge cases | 88 passing | Hardware validation done (Phase 5C) |
| Hardware init | Firmware-captured sequences | Done (cross-audit) | FW version + Neo/OG variants from shadPS4 |
| Downstream migration | freegnm-examples + compat | Done | 9 examples build with USE_OPENGNM=1 |

**Proven algorithms to port (AMD PAL-derived, not rewrite from zero):**
- gpuaddr / AddrLib surface computation: 4,164 LOC (AMD PAL-derived math)
- GCN assembler: 9,039 LOC (needed for `sceGnmCreateFetchShader`)
- PM4 encoding: 551 LOC
- drawcommandbuffer: 1,939 LOC (PM4 command buffer building)

---

## Architecture

```
opengnm/
├── include/                        # Public headers (Sony SDK layout)
│   ├── gnm.h                       # Master include
│   ├── gnm_types.h                 # All Gnm* enums + PS4_SYSV_ABI + forward decls
│   ├── gnmdriver.h                 # sceGnm* runtime (207 functions)
│   ├── gnm_drawcommandbuffer.h     # sceGnmDrawCmd* command buffer building
│   ├── gnm_rendertarget.h
│   ├── gnm_depthrendertarget.h
│   ├── gnm_texture.h
│   ├── gnm_sampler.h
│   ├── gnm_shader.h                # Fetch shader, input semantics, stage registers
│   ├── gnm_shaderbinary.h
│   ├── gnm_dataformat.h
│   ├── gnm_error.h                 # ORBIS_GNM_ERROR_* codes
│   ├── gnm_controls.h              # Control register bitfield structs
│   ├── gpuaddr.h                   # sceGpa* surface computation
│   ├── platform.h                  # Platform detection (ORBIS/GENERIC)
│   └── pm4/                        # PM4 register definitions
│       ├── amdgfxregs.h
│       └── sid.h
├── src/
│   ├── drawcommandbuffer.c         # sceGnmDrawCmd* (PM4 emission — core value)
│   ├── rendertarget.c
│   ├── depthrendertarget.c
│   ├── texture.c
│   ├── shader.c                    # Fetch shader generation via GCN assembler
│   ├── dataformat.c
│   ├── commandbuffer.c
│   ├── error.c
│   ├── driver_orbis.c              # Orbis: 74 firmware externs + 14 wrappers + 172 stubs + 11 validate stubs
│   ├── driver_generic.c            # Generic: 14 PM4 packet builders + real sceGnm* + 172 stubs + 11 validate stubs
│   ├── platform_orbis.c            # Orbis: sceGnmGpuMode + buffer label address
│   ├── platform_generic.c          # Generic: sceGnmGpuMode + malloc'd buffer label
│   ├── gpuaddr/                    # sceGpa* surface computation (ported)
│   │   ├── surface.c
│   │   ├── tilemodes.c
│   │   ├── tiler.c
│   │   ├── decompress.c
│   │   └── surfgen.c
│   ├── gcn/                        # GCN assembler (ported, lib-internal)
│   │   ├── assembler.c
│   │   ├── decoder.c
│   │   └── ...
│   └── pm4/                        # PM4 encoding (ported)
├── tests/
│   ├── test_surface.c
│   ├── test_drawcmd.c
│   ├── test_validate.c
│   └── test_api.c
├── CMakeLists.txt
├── Makefile
├── config.orbis.mak
├── config.generic.mak
├── build.sh                        # Docker wrapper (OpenOrbis)
├── COPYING
└── README.md
```

**Tools split out:** `gcn-dis`, `gnf-conv`, `gnf-info`, `gnf-mk`, `pm4-dis`, `psb-dis`
move to a separate `opengnm-tools/` repo. The library keeps only what it needs: GCN
assembler (fetch shaders), gpuaddr, PM4 encoding. GNF/FNF/PSSL decoder code goes with
the tools.

**Shader compiler split out:** The SPIR-V to PS4 Shader Binary compiler is in a
separate `opengnm-psbc/` repo, built on Mesa 26.2.0 (NIR + ACO). It consumes
opengnm's headers for the `GnmShaderFileHeader` / `GnmVsShader` / `GnmPsShader`
binary container format. See `opengnm-psbc/OPENGNM_PSBC_PLAN.md`.

---

## Phases

### Phase 0: Reverse Engineering [DONE]

RE ground truth for PM4 formats, validation rules, and firmware behavior.
**All 10 RE deliverables complete.**

**Key finding: 122 of 207 sceGnm* functions are stubs returning
ORBIS_GNM_ERROR_FAILURE on retail firmware.** Only ~85 functions have real
implementations (draw, dispatch, shader, submit, init, compute queue, VGT, validate).

See `tools/gnm_sdma_debugprof_re_analysis.md` for the retail-vs-devkit breakdown.

**Completed RE deliverables:**
- RE-1: Draw/dispatch PM4 packet formats (12+4 functions) — DONE
- RE-2: Shader set/update PM4 packet formats (8 stages) — DONE
- RE-3: Validation function behavior (all stubs, return 0) — DONE
- RE-4: Submit function packet format — DONE
- RE-5: DrawInitDefaultHardwareState PM4 blob (128 dwords) — DONE
- RE-6: Shader binary parser — DONE
- RE-7: SDMA functions (8, all stubs on retail) — DONE
- RE-8: Debugger/profiler (Sqtt/Spm/Debugger ~47, all stubs on retail) — DONE
- RE-9: Resource registration (19, all stubs on retail) — DONE
- RE-10: GnmCompositor real-world patterns — DONE

**Method:** Load `libSceGnmDriver.sprx` into r2/IDA with `aerolib.csv` NID resolution.
Find functions via NID export table, trace logic, document findings in `tools/`.

### Phase 1: Header Layer + Build System [DONE]

The foundation — all public headers with correct Sony SDK signatures.

**Deliverables:**
- `include/gnm_types.h` — All `Gnm*` enums, `PS4_SYSV_ABI`, constants.
- `include/gnm_controls.h` — Control bitfield structs with `_Static_assert` checks.
- `include/gnmdriver.h` — All 207 `sceGnm*` function declarations with
  `PS4_SYSV_ABI` and correct signatures. Organized by category (see Appendix A).
- `include/gnm_drawcommandbuffer.h` — All `sceGnmDrawCmd*` declarations.
- `include/gnm_rendertarget.h` / `gnm_depthrendertarget.h` / `gnm_texture.h` /
  `gnm_sampler.h` / `gnm_shader.h` / `gnm_shaderbinary.h` / `gnm_dataformat.h` /
  `gnm_error.h` — Type definitions + creation functions.
- `include/gpuaddr.h` — `sceGpa*` surface computation functions.
- `include/pm4/amdgfxregs.h` + `include/pm4/sid.h` — GCN register defs.
- `include/platform.h` — `GnmGpuMode`, platform detection.
- `CMakeLists.txt` — `OPENGNM_PLATFORM` option (orbis/generic/auto).
- `Makefile` + `config.orbis.mak` + `config.generic.mak` — dual-config build.
- `build.sh` — Docker wrapper for OpenOrbis cross-compile.

**Gate P1 (PASSED):** Headers compile cleanly. `_Static_assert` sizes verified.
`#include <gnm.h>` compiles on host + orbis targets. 207 `sceGnm*` declared.

### Phase 2: Core Implementation [DONE]

Command buffer building, resource setup, shader helpers. 24 source files, ~21,683 LOC.

**Deliverables (ALL COMPLETE):**
- `src/drawcommandbuffer.c` — 60 `sceGnmDrawCmd*` PM4 emission functions (1,939 LOC)
- `src/rendertarget.c` / `depthrendertarget.c` — RT/DRT creation/sizing/address
- `src/texture.c` — Texture creation/sizing/address
- `src/shader.c` — Fetch shader generation (uses GCN assembler)
- `src/dataformat.c` — Format utils
- `src/commandbuffer.c` — Cmd buffer init/reset/alloc
- `src/error.c` — `sceGnmStrError`, message handler
- `src/gpuaddr/` — 6 files, 4,164 LOC AddrLib (AMD PAL math), `sceGpa*` naming
- `src/gcn/` — 6 files, GCN assembler (internal)
- `src/pm4/` — 4 files, PM4 encoding (internal)
- `src/u/` + `src/deps/` — Utility headers + bcdec

**Gate P2 (PASSED):** All 24 `.c` files compile with zero errors. CMake builds
`libopengnm.a` successfully (generic backend).

### Phase 3: Runtime Delegation (orbis backend) [DONE]

Thin wrappers delegating to PS4 firmware `libSceGnmDriver.sprx`. Per RE findings,
only ~85 functions have real firmware implementations; ~122 are error stubs.

**Deliverables (ALL COMPLETE):**
- `src/driver_orbis.c` (977 lines) — All runtime `sceGnm*`:
  - **74 firmware extern declarations** for real functions (Draw/Dispatch/
    Shader-set/Shader-update/Submit/Init/Compute-queue/VGT/Wave/Markers/EQ/
    Workload/Misc-getters) — resolved at runtime via `-lSceGnmDriver` NID stubs.
    Proper signatures (NOT OpenOrbis's `void func()` stubs).
  - **14 `sceGnmDriver*` forwarding wrappers** called by `drawcommandbuffer.c`,
    with signature adaptation (SceGnmDrawFlags→uint32_t, default type=0 for
    DrawIndex, default modifier=0 for SetEmbeddedPsShader).
  - **172 retail stubs** with return values matching shadPS4 firmware emulation:
    - SDMA/Sqtt/Spm/Debugger/Resource → `ORBIS_GNM_ERROR_FAILURE`
    - misc/coredump/CU/mipstats/DestroyWorkloadStream/InsertThreadTraceMarker/
      InsertSetColorMarker/DebugHardwareStatus → `ORBIS_OK` (0)
    - Razor captures → `ORBIS_GNM_ERROR_CAPTURE_FAILED_INTERNAL`
    - `DriverInternalRetrieveGnmInterface*` → `0x80000000`
    - `GetDbgGcHandle` → `-1`
    - `GetProtectionFaultTimeStamp` → `0`
    - `LogicalCuMaskToPhysicalCuMask` → passthrough (returns input)
    - `DriverTriggerCapture` → `ORBIS_GNM_ERROR_CAPTURE_RAZOR_NOT_LOADED`
    - 39 unnamed `Func_*` NID-only exports → `ORBIS_OK` (0)
  - **11 validate functions** — return 0 (matching RE-3: firmware validate stubs
    also return 0).
- `src/platform_orbis.c` — `sceGnmGpuMode` (BASE/NEO via `sceKernelIsNeoMode`,
  cached on first call), `sceGnmPlatGetBufferLabelAddress` (via
  `sceVideoOutGetBufferLabelAddress`). Externs declared inline (avoid OpenOrbis
  incomplete headers).
- `include/gnm_error.h` — Added `ORBIS_GNM_ERROR_FAILURE` (0x8EEE00FF) and
  capture error codes (`RAZOR_NOT_LOADED`, `NOTHING_TO_CAPTURE`,
  `FAILED_INTERNAL`).
- `CMakeLists.txt` + `Makefile` — Updated with orbis source files.

**Gate P3 (PASSED):** Generic build compiles with zero errors. Orbis source
files (`driver_orbis.c`, `platform_orbis.c`) compile cleanly with `clang -c`
(zero errors, zero warnings). All 207+ `sceGnm*` functions have implementations
(74 externs + 172 stubs + 2 platform). No dependency on `orbis/_types/gnm.h`.
Full orbis link test requires PS4 toolchain (Docker).

### Phase 4: Generic Backend (host testing) [DONE]

Software implementation for testing without a PS4. Merged with former Phase 5
since the stub split simplifies it.

**Deliverables (ALL COMPLETE):**
- `src/driver_generic.c` (1,200+ lines) — All runtime `sceGnm*` in software:
  - **14 `sceGnmDriver*` PM4 packet builder wrappers** that emit PM4 packets
    directly into command buffers (DrawInitDefaultHardwareState350 with full
    register setup, DrawIndex/Auto/Indirect/Multi/CountMulti, SetVs/Ps/Ps350/
    EmbeddedVs/EmbeddedPs, InsertWaitFlipDone) — ported from freegnm's
    `driver_generic.c`, adapted to opengnm's naming and types
  - **Real `sceGnm*` functions** for draw/dispatch/shader-set/shader-update/
    init/compute/VGT/markers/EQ/workload — emit PM4 or return OK/UNSUPPORTED
    (no real hardware on generic platform)
  - **Embedded shader binaries** (fullscreen VS, dummy PS, dummy RG32 PS)
  - **172 stub functions** matching orbis backend return values
  - **11 validate functions** returning 0
- `src/platform_generic.c` — `sceGnmGpuMode` returns `GNM_GPU_BASE` (or
  configured mode via `sceGnmPlatInit`), `sceGnmPlatGetBufferLabelAddress`
  uses callback if set, else malloc'd fallback (16 labels × 8 bytes).
- `CMakeLists.txt` + `Makefile` — Updated with generic source files.

**Gate P4 (PASSED):** Generic build compiles with zero errors, zero warnings
(`-Wall -Wextra -Wpedantic`). All 207+ `sceGnm*` functions have implementations
across both backends (orbis + generic). All 4 backend source files compile
cleanly with `clang -c`.

### Phase 5: Tests + Integration [PARTIAL]

Merged former Phases 4 (validate/resource/workload — now all stubs, trivial) and 7.
Host-side tests, OpenOrbis link validation, PS4 package generation, package
staging, and the PS4 hardware-smoke run are complete. A first downstream
migration layer is also present: opt-in `freegnm` source compatibility maps
compatible `gnm*`/`gpa*` wrapper calls to the Sony SDK-style `sceGnm*`/`sceGpa*`
ABI without exporting a second binary ABI.

**Deliverables:**
- `tests/test_surface.c` — gpuaddr surface computation (7 tests) ✅
- `tests/test_drawcmd.c` — PM4 command buffer building + ABI regressions (17 tests) ✅
- `tests/test_validate.c` — PM4 validation, generic backend (9 tests) ✅
- `tests/test_api.c` — Call every sceGnm* category once (18 tests) ✅
- `tests/test_compat.c` — freegnm-style include paths and wrapper aliases (3 tests) ✅
- `tests/link_smoke.c` — PS4-target link smoke against OpenOrbis SDK libs ✅
- `tests/hardware_smoke.c` — PS4 VideoOut + direct-memory + submit/EOP smoke ✅
- `tests/test.h` — minimal test framework (utassert/utasserteq/test_suite) ✅
- `tests/test_main.c` — harness entry point ✅
- CMakeLists.txt: opengnm_tests target + ctest registration ✅
- Makefile: tests target ✅
- Makefile: `link-smoke` target for `PLATFORM=orbis` ✅
- Makefile: `hardware-smoke` target for `PLATFORM=orbis` ✅
- Makefile: `hardware-smoke-pkg` target for installable PS4 package ✅
- Full OpenOrbis Docker/orbis build + link smoke (`./build.sh docker-build`) ✅
- Native macOS package generation with OpenOrbis v0.5.4 LLVM 18 and Homebrew
  `llvm@18`: `./build.sh macos-hardware-pkg` builds and validates
  `IV0000-OGNM00001_00-OPENGNMHWSMOKE00.pkg` ✅
- Assess Eden/example direct-opengnm integration — DONE: adapter layer started;
  unsupported tooling-only old headers still need migration
- `freegnm-examples/triangle`: `make -B USE_OPENGNM=1 tri` links against
  `opengnm/libopengnm.a` in the OpenOrbis Docker environment ✅
- `freegnm-examples/eden-composite-blit`: `make -B USE_OPENGNM=1
  eden_composite_blit` links against `opengnm/libopengnm.a` in the OpenOrbis
  Docker environment ✅
- `freegnm-examples/eden-composite-dma`: `make -B USE_OPENGNM=1
  eden_composite_dma` links against `opengnm/libopengnm.a` in the OpenOrbis
  Docker environment ✅
- `freegnm-examples/eden-triangle-wrapper`: `make -B USE_OPENGNM=1
  eden_triangle_wrapper` links against `opengnm/libopengnm.a` in the OpenOrbis
  Docker environment ✅
- PS4 hardware smoke test for submit/draw/present paths — PASS on 2026-07-03
  and reconfirmed on 2026-07-04: full-screen green with scrolling white bar and
  digit `0`; GoldHEN reported about 3.15 FPS for the CPU-filled status presenter.

**Gate P5A (PASSED):** All 54 host tests pass on generic backend via CMake/CTest,
strict CMake warning build, and `build.sh tests` / Makefile.

**Gate P5B (PASSED):** OpenOrbis Docker/orbis compile, link smoke,
hardware-smoke package generation, FTP staging to the configured PS4, and the
hardware run succeed. The visible result was a full-screen green status view
with scrolling white bar and digit `0`, confirming the EOP label write after
`sceGnmSubmitCommandBuffers`/`sceGnmSubmitDone`. Native macOS package generation
also passes with the OpenOrbis v0.5.4 LLVM 18 SDK; the verified package SHA-256
is `f49f68212c21d378689c913610bf49ba8f1f4d8325f3d8c78c31da0ab330e358`.

**Phase 5C: Hardware hardening package matrix [DONE]**

OpenGNM has confirmed PS4 hardware passes across the submit/EOP smoke test,
the first advanced package matrix, and repeated launch stability checks through
third launch. Longer soak/cold-boot reruns are still useful before making it
Eden's default GPU library. Before switching Eden from freegnm to OpenGNM,
validate a small package matrix on hardware:

- Submit/EOP smoke: existing `tests/hardware_smoke.c`, repeated after cold boot
  and repeated launches.
- Triangle draw: freegnm-compatible triangle linked with `USE_OPENGNM=1`.
- Composite blit: `freegnm-examples/eden-composite-blit` linked with OpenGNM.
- Composite DMA: `freegnm-examples/eden-composite-dma` linked with OpenGNM.
- Renderer-draw wrapper: `freegnm-examples/eden-triangle-wrapper` linked with
  OpenGNM, including repeated-run/freeze checks.
- Crash logging: every package should use the existing log-file pattern and
  record stage, PM4 packet counts, EOP label values, submit return codes, flip
  requests, and frame counters before and after submit.

**Gate P5C:** packages are installable, visible output matches the expected
smoke visuals, repeated launches do not freeze the console, and crash logs are
useful when a package fails.

**Phase 5C build/staging status (2026-07-04):**

- `FGNM00000` triangle, `FGNM00001` spinning cube, `FGNM00008` composite DMA,
  `FGNM00009` composite blit, and `FGNM00011` renderer-draw wrapper all build
  with `USE_OPENGNM=1`.
- Each builder now forces a clean PS4-target OpenGNM static library inside the
  Docker/OpenOrbis environment so it cannot accidentally link a host-built
  `libopengnm.a`.
- All five packages are staged to `/data/pkg` on the PS4 FTP server.
- `FGNM00000` triangle passed on hardware at 60 FPS.
- `FGNM00001` spinning cube passed on hardware: visible textured cube, 60 FPS,
  about 2% CPU usage.
- `FGNM00008` composite DMA passed on hardware: tiles slowly flipping, scrolling
  top bar, 4.61 FPS.
- `FGNM00009` composite blit passed on hardware: tiles slowly flipping,
  scrolling top bar, 5 FPS.
- `FGNM00011` renderer-draw wrapper passed on hardware: orange gradient
  triangle, 60 FPS.
- Second- and third-launch/freeze checks passed for the OpenGNM-linked advanced
  packages.
- Native macOS OpenOrbis rebuilds of the same package matrix pass packaging
  validation without Docker. The wrappers use Homebrew `llvm@18`, OpenOrbis
  v0.5.4 LLVM 18 macOS tools, and prebuilt `.sb` shader assets; `psbc` is only a
  reference/regeneration tool for this path. The cube package uses the local
  `cglm` checkout and its `.sb` shader assets were regenerated with Docker
  `psbc` before the native package build.

**Downstream migration started:** Eden and `freegnm-examples` link `../freegnm`
and call `gnm*` wrapper functions (`gnmCmdInit`, `gnmDrawCmd*`,
`GnmCommandBuffer`, `gpaFindOptimalSurface`, etc.). opengnm now provides
source-only aliases in `<compat/freegnm.h>` plus core `<gnm/...>` forwarding
headers. This preserves the official `sceGnm*` / `sceGpa*` binary ABI because no
exported `gnm*` or `gpa*` symbols are added. The `triangle`, `cube`,
`eden-composite-blit`, `eden-composite-dma`, and `eden-triangle-wrapper`
examples have opt-in opengnm link paths. Consumers that
include old `gnm/pssl/*`, `gnm/gnf/*`, or other tool-layer headers still need
source migration to opengnm's split tool libraries.

**Phase 5D: bgfx consumer integration surface [DONE — 2026-07-06]**

OpenGNM now has a stable enough public surface for a bgfx GNM backend to include
and link directly:

- Public headers are C++ clean under `clang++ -std=c++14`; exported C APIs use
  `OPENGNM_EXTERN_C_BEGIN` / `OPENGNM_EXTERN_C_END`, and `_Static_assert` falls
  back to `static_assert` in C++.
- Generated separator fragments in `gnmdriver.h`, `src/driver_generic.c`, and
  `src/driver_orbis.c` were normalized to valid C comments.
- `gnm_dataformat.h`, `gnm_buffer.h`, `gnm_texture.h`, and
  `gnm_rendertarget.h` no longer rely on C-only initializer forms in inline
  helpers used by C++ consumers.
- `<gnm_helpers.h>` adds renderer-oriented helpers for direct memory, VideoOut
  backbuffer layout and optional Orbis flip handling, 2D texture setup, color
  render-target setup, shader binary metadata extraction, and command-buffer
  validation diagnostics.
- CMake now exports `opengnm::opengnm`; install rules generate
  `opengnmConfig.cmake`, `opengnmConfigVersion.cmake`, and `opengnm.pc`.
- Orbis consumers should link `-lopengnm -lkernel -lSceGnmDriver -lSceVideoOut`.
  Generic builds provide host-test submit stubs; Orbis builds intentionally
  resolve `sceGnmSubmit*` through firmware `libSceGnmDriver`.
- Verification passed for generic CMake/CTest, installed C and C++ header
  compile checks through pkg-config flags, and Orbis backend syntax checks with
  `OPENGNM_ORBIS`.

### Compatibility Audit: ABI and PM4 Edge Cases [DONE — 2026-07-03]

Review pass against shadPS4 and firmware-derived behavior found and fixed:

- Command-buffer resize callback logic (`cmdcanfit`) now only succeeds after the
  callback actually provides enough space.
- `sceGnmCmdAllocInside` now rounds byte sizes up to dwords, rejects non-power-of-two
  alignments, and re-checks post-callback capacity.
- Register range validation now uses byte ranges and aborts on invalid CONTEXT/SH/
  UCONFIG ranges.
- Generic draw packet builders preserve NEO render-target-slice bits in draw initiator
  fields and reject null index addresses.
- Public indirect-draw wrappers now reject out-of-range SGPR offsets before truncation.
- `sceGnmDrawIndexOffset` now requires the firmware-compatible exact size of 9 dwords.
- `sceGnmDrawCmdDrawIndexOffset` delegates to the ABI-aware driver path.
- `build.sh` generated configs include `-I./src`, and Makefile compile loops fail fast.
- OpenOrbis Docker now uses the SDK path present in the official image and runs a
  PS4-target `link-smoke` executable link against `libopengnm.a`, `libkernel`,
  `libSceGnmDriver`, and `libSceVideoOut`.
- Added a packageable PS4 hardware smoke app that presents visible VideoOut
  status, allocates garlic direct memory, emits default hardware state, a
  zero-work draw packet, an EOP write, and submits through
  `sceGnmSubmitCommandBuffers`/`sceGnmSubmitDone`.

Regression coverage added in `tests/test_drawcmd.c` for callback growth, small
`AllocInside`, exact DrawIndexOffset sizing, null index address rejection, SGPR offset
rejection, and NEO slice-bit preservation.

### RPCSX Cross-Audit [DONE]

Cross-audited opengnm's PM4 packet handling against RPCSX and shadPS4.

**Findings and resolutions:**

- `EVENT_WRITE_EOP`: opengnm encoding matches. EOP `invL2` bit (bit 20) found
  in RPCSX is a GFX9+ `RELEASE_MEM` feature — not applicable to PS4 (GFX7
  Liverpool). Interrupt select is hardcoded to write-confirm (value 3) which
  matches the common case used by all freegnm-examples and Eden.
- `DMA_DATA`: opengnm's FillMemory/CopyMemory cover the memory-to-memory and
  fill cases. GDS source/destination and L2 cache policy modes are not exposed
  — these are compute-only features not used by any current consumer. RPCSX
  and shadPS4 handle them in their rasterizer, not in the packet builder.
- `WRITE_DATA`: not implemented in opengnm or freegnm. The Sony GNM API does
  not expose WRITE_DATA as a public function. RPCSX/shadPS4 handle it in their
  command processor emulator, not as a GNM API call.
- `WAIT_REG_MEM`: opengnm uses memory space (MEM_SPACE=1) which matches all
  current usage. Register space waits are for internal firmware use only.
- **Register default init (FIXED)**: opengnm previously used hand-written
  register writes. Replaced with firmware-captured sequences from shadPS4,
  adding FW version-specific (base/1.75/2.00/3.50) and Neo/OG/Neo-compat
  variants. See commit `1137919`.
- **gpuaddr tiling**: all features (macro tile mode, pipe config, tile split,
  bank height, bank width, num banks, macro tile aspect) are present in
  `tilemodes.c` (836 lines, 45 functions). No gaps found.

---

## Design Decisions

1. **Port proven algorithms, rewrite structure.** gpuaddr (4K LOC AddrLib), GCN
   assembler (9K LOC), PM4 encoding are battle-tested. The "clean rewrite" applies to
   API surface, header organization, naming, and code structure — implementation
   logic is ported with renaming and refactoring.

2. **`sceGnm*` naming throughout.** All public functions use `sceGnm*` / `sceGpa*`.
   Internal helpers use `opengnm_*` prefix.

3. **`PS4_SYSV_ABI` on all declarations.** No-op on x86_64 (sysv is default).
   Enabled via `OPENGNM_REQUIRE_ABI` for non-x86_64 targets. Matches Sony SDK
   convention and documents intent.

4. **Binary-compatible struct layouts.** All `Gnm*` struct sizes preserved via
   `_Static_assert`.

5. **No exported `gnm*` binary ABI.** Core compatibility is source-only through
   opt-in aliases. Linked code still targets `sceGnm*` / `sceGpa*`.

6. **No `orbis/_types/gnm.h` dependency.** opengnm provides complete declarations
   (OpenOrbis headers are incomplete — `void func()` stubs, missing types).

7. **Library vs tools split.** Library keeps GCN assembler (fetch shaders), gpuaddr,
   PM4, core GNM. GNF/FNF/PSSL decoder code → `opengnm-tools/`.

8. **Two backends, same headers.** `#include <gnm.h>` works on orbis + generic.
   Backend via build config.

9. **RE-driven implementation.** PM4 packet formats, validation rules, and firmware
   behavior are verified against `libSceGnmDriver.sprx` disassembly (RE-1 to RE-10),
   not guessed from public docs alone.

---

## Appendix A: sceGnm* Function Categories (207 total)

| Category | Count | Phase | RE Status | Notes |
|----------|-------|-------|-----------|-------|
| Draw (DrawIndex/Auto/Indirect/Multi/Offset) | 12 | 2-3 (DONE) | RE-1 DONE | Core, PM4 RE'd |
| Dispatch (Direct/Indirect/OnMec/Init) | 3 (stub) | 2-3 (DONE) | RE-1 DONE | Core, PM4 RE'd |
| Shader set (Vs/Ps/Ps350/Cs/CsMod/Gs/Es/Hs/Ls/Embedded) | 11 | 2-3 (DONE) | RE-2 DONE | Core, PM4 RE'd |
| Shader update (Vs/Ps/Ps350/Gs/Hs) | 5 | 3 (DONE) | RE-2 DONE | Forward to firmware |
| Submit (CommandBuffers/AndFlip/ForWorkload/Done/RequestFlip) | 7 | 3 (DONE) | RE-4 DONE | Forward to firmware |
| Init (DefaultHardwareState 175/200/350/ContextState 400) | 3 (stub) | 2-3 (DONE) | RE-5 DONE | PM4 blob needs RE |
| SDMA (Open/Close/CopyLinear/CopyTiled/CopyWindow/ConstFill/Flush/GetMinCmd) | 8 | 3 (DONE) | RE-7 DONE (stubs) | Return FAILURE |
| Compute queue (Map/MapPriority/Unmap/TessRing/GsRing) | 5 | 3 (DONE) | — | Forward to firmware |
| VGT/Wave (SetVgtControl/Reset/WaveLimit*) | 3 (stub) | 3 (DONE) | — | Forward to firmware |
| Validate (Validate*/GetDiagnostics/Disable/Reset/Register) | 11 | 3 (DONE) | RE-3 DONE | Stubs return 0 |
| Resource (RegisterOwner/Resource/Find/Get/Set/Unregister) | 19 | 3 (DONE) | RE-9 DONE (stubs) | Return FAILURE |
| Workload (Begin/End/Create/Destroy/DingDong/AreSubmits) | 8 | 3 (DONE) | — | Forward to firmware |
| EQ (AddEqEvent/Delete/GetEventType/GetTimeStamp) | 3 (stub) | 3 (DONE) | — | Forward to firmware |
| Sqtt (trace buffer profiling) | 25 | 3 (DONE) | RE-8 DONE (stubs) | Return FAILURE |
| Spm (performance counters) | 12 | 3 (DONE) | RE-8 DONE (stubs) | Return FAILURE |
| Debugger (GetAddressWatch/Halt/Read/Write/Resume) | 10 | 3 (DONE) | RE-8 DONE (stubs) | Return FAILURE |
| Markers (Push/Pop/Color/Set/ThreadTrace) | 3 (stub) | 3 (DONE) | — | Forward to firmware |
| Coredump/Misc (GetCoredump*/GetDebugTimestamp/GetLastWaited) | 8 | 3 (DONE) | — | Mixed OK/FAILURE |
| DriverInternal (RetrieveGnmInterface* 7 variants/VirtualQuery/TriggerCapture) | 10 | 3 (DONE) | — | 0x80000000/OK/FAILURE |
| LogicalCu/GpuPa (CuIndex/Mask/Tca/Physical) | 5 | 3 (DONE) | — | Mixed OK/passthrough |
| MipStats (Setup/Disable/RequestAndReset) | 3 | 3 (DONE) | — | Return OK |
| Misc (FlushGarlic/GetGpuCoreClock/GetNumTca/GetOffChipTess/IsUserPa/PaHeartbeat) | 14 | 3 (DONE) | — | Mixed real/stub |

---

## Risk Register

| Risk | Likelihood | Impact | Mitigation |
|------|-----------|--------|------------|
| PM4 emission doesn't match firmware | Low | High | RE-1/RE-2 verified byte layouts; diff test output |
| gpuaddr port introduces surface bugs | Low | High | Port verbatim, diff test output against reference |
| sceGnm* signature mismatch with firmware | Med | High | Cross-check shadPS4 RE + aerolib NIDs + firmware disassembly |
| Missing firmware symbol at link | Med | Med | Declare externs; link test on orbis |
| Struct layout drift | Low | High | `_Static_assert` every struct size |
| OpenOrbis header conflict | Med | Med | Don't include `orbis/_types/gnm.h` |
| Orbis runtime mismatch | Low | High | Gate P5B passed on hardware: package launch reached green code `0` after submit/EOP |
| Host backend PM4 edge-case drift | Low | High | Regression tests (88 passing) + RPCSX cross-audit done + firmware-captured init sequences |
| Hardware coverage too narrow | Low | High | Phase 5C package matrix passed; 9 examples build with USE_OPENGNM=1 |
| RPCSX behavior differs from real PS4 ABI | Low | Med | Cross-audit done; shadPS4 firmware sequences imported for hw init; gpuaddr tiling verified |
| Stream-out buffer base address incomplete | Med | Med | RE `STRMOUT_BUFFER_UPDATE`/base-address behavior before wiring |
| Eden/freegnm downstream migration breaks builds | Low | Med | All freegnm-examples build with USE_OPENGNM=1; PSSL/GNF compat headers added |
| Debugger stubs wrong error codes | Low | Low | Match `ORBIS_GNM_ERROR_*` |

---

## Execution Order

0. **Phase 0 (RE) [DONE]** — All 10 RE deliverables complete. 122/207 functions are stubs on retail.
1. **Phase 1 (DONE)** — Headers + build system. 207 sceGnm* declared.
2. **Phase 2 (DONE)** — Core implementation. 24 source files, libopengnm.a builds.
3. **Phase 3 (DONE)** — Runtime delegation (orbis backend). 74 real externs + 14 sceGnmDriver* wrappers + 172 retail stubs + 11 validate stubs + 2 platform functions.
4. **Phase 4 (DONE)** — Generic backend (host testing). 14 PM4 packet builders + real sceGnm* + 172 stubs + 11 validate stubs + 2 platform functions.
5. **Phase 5A (DONE)** — Host tests (88 tests, all passing via CMake/CTest and Makefile).
6. **Phase 5B (DONE)** — OpenOrbis/orbis build + link smoke + package generation + PS4 hardware smoke run passed.
7. **Phase 5C (DONE)** — Hardware hardening package matrix: submit/EOP, triangle, composite blit, composite DMA, renderer-draw wrapper, repeated launch stability, and crash logs.
8. **TODO cleanup (DONE)** — All 18 TODO/FIXME comments resolved across 10 source files. Zero TODO/FIXME remaining.
9. **RPCSX cross-audit (DONE)** — Compared PM4/EOP/DMA/wait/default-state/tiling behavior against RPCSX and shadPS4. Replaced hand-written hardware init with firmware-captured sequences from shadPS4. Added Neo/OG and FW version-specific init sequences. gpuaddr tiling already matches (macro tile mode, pipe config, tile split, bank height all present in tilemodes.c). EOP invL2 bit not applicable (GFX9+ only, PS4 is GFX7).
10. **Downstream migration (DONE)** — Source-only `gnm*`/`gpa*` aliases and core `<gnm/...>` forwarding headers are present. PSSL and GNF tooling headers extracted into opengnm compat layer. All freegnm-examples (triangle, cube, shader-test, eden-composite-blit, eden-composite-dma, eden-triangle-wrapper, gltf, indirect, instances) build with `USE_OPENGNM=1`.

After Phase 4, opengnm builds on both PS4 (orbis) and host (generic).
After Phase 5A, host behavior is regression-tested (88 tests). After Phase 5B, the
OpenOrbis linker path, installable package path, VideoOut presentation path, and
GNM submit/EOP path are verified on PS4 hardware. Phase 5C broadens that into a
small reusable-GPU-API confidence matrix. All TODO/FIXME comments are resolved.
The RPCSX cross-audit replaced hand-written hardware init with firmware-captured
sequences and verified gpuaddr tiling has no gaps. All freegnm-examples build
with USE_OPENGNM=1 via the compat layer (PSSL/GNF types + freegnm aliases).
opengnm-psbc (shader compiler) is complete and hardware-validated.

**All planned work is complete.** opengnm is feature-complete, hardware-validated,
and ready for Eden to default to as its GNM backend. Remaining work is in sibling
projects: `vulkan-ps4` (Vulkan-on-GNM), `openagc-psbc` (PS5 shader compiler),
and the Eden PS4 port itself.
