# Backends

opengnm supports two compilation backends that implement the same `sceGnm*` API
but target different environments. The backend is selected at build time via
the `OPENGNM_PLATFORM` CMake variable or the Makefile config.

---

## Backend Comparison

| | **Orbis** | **Generic** |
|---|-----------|-------------|
| **Source file** | `src/driver_orbis.c` | `src/driver_generic.c` |
| **Platform file** | `src/platform_orbis.c` | `src/platform_generic.c` |
| **Target** | PS4 hardware | Host (macOS, Linux, Windows) |
| **Compile define** | `OPENGNM_ORBIS` | `OPENGNM_GENERIC` |
| **ABI attribute** | `OPENGNM_REQUIRE_ABI` (enables `__attribute__((sysv_abi))`) | Not set (ABI macro is no-op) |
| **Submit path** | Forwards to firmware `libSceGnmDriver` (or HLE-exposed `sceGnm*` under shadPS4) | No-op / PM4 buffer capture |
| **Draw commands** | Calls firmware `sceGnmDriver*` packet builders | Emits PM4 packets directly |
| **Link dependencies** | `-lkernel -lSceGnmDriver -lSceVideoOut` | None |
| **Use case** | Production PS4 apps, hardware testing, shadPS4 HLE | Development, CI, unit tests |

---

## Orbis Backend

The Orbis backend is the production target for PS4 hardware. It delegates to
the official Sony firmware libraries while providing the opengnm descriptor
logic, surface computation, and helper functions.

### Firmware Externs

The Orbis backend links against 74 real firmware externs from
`libSceGnmDriver`. These are the functions that actually talk to the GPU:

- `sceGnmSubmitCommandBuffers` / `sceGnmSubmitAndFlipCommandBuffers`
- `sceGnmDrawInitDefaultHardwareState*`
- `sceGnmSetVsShader` / `sceGnmSetPsShader` / `sceGnmSetCsShader`
- `sceGnmDrawIndex*` / `sceGnmDrawIndirect*`
- `sceGnmDispatchDirect` / `sceGnmDispatchIndirect`
- Compute queue management (`sceGnmMapComputeQueue`, `sceGnmDingDong`)
- And more

### Forwarding Wrappers

14 `sceGnmDriver*` forwarding wrappers translate the opengnm draw command
buffer API into firmware calls:

- `sceGnmDriverDrawIndex`
- `sceGnmDriverDrawIndexAuto`
- `sceGnmDriverDrawIndexIndirect`
- `sceGnmDriverDrawIndirect`
- `sceGnmDriverDrawIndexIndirectMulti`
- `sceGnmDriverDrawIndirectMulti`
- `sceGnmDriverDrawIndexIndirectCountMulti`
- `sceGnmDriverSetVsShader`
- `sceGnmDriverSetPsShader` / `sceGnmDriverSetPsShader350`
- `sceGnmDriverSetEmbeddedVsShader` / `sceGnmDriverSetEmbeddedPsShader`
- `sceGnmDriverDrawInitDefaultHardwareState350`
- `sceGnmDriverInsertWaitFlipDone`

### Retail Stubs

172 retail stub functions return `ORBIS_GNM_ERROR_FAILURE` (`0x8eee00ff`) on
retail firmware. These cover SDMA, debugger, profiler, resource registration,
Sqtt, Spm, and other functions that are not available on retail PS4 units.

11 validate stubs cover the validation API, which is also not available on
retail firmware.

### Linking

```sh
-lopengnm -lkernel -lSceGnmDriver -lSceVideoOut
```

### HLE Emulator Support (shadPS4)

The Orbis backend also runs under HLE (high-level emulation) emulators such as
**shadPS4**, where there is no real `libSceGnmDriver` firmware module loaded.
Two accommodations make this work without source changes:

#### Two-Pass Symbol Resolution

`resolvegnmdriversym()` resolves firmware symbols in two passes:

1. **First pass** — scans loaded modules for one named `libSceGnmDriver` and
   looks up the symbol via `sceKernelDlsym` on that module handle. This is the
   real-firmware path.
2. **Second pass** — if no `libSceGnmDriver` module is present (HLE case), it
   falls back to calling `sceKernelDlsym` on *every* loaded module handle. The
   HLE layer exposes `sceGnm*` symbols through whichever module it registers
   them under, so this finds them regardless of the host module name.

