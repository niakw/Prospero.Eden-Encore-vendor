# opengnm

opengnm is a clean-rewrite PS4 GNM library providing `sceGnm*` / `sceGpa*` drop-in
API compatibility with the Sony official PS4 SDK.

Any PS4 app or game written against the official SDK headers should compile and link
against opengnm unmodified.

## Features

- **Full `sceGnm*` API surface** — 207 functions matching the Sony SDK ABI
- **OpenOrbis SDK compatible** — builds with the OpenOrbis PS4 toolchain
- **Two backends:**
  - `orbis` — delegates to the official `libSceGnmDriver` firmware (74 real
    externs + 14 `sceGnmDriver*` forwarding wrappers + 172 retail stubs +
    11 validate stubs)
  - `generic` — pure software PM4 emission for host testing (no PS4 needed,
    14 PM4 packet builders + real `sceGnm*` + 172 stubs + 11 validate stubs)
- **Binary-compatible struct layouts** — `_Static_assert` verified sizes
- **Surface computation** — `sceGpa*` (gpuaddr / AddrLib) for RT/texture sizing
- **GCN assembler** — fetch shader generation
- **PM4 encoding** — command buffer packet building
- **Firmware-captured hardware init** — `DrawInitDefaultHardwareState` uses
  real PS4 firmware sequences (from shadPS4 RE) with FW version-specific
  (base/1.75/2.00/3.50) and Neo/OG/Neo-compat variants
- **Opt-in freegnm source compatibility** — `<compat/freegnm.h>` and
  `<gnm/...>` forwarding headers map compatible `gnm*` / `gpa*` wrapper calls to
  `sceGnm*` without exporting a second ABI. PSSL and GNF tooling types are
  provided via `<compat/pssl.h>` and `<compat/gnf.h>` with `<gnm/pssl/types.h>`
  and `<gnm/gnf/gnf.h>` forwarding headers.

## Status

**Feature-complete.** All planned phases (0-5D), TODO cleanup, RPCSX
cross-audit, and downstream migration are done. The OpenOrbis Docker build,
PS4-target link smoke, PS4 hardware-smoke package build, FTP staging, and PS4
hardware run all pass. All 207+ `sceGnm*` functions are implemented across
both backends, and the generic host backend passes 88 tests via CMake/CTest
and Makefile. Zero TODO/FIXME comments remain in the source.

Hardware init uses firmware-captured register sequences from shadPS4 RE data,
with FW version-specific (base/1.75/2.00/3.50) and Neo/OG/Neo-compat variants.
The gpuaddr tiling implementation was cross-audited against RPCSX with no gaps
found.

All freegnm-examples (triangle, cube, shader-test, eden-composite-blit,
eden-composite-dma, eden-triangle-wrapper, gltf, indirect, instances) build
with `USE_OPENGNM=1` via the compat layer. The companion shader compiler
`opengnm-psbc` is complete and hardware-validated on PS4 FW 9.00.

See `OPENGNM_REWRITE_PLAN.md` for the full plan and progress.

## Building

### Host (generic, for testing)

```sh
cmake -B build -DCMAKE_BUILD_TYPE=Debug -DOPENGNM_PLATFORM=generic
cmake --build build
```

Or with Make:

```sh
cp config.generic.mak config.mak
make
```

### PS4 (OpenOrbis)

```sh
export OO_PS4_TOOLCHAIN=/path/to/openorbis
cp config.orbis.mak config.mak
make
make install DESTDIR=$OO_PS4_TOOLCHAIN
```

When linking a PS4 target, link `opengnm` together with the firmware libraries
it delegates to:

```sh
-lopengnm -lkernel -lSceGnmDriver -lSceVideoOut
```

The Orbis backend forwards submit and flip paths to firmware-provided
`sceGnmSubmit*` exports in `libSceGnmDriver`. The generic backend provides host
test no-op implementations, but an Orbis archive is expected to keep those
symbols resolved by the platform SDK libraries.

### Installed Consumers

OpenGNM installs a CMake package and pkg-config file:

```cmake
find_package(opengnm CONFIG REQUIRED)
target_link_libraries(my_renderer PRIVATE opengnm::opengnm)
```

```sh
cc $(pkg-config --cflags opengnm) -c renderer.c
cc renderer.o $(pkg-config --libs --static opengnm)
```

### Docker

```sh
./build.sh docker-build          # OpenOrbis build + link/hardware-smoke ELFs
./build.sh docker-link-smoke     # PS4-target link smoke only
./build.sh docker-hardware-smoke # PS4 hardware-smoke ELF only
./build.sh docker-hardware-pkg   # PS4 hardware-smoke package
./build.sh stage-hardware-pkg    # build/upload package to the configured PS4
./build.sh tests                 # build + run host tests
```

## freegnm Source Compatibility

Code that used the old wrapper API can either include `<compat/freegnm.h>` before
using `gnm*` names, or switch its include path to opengnm and keep core includes
such as `<gnm/drawcommandbuffer.h>`, `<gnm/platform.h>`, and
`<gnm/gpuaddr/gpuaddr.h>`. The compatibility layer is source-only: linked
objects still call the Sony SDK-style `sceGnm*` / `sceGpa*` symbols.

## Renderer Helper API

`<gnm_helpers.h>` provides small convenience APIs for renderer backends such as
bgfx:

- direct-memory allocation/mapping with a host fallback for tests
- VideoOut backbuffer layout helpers, plus Orbis registration/flip helpers when
  OpenOrbis VideoOut/libkernel headers are available
- 2D texture and color render-target descriptor setup with size/alignment output
- shader binary metadata extraction for vertex and pixel shader headers
- command-buffer range validation and diagnostic messages

## License

MIT, see [LICENSE](LICENSE).

## Sources

- Clean rewrite, based on the PS4 SDK ABI and AMD public documentation
- Sony ABI reference: `shadPS4/src/core/libraries/gnmdriver/`
- Firmware init sequences: `shadPS4/src/core/libraries/gnmdriver/gnmdriver_init.h`
  (captured from real PS4 firmware)
- Surface computation: AMD PAL / Mesa AddrLib
- gpuaddr cross-audit: RPCSX (independent AMD tiler implementation)
