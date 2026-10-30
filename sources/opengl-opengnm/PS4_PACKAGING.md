# PS4 Packaging and Hardware Smoke Runbook

This document records the packaging process for turning the `opengnm`
PS4-target smoke ELF into an installable PS4 package, staging it to the test
console, and validating the result with PS4debug.

## Current Status

The current `opengnm` build can produce:

- `opengnm_link_smoke.elf`: verifies that `libopengnm.a` links against
  OpenOrbis `libkernel`, `libSceGnmDriver`, and `libSceVideoOut`.
- `opengnm_hw_smoke.elf`: a hardware smoke executable that allocates garlic
  direct memory, builds a small draw command buffer, submits it through
  `sceGnmSubmitCommandBuffers`, calls `sceGnmSubmitDone`, and waits for an EOP
  label write.
- `IV0000-OGNM00001_00-OPENGNMHWSMOKE00.pkg`: an installable package wrapping
  the smoke executable as title id `OGNM00001`.

The current hardware result, observed again on 2026-07-04, is a full-screen
green status view with a scrolling white bar at the top and a large digit `0`.
GoldHEN reported about 3.15 FPS. The low FPS is expected for the CPU-filled
status presenter and does not block the GNM submit/EOP result: VideoOut is
presenting and the EOP label write after `sceGnmSubmitCommandBuffers` /
`sceGnmSubmitDone` completed.

Native macOS packaging is verified as of 2026-07-04 with the OpenOrbis v0.5.4
LLVM 18 SDK and Homebrew `llvm@18`:

```sh
./build.sh macos-hardware-pkg
```

Verified package artifact:

```text
IV0000-OGNM00001_00-OPENGNMHWSMOKE00.pkg
SHA-256: f49f68212c21d378689c913610bf49ba8f1f4d8325f3d8c78c31da0ab330e358
```

## Consumer Build Interface

As of 2026-07-06, OpenGNM installs a stable consumer interface for renderer
backends such as bgfx:

- CMake package target: `opengnm::opengnm`
- pkg-config file: `opengnm.pc`
- installed public headers under the configured include directory
- static library under the configured library directory

CMake consumers:

```cmake
find_package(opengnm CONFIG REQUIRED)
target_link_libraries(my_renderer PRIVATE opengnm::opengnm)
```

pkg-config consumers:

```sh
cc $(pkg-config --cflags opengnm) -c renderer.c
cc renderer.o $(pkg-config --libs --static opengnm)
```

PS4 targets still need the firmware libraries that the Orbis backend delegates
to:

```sh
-lopengnm -lkernel -lSceGnmDriver -lSceVideoOut
```

`libopengnm.a` exports the `sceGnmDrawCmd*` wrapper symbols used by higher-level
renderers. On Orbis, `sceGnmSubmit*` is expected to resolve from firmware
`libSceGnmDriver`; the generic backend provides no-op host-test submit
implementations.

`<gnm_helpers.h>` covers the setup paths most useful to packageable renderers:
direct memory, VideoOut backbuffer layout/flip helpers, texture and color
render-target descriptors, shader binary metadata, and command-buffer
validation diagnostics.

Advanced OpenGNM matrix packages staged to `/data/pkg` on 2026-07-04:

| Title ID | Package | SHA-256 | Hardware result |
|---|---|---|---|
| `FGNM00000` | `IV0000-FGNM00000_00-TRIANGLESAMPLE00.pkg` | `d57f5550d6e29a53313a91a7025f79bb60877b990eca21b2d8941ff23e34a69b` | PASS: visible triangle sample at 60 FPS |
| `FGNM00008` | `IV0000-FGNM00008_00-EDENCOMPOSITEDMA.pkg` | `2a0ea27e56ff0e3b9c7f47efc4632e7024b53409f349d3bd62f015a6c46d5aa9` | PASS: tiles slowly flipping, scrolling top bar, 4.61 FPS |
| `FGNM00009` | `IV0000-FGNM00009_00-EDENCOMPOSITEBLT.pkg` | `da50b62751e3a8399324636058dc76a0104dcc2e5a246a063e9bcff99e1184c0` | PASS: tiles slowly flipping, scrolling top bar, 5 FPS |
| `FGNM00011` | `IV0000-FGNM00011_00-EDENTRIWRAPPER00.pkg` | `fb621085e4b0bbd93ccc79340c5ce4f9c5231083fb8cdfc723db755b6f9d8dc7` | PASS: orange gradient triangle, 60 FPS |

