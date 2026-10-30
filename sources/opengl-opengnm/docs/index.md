# opengnm

<p style="font-size:1.2em; color: var(--md-primary-fg-color);">
A clean-rewrite PS4 GNM library providing <code>sceGnm*</code> / <code>sceGpa*</code>
drop-in API compatibility with the Sony official PS4 SDK.
</p>

---

## Overview

**opengnm** is a from-scratch implementation of the PlayStation 4 Graphics Next
Module (GNM) library. Any PS4 app or game written against the official Sony SDK
headers should compile and link against opengnm **unmodified**.

The library exposes the full `sceGnm*` API surface — **207+ functions** matching
the Sony SDK ABI — plus the `sceGpa*` surface computation API (AMD AddrLib
equivalent), a GCN fetch-shader assembler, and PM4 command buffer packet
builders.

## Key Features

- **Full `sceGnm*` API surface** — 207 functions matching the Sony SDK ABI
- **OpenOrbis SDK compatible** — builds with the OpenOrbis PS4 toolchain
- **Two backends:**
    - `orbis` — delegates to the official `libSceGnmDriver` firmware
    - `generic` — pure software PM4 emission for host testing (no PS4 needed)
- **Binary-compatible struct layouts** — `_Static_assert` verified sizes
- **Surface computation** — `sceGpa*` (gpuaddr / AddrLib) for RT/texture sizing
- **GCN assembler** — fetch shader generation
- **PM4 encoding** — command buffer packet building
- **freegnm source compatibility** — `<compat/freegnm.h>` maps `gnm*` wrapper
  calls to `sceGnm*` without exporting a second ABI

## Quick Start

### Host build (generic, for testing)

```sh
cmake -B build -DCMAKE_BUILD_TYPE=Debug -DOPENGNM_PLATFORM=generic
cmake --build build
```

### PS4 build (OpenOrbis)

```sh
export OO_PS4_TOOLCHAIN=/path/to/openorbis
cp config.orbis.mak config.mak
make
make install DESTDIR=$OO_PS4_TOOLCHAIN
```

Link your PS4 target with:

```sh
-lopengnm -lkernel -lSceGnmDriver -lSceVideoOut
```

See [Getting Started](getting-started.md) for full build instructions.

## Documentation Sections

| Section | Description |
|---------|-------------|
| [Getting Started](getting-started.md) | Build, install, and link opengnm |
| [Architecture](architecture.md) | Subsystem overview and data flow |
| [Backends](backends.md) | orbis vs generic backend comparison |
| [Guides](guides/index.md) | Tutorial-style walkthroughs for each subsystem |
| [API Reference](reference/index.md) | Complete function, struct, and enum reference |
| [Examples](examples/index.md) | Code examples and walkthroughs |
| [freegnm Compatibility](freegnm-compat.md) | Source-level compatibility with the old freegnm wrapper API |

## API at a Glance

| Subsystem | Header | Functions | Description |
|-----------|--------|-----------|-------------|
| Core Types | `gnm_types.h` | — | 51 enums, constants, limits |
| Error Handling | `gnm_error.h` | 4 | Error codes, message handler |
| Data Formats | `gnm_dataformat.h` | 44 | `GnmDataFormat`, format constants |
| Buffer | `gnm_buffer.h` | 12 | `GnmBuffer` descriptor |
| Texture | `gnm_texture.h` | 23 | `GnmTexture` (0x20 bytes) |
| Sampler | `gnm_sampler.h` | 1 | `GnmSampler` (0x10 bytes) |
| Render Target | `gnm_rendertarget.h` | 11 | `GnmRenderTarget` (0x40 bytes) |
| Depth RT | `gnm_depthrendertarget.h` | 28 | `GnmDepthRenderTarget` (0x34 bytes) |
| Control Registers | `gnm_controls.h` | — | Blend, depth-stencil, primitive setup |
| Shader | `gnm_shader.h` | 10 | Stage registers, fetch shader |
| Shader Binary | `gnm_shaderbinary.h` | 14 | VS/PS shader containers |
| Command Buffer | `gnm_commandbuffer.h` | 3 | `GnmCommandBuffer` (0x40 bytes) |
| Draw Command Buffer | `gnm_drawcommandbuffer.h` | 63 | `sceGnmDrawCmd*` functions |
| Driver Runtime | `gnmdriver.h` | 265 | `sceGnm*` runtime (207+ public + stubs) |
| Surface Computation | `gpuaddr.h` | 19 | `sceGpa*` / AddrLib |
| Platform | `platform.h` | 3 | GPU mode, buffer label |
| Helpers | `gnm_helpers.h` | 14 | VideoOut, direct memory, validation |
| Strings | `gnm_strings.h` | 22 | Enum-to-string converters |

## License

MIT, see `COPYING` in the repository.

## Sources

- Clean rewrite, based on the PS4 SDK ABI and AMD public documentation
- Sony ABI reference: `shadPS4/src/core/libraries/gnmdriver/`
- Surface computation: AMD PAL / Mesa AddrLib