Without the second pass, `sceGnmSubmitCommandBuffers` and the other forwarding
wrappers would receive a NULL symbol pointer and command buffer submission
would silently fail under shadPS4.

#### Embedded Shader Routing

The embedded shader wrappers
[`sceGnmDriverSetEmbeddedVsShader`](reference/driver.md#scegnmdriversetembeddedvsshader)
and
[`sceGnmDriverSetEmbeddedPsShader`](reference/driver.md#scegnmdriversetembeddedpsshader)
use a **runtime detection** to select between two emission paths:

- **Real hardware** (libSceGnmDriver module loaded): emit the PM4
  register-write sequence locally using the embedded shader register blobs
  (`s_embedded_vs_fullscreen`, `s_embedded_ps_dummy`,
  `s_embedded_ps_dummyrg32`). This avoids the FW 9.00 crash in firmware
  shader-set helpers (`sceGnmSetVsShader` crashes inside the GPU work pump
  context) and produces identical GPU state. The reserved dword space is
  padded with `PKT3_NOP`.
- **HLE emulator** (no GnmDriver module — e.g. shadPS4): forward to the
  HLE-exposed `sceGnmSetEmbeddedVsShader` / `sceGnmSetEmbeddedPsShader`
  entry points. The local register blobs reference firmware-embedded shader
  addresses that don't exist under HLE, so `SearchBinaryInfo` would fail.
  The HLE layer provides its own built-in dummy shader code and registers.

The runtime check (`opengnm_is_hle_runtime`) enumerates loaded modules via
`sceKernelGetModuleList` and looks for a module named `GnmDriver`. The
result is cached after the first call. This is the same detection logic
used by `resolvegnmdriversym`.

---

## Generic Backend

The Generic backend is a pure software implementation that emits PM4 packets
directly, without any PS4 firmware dependency. It is used for:

- **Host-side testing** — unit tests run on macOS/Linux/Windows
- **CI pipelines** — verify API correctness without PS4 hardware
- **Development** — iterate on descriptor logic and packet building

### PM4 Packet Building

The generic backend implements 14 PM4 packet builders that produce real PM4
command streams:

- Draw init default hardware state
- Draw index / draw index auto
- Draw index indirect / draw indirect (and multi variants)
- Set VS/PS shader
- Set embedded VS/PS shader
- Insert wait flip done

These packets can be inspected and validated by the test suite.

### No-Op Submit

`sceGnmSubmitCommandBuffers` and related submit functions are no-ops in the
generic backend — they do not send commands to a GPU. This allows testing the
command buffer building logic without hardware.

### Test Coverage

The generic backend passes 54+ tests via CMake/CTest and Makefile, covering:

- Surface computation and tiling
- Draw command buffer packet building
- Command buffer validation
- API surface completeness
- freegnm compatibility
- Helper functions
- PM4 encoding

---

## Selecting a Backend

### CMake

```sh
# Generic (host)
cmake -B build -DOPENGNM_PLATFORM=generic

# Orbis (PS4)
cmake -B build -DOPENGNM_PLATFORM=orbis \
    -DCMAKE_TOOLCHAIN_FILE=$OO_PS4_TOOLCHAIN/cmake_toolchain/openorbis.cmake

# Auto-detect (default)
cmake -B build -DOPENGNM_PLATFORM=auto
```

### Makefile

```sh
cp config.generic.mak config.mak   # Generic
cp config.orbis.mak config.mak     # Orbis
make
```

### Auto-detection

When `OPENGNM_PLATFORM=auto` (the default), opengnm checks:

1. `ORBIS` CMake variable
2. `PLATFORM_PS4` CMake variable
3. `CMAKE_SYSTEM_NAME STREQUAL "OpenOrbis"`

If any is set, the Orbis backend is selected. Otherwise, the generic backend is
used.

---

## Compile Definitions

| Define | Backend | Effect |
|--------|---------|--------|
| `OPENGNM_ORBIS` | Orbis | Selects orbis source files, links firmware libs |
| `OPENGNM_GENERIC` | Generic | Selects generic source files |
| `OPENGNM_REQUIRE_ABI` | Orbis | Enables `__attribute__((sysv_abi))` on `PS4_SYSV_ABI` |

On x86_64 (both PS4 and host), the `PS4_SYSV_ABI` macro is a no-op by default
since the System V calling convention is already the default. The
`OPENGNM_REQUIRE_ABI` define enables the actual attribute for non-x86_64
targets if opengnm is ever ported.