Native macOS OpenOrbis rebuilds of the same advanced package matrix were
verified on 2026-07-04 without Docker:

```sh
USE_OPENGNM=1 OPENORBIS_BUILD_BACKEND=macos tools/build_triangle_pkg.sh
USE_OPENGNM=1 OPENORBIS_BUILD_BACKEND=macos tools/build_eden_composite_dma_pkg.sh
USE_OPENGNM=1 OPENORBIS_BUILD_BACKEND=macos tools/build_eden_composite_blit_pkg.sh
USE_OPENGNM=1 OPENORBIS_BUILD_BACKEND=macos tools/build_eden_triangle_wrapper_pkg.sh
USE_OPENGNM=1 OPENORBIS_BUILD_BACKEND=macos tools/build_cube_pkg.sh
```

On macOS, the wrappers use Homebrew `llvm@18`, the cached OpenOrbis v0.5.4
LLVM 18 SDK, `create-fself-macos`, `create-gp4`, and `PkgTool.Core`. Shader
packages use prebuilt `.sb` assets; `psbc` is only a reference/regeneration tool,
not a required dependency for the native package path. The cube sample also uses
the local workspace `cglm` checkout for matrix math headers.

| Title ID | Native macOS SHA-256 |
|---|---|
| `FGNM00000` | `7505ca8c1fd4cbbf0f0efcc3dc3329b963871093b56a5da49c3553c05597951c` |
| `FGNM00001` | `80b81cb4ee06ec092bdff40d7a8b78ac4149f982ec9f1613a15312c9db857eeb` |
| `FGNM00008` | `532e63dbdc25b787125346c32395764198f2666c9187a2fcb989c86bdb20619e` |
| `FGNM00009` | `0fba2ffe964ecf7045aab638466605814f7b21c9038d175d4d6a7f2ff02ab3c8` |
| `FGNM00011` | `ddae0b3efea02840dd8d2ab4bee5eb971b87610589ac76f1639d3ae300aa247b` |

These native macOS rebuilds were uploaded to `/data/pkg` on 2026-07-04 and
hardware-confirmed. The four earlier tests still behave as before. `FGNM00001`
shows a visible spinning textured cube at 60 FPS with about 2% CPU usage. The
cube build intentionally does not define `CGLM_FORCE_LEFT_HANDED`: the sample
places the cube at negative Z, so the left-handed projection variant produced a
white clear screen with no visible cube.

The cube `.sb` shader assets were generated once from the GLSL sources with
host `glslc` plus Linux `psbc` inside Docker because the macOS OpenOrbis package
path intentionally does not depend on `psbc`. Once those assets exist, the cube
package rebuild is fully native macOS OpenOrbis:

```sh
glslc -fshader-stage=vertex freegnm-examples/cube/assets/misc/cube.vert.glsl -o freegnm-examples/cube/assets/misc/cube.vert.spv
glslc -fshader-stage=fragment freegnm-examples/cube/assets/misc/clear.frag.glsl -o freegnm-examples/cube/assets/misc/clear.frag.spv
glslc -fshader-stage=fragment freegnm-examples/cube/assets/misc/cube.frag.glsl -o freegnm-examples/cube/assets/misc/cube.frag.spv
docker run --rm --platform linux/amd64 \
  --mount type=bind,source="$(pwd)",target=/work \
  -w /work openorbisofficial/toolchain:latest bash -lc '
set -e
cd /work/psbc
make -j"$(nproc)" \
  CFLAGS="-std=gnu11 -Wall -O2 -g -include alloca.h -include strings.h -I../freegnm -I../Vulkan-Headers/include -I../mesa/include -Iinclude/ -Isrc/ -Isrc/amd -Isrc/amd/common -Isrc/amd/compiler -Isrc/amd/vulkan -Isrc/compiler -Isrc/compiler/nir -Isrc/gallium/include -Isrc/mesa -Isrc/util -D_XOPEN_SOURCE=700 -DUTIL_ARCH_LITTLE_ENDIAN=1 -DUTIL_ARCH_BIG_ENDIAN=0 -DHAVE_STRUCT_TIMESPEC=1 -DHAVE_PTHREAD=1" \
  CXXFLAGS="-std=c++17 -Wall -O2 -g -I../freegnm -I../Vulkan-Headers/include -I../mesa/include -Iinclude/ -Isrc/ -Isrc/amd -Isrc/amd/common -Isrc/amd/compiler -Isrc/amd/vulkan -Isrc/compiler -Isrc/compiler/nir -Isrc/gallium/include -Isrc/mesa -Isrc/util -D_XOPEN_SOURCE=700 -DUTIL_ARCH_LITTLE_ENDIAN=1 -DUTIL_ARCH_BIG_ENDIAN=0 -DHAVE_STRUCT_TIMESPEC=1 -DHAVE_PTHREAD=1" \
  LDFLAGS="-lm -lpthread"
cd /work/freegnm-examples/cube
/work/psbc/psbc -s vertex -f assets/misc/cube.vert.spv -o assets/misc/cube.vert.sb
/work/psbc/psbc -s fragment -f assets/misc/clear.frag.spv -o assets/misc/clear.frag.sb
/work/psbc/psbc -s fragment -f assets/misc/cube.frag.spv -o assets/misc/cube.frag.sb
'
```

## Target Configuration Found

The active Codex config does not contain a PS4 host. The project scripts and
prior Codex session history use these defaults:

```sh
PS4_HOST=10.0.1.157
PS4_FTP_PORT=2121
PS4_PKG_DIR=/data/pkg
PS4DEBUG_HOST=10.0.1.157
PS4DEBUG_PYTHON=<PyPS4debug-venv>/bin/python
```

Existing staging scripts use FTP to upload packages to `/data/pkg` and
optionally use PS4debug to send a notification.

## Prerequisites

Host-side:

- Docker image: `openorbisofficial/toolchain:latest`
- Or native macOS:
  - Homebrew `llvm@18`
  - OpenOrbis v0.5.4 LLVM 18 SDK extracted by
    `../tools/setup_openorbis_llvm18_macos.sh`
  - SDK macOS helpers:
    `bin/macos/create-fself-macos`, `bin/macos/create-gp4`, `bin/macos/PkgTool.Core`
- Repository root: `<opengnm-stack-root>`
- OpenOrbis helper source directories, if the Docker image does not provide
  compatible helper binaries:
  - `../OpenOrbis/create-fself`
  - `../OpenOrbis/create-gp4`
- Go toolchain on the host if the helper binaries must be rebuilt.

PS4-side:

- PS4 reachable at `10.0.1.157`.
- FTP server running on port `2121`.
- Package install path `/data/pkg` exists or can be created by FTP.
- PS4debug running if process capture/notification is desired.

## Existing Package Pattern

The known-good local examples use this structure:

```text
sample/
├── eboot.bin
├── pkg.gp4
├── sce_module/
│   ├── libc.prx
│   └── libSceFios2.prx
├── sce_sys/
│   ├── about/right.sprx
│   ├── icon0.png
│   └── param.sfo
└── IV0000-...pkg
```

Reference files:

- `freegnm-examples/videoout-linear/Makefile`
- `tools/build_videoout_linear_pkg.sh`
- `tools/stage_videoout_linear_pkg.sh`

The packaging flow is:

1. Compile the executable for `x86_64-ps4-elf`.
2. Link with OpenOrbis startup objects and `link.x`.
3. Convert the executable to `eboot.bin` with `create-fself`.
4. Copy runtime modules from the SDK.
5. Generate `sce_sys/param.sfo` with `PkgTool.Core`.
6. Generate `pkg.gp4` with `create-gp4`.
7. Build the final `.pkg` with `PkgTool.Core pkg_build`.
8. Validate the package with `PkgTool.Core pkg_validate`.

## opengnm Hardware Smoke Metadata

Use a new title id/content id so it does not collide with `freegnm-examples`.

```make
TITLE=opengnm Hardware Smoke
VERSION=1.00
TITLE_ID=OGNM00001
CONTENT_ID=IV0000-OGNM00001_00-OPENGNMHWSMOKE00
EXE=opengnm_hw_smoke
PKG=$(CONTENT_ID).pkg
```

The title id is intentionally different from the `FGNMxxxxx` examples because
this package validates the `opengnm` Sony-style ABI surface, not the `freegnm`
`gnm*` wrapper API.

## Build Commands Inside the OpenOrbis Container

The current ELF target already works:

```sh
cd /work/opengnm
make hardware-smoke
```

For package generation, the link step should use OpenOrbis startup files and
`link.x`, not the minimal `ld.lld -e main` smoke link:

```sh
ld.lld -o opengnm_hw_smoke \
  tests/hardware_smoke.o libopengnm.a \
  -m elf_x86_64 -pie --script "$OO_PS4_TOOLCHAIN/link.x" \
  --eh-frame-hdr -L"$OO_PS4_TOOLCHAIN/lib" \
  -lc -lkernel -lSceGnmDriver -lSceVideoOut \
  "$OO_PS4_TOOLCHAIN/lib/crt1.o" \
  "$OO_PS4_TOOLCHAIN/lib/crti.o" \
  "$OO_PS4_TOOLCHAIN/lib/crtn.o"
```

Then create `eboot.bin`:

```sh
create-fself \
  -in=opengnm_hw_smoke \
  -out=opengnm_hw_smoke.oelf \
  -eboot=eboot.bin \
  --paid 0x3800000000000011
```

Copy runtime modules:

```sh
mkdir -p sce_module
cp "$OO_PS4_TOOLCHAIN/bin/data/modules/libc.prx" sce_module/
cp "$OO_PS4_TOOLCHAIN/bin/data/modules/libSceFios2.prx" sce_module/
```

For the v0.5.4 LLVM 18 native SDK, the same PRXs live under:

```sh
$OO_PS4_TOOLCHAIN/src/modules/libc.prx
$OO_PS4_TOOLCHAIN/src/modules/libSceFios2.prx
```

`Makefile` exposes this as `RUNTIME_MODULE_DIR`; the native macOS wrapper sets
it to `$OO_PS4_TOOLCHAIN/src/modules`.

Create `sce_sys` files. `icon0.png` and `right.sprx` can be copied from the
existing local smoke example until opengnm has its own assets:

```sh
mkdir -p sce_sys/about
cp /work/freegnm-examples/videoout-linear/sce_sys/icon0.png sce_sys/icon0.png
cp /work/freegnm-examples/videoout-linear/sce_sys/about/right.sprx sce_sys/about/right.sprx
```

Generate `param.sfo`:

```sh
PkgTool.Core sfo_new sce_sys/param.sfo
PkgTool.Core sfo_setentry sce_sys/param.sfo APP_TYPE --type Integer --maxsize 4 --value 1
PkgTool.Core sfo_setentry sce_sys/param.sfo APP_VER --type Utf8 --maxsize 8 --value "1.00"
PkgTool.Core sfo_setentry sce_sys/param.sfo ATTRIBUTE --type Integer --maxsize 4 --value 0
PkgTool.Core sfo_setentry sce_sys/param.sfo CATEGORY --type Utf8 --maxsize 4 --value "gd"
PkgTool.Core sfo_setentry sce_sys/param.sfo CONTENT_ID --type Utf8 --maxsize 48 --value "IV0000-OGNM00001_00-OPENGNMHWSMOKE00"
PkgTool.Core sfo_setentry sce_sys/param.sfo DOWNLOAD_DATA_SIZE --type Integer --maxsize 4 --value 0
PkgTool.Core sfo_setentry sce_sys/param.sfo SYSTEM_VER --type Integer --maxsize 4 --value 0
PkgTool.Core sfo_setentry sce_sys/param.sfo TITLE --type Utf8 --maxsize 128 --value "opengnm Hardware Smoke"
PkgTool.Core sfo_setentry sce_sys/param.sfo TITLE_ID --type Utf8 --maxsize 12 --value "OGNM00001"
PkgTool.Core sfo_setentry sce_sys/param.sfo VERSION --type Utf8 --maxsize 8 --value "1.00"
```

Generate `pkg.gp4` and build the package:

```sh
PKG_FILES="eboot.bin sce_sys/about/right.sprx sce_sys/param.sfo sce_sys/icon0.png sce_module/libc.prx sce_module/libSceFios2.prx"

create-gp4 \
  -out=pkg.gp4 \
  -content-id=IV0000-OGNM00001_00-OPENGNMHWSMOKE00 \
  -files "$PKG_FILES"

PkgTool.Core pkg_build pkg.gp4 .
PkgTool.Core pkg_validate --verbose IV0000-OGNM00001_00-OPENGNMHWSMOKE00.pkg
```

## Docker Wrapper Shape

The wrapper should follow the existing `tools/build_videoout_linear_pkg.sh`
pattern:

1. Set `ROOT_DIR` to the parent workspace.
2. Use `OPENORBIS_DOCKER_IMAGE`, defaulting to
   `openorbisofficial/toolchain:latest`.
3. Build Linux `create-fself` and `create-gp4` helper binaries from
   `../OpenOrbis` if available.
4. Mount the repository at `/work`.
5. Set `OO_PS4_TOOLCHAIN=/lib/OpenOrbisSDK` inside the container if using the
   existing example scripts, or `/usr/lib/OpenOrbisSDK` if using the current
   `opengnm/build.sh` image path.
6. Run the package make target from `/work/opengnm`.

Use `--platform linux/amd64` for the current Docker image on Apple Silicon to
avoid platform ambiguity.

## Native macOS Wrapper

The native wrapper avoids Docker and Ubuntu package setup:

```sh
cd <opengnm-root>
./build.sh macos-hardware-pkg
```

It does the following:

1. Locates Homebrew `llvm@18`, or uses `LLVM18_PREFIX`.
2. Locates the OpenOrbis v0.5.4 LLVM 18 SDK, or runs
   `../tools/setup_openorbis_llvm18_macos.sh` if `OO_PS4_TOOLCHAIN` is unset.
3. Writes `config.mak` with Homebrew `clang`, `ld.lld`, and `llvm-ar`.
4. Uses SDK macOS helpers:
   `create-fself-macos`, `create-gp4`, and `PkgTool.Core`.
5. Uses `$OO_PS4_TOOLCHAIN/src/modules` for runtime PRXs.
6. Builds and validates `IV0000-OGNM00001_00-OPENGNMHWSMOKE00.pkg`.

Useful overrides:

```sh
LLVM18_PREFIX=/opt/homebrew/opt/llvm@18
OO_PS4_TOOLCHAIN=/path/to/OpenOrbis/PS4Toolchain
RUNTIME_MODULE_DIR=/path/to/sce_module_prx_dir
```

## Staging the Package to PS4

Upload by FTP:

```sh
ROOT_DIR=<opengnm-stack-root>
PKG="$ROOT_DIR/opengnm/IV0000-OGNM00001_00-OPENGNMHWSMOKE00.pkg"
PS4_HOST="${PS4_HOST:-10.0.1.157}"
PS4_FTP_PORT="${PS4_FTP_PORT:-2121}"
PS4_PKG_DIR="${PS4_PKG_DIR:-/data/pkg}"

curl --fail --silent --show-error --ftp-create-dirs \
  -T "$PKG" \
  "ftp://$PS4_HOST:$PS4_FTP_PORT$PS4_PKG_DIR/$(basename "$PKG")"

curl --fail --silent --show-error \
  "ftp://$PS4_HOST:$PS4_FTP_PORT$PS4_PKG_DIR/" | grep -F "$(basename "$PKG")"
```

Optionally notify through PS4debug:

```sh
PS4DEBUG_PYTHON=<PyPS4debug-venv>/bin/python

"$PS4DEBUG_PYTHON" - "$PS4_HOST" "$PS4_PKG_DIR/$(basename "$PKG")" <<'PY'
import asyncio
import sys
from ps4debug import PS4Debug

async def main() -> None:
    host, path = sys.argv[1], sys.argv[2]
    await PS4Debug(host).notify(f"opengnm hardware smoke PKG staged: {path}")

asyncio.run(main())
PY
```

## Installing and Running

After staging:

1. Install `IV0000-OGNM00001_00-OPENGNMHWSMOKE00.pkg` from the PS4 package
   installer.
2. Launch title id `OGNM00001`.
3. Watch for the console app output:

```text
opengnm hardware smoke passed: dcb_size=<bytes>
```

## Expected Result

Success:

- Package uploads to `ftp://10.0.1.157:2121/data/pkg/`.
- Package installs.
- Title `OGNM00001` launches.
- The smoke executable stays on a full-screen green status view.
- The scrolling white bar at the top keeps moving.
- The large center digit is `0`, meaning the EOP label was written after
  `sceGnmSubmitCommandBuffers` and `sceGnmSubmitDone`.
- The app logs `opengnm hardware smoke passed`.

Failure status digits:

- `1`: GNM direct-memory allocation failed.
- `2`: `sceGnmSubmitCommandBuffers` failed.
- `3`: `sceGnmSubmitDone` failed.
- `4`: EOP label timeout.
- `5`: GNM direct-memory map failed.

Failure modes to record:

- FTP connection refused or timed out: PS4 FTP server is not running or target IP
  changed.
- Package install fails: inspect `param.sfo`, `CONTENT_ID`, runtime modules, and
  package validation output.
- Launch crashes before submit: inspect OpenOrbis startup/link flags and missing
  runtime modules.
- Submit fails: record `sceGnmSubmitCommandBuffers` return value.
- SubmitDone fails: record `sceGnmSubmitDone` return value.
- EOP timeout: GPU accepted submission but did not write the label; inspect DCB
  content and event packet encoding.

## Commit/Artifact Policy

Do commit:

- Build scripts.
- Makefile package targets.
- Source files.
- This runbook.

Do not commit:

- `eboot.bin`
- `*.oelf`
- `*.pkg`
- Generated `pkg.gp4`
- Generated `sce_sys/param.sfo`
- Copied runtime `.prx` files

## freegnm-example spinning cube (OpenGNM)
- `tools/build_cube_pkg.sh` packages the `cube` sample through native macOS OpenOrbis tooling with:
  - `freegnm-examples/cube/IV0000-FGNM00001_00-CUBESAMPLE000000.pkg`
  - Title `FGNM00001`
  - Content ID `IV0000-FGNM00001_00-CUBESAMPLE000000`
  - Use-command:
    - `OPENORBIS_BUILD_BACKEND=macos tools/build_cube_pkg.sh`
- Runtime artifact hash after staging:
    - `b99208b2d8145b78f85603c0ba8be2475296f5f55146f71cb5aef3c9c7c9d576`
