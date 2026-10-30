# Getting Started

This guide covers building, installing, and linking opengnm for both host
testing (generic backend) and PS4 deployment (orbis backend).

---

## Prerequisites

### Host build (generic)

- CMake >= 3.15
- A C compiler (GCC, Clang, or MSVC)
- Make (optional, for Makefile build)

### PS4 build (orbis)

- [OpenOrbis PS4 Toolchain](https://github.com/OpenOrbis/openorbis-ps4-toolchain)
- Clang with PS4 target support
- CMake >= 3.15 (optional) or Make

---

## Building

### Option 1: CMake (recommended)

#### Host (generic, for testing)

```sh
cmake -B build -DCMAKE_BUILD_TYPE=Debug -DOPENGNM_PLATFORM=generic
cmake --build build
```

Run the test suite:

```sh
cd build && ctest
```

#### PS4 (OpenOrbis)

```sh
cmake -B build -DCMAKE_BUILD_TYPE=Release -DOPENGNM_PLATFORM=orbis \
    -DCMAKE_TOOLCHAIN_FILE=$OO_PS4_TOOLCHAIN/cmake_toolchain/openorbis.cmake
cmake --build build
```

### Option 2: Makefile

#### Host (generic)

```sh
cp config.generic.mak config.mak
make
```

#### PS4 (OpenOrbis)

```sh
export OO_PS4_TOOLCHAIN=/path/to/openorbis
cp config.orbis.mak config.mak
make
make install DESTDIR=$OO_PS4_TOOLCHAIN
```

### Option 3: Docker

```sh
./build.sh docker-build          # OpenOrbis build + link/hardware-smoke ELFs
./build.sh docker-link-smoke     # PS4-target link smoke only
./build.sh docker-hardware-smoke # PS4 hardware-smoke ELF only
./build.sh docker-hardware-pkg   # PS4 hardware-smoke package
./build.sh stage-hardware-pkg    # build/upload package to the configured PS4
./build.sh tests                 # build + run host tests
```

---

## Installation

### CMake install

```sh
cmake --install build --prefix /usr/local
```

This installs:

- Headers to `include/`
- Static library to `lib/`
- CMake package config to `lib/cmake/opengnm/`
- pkg-config file to `lib/pkgconfig/`

### Makefile install (PS4)

```sh
make install DESTDIR=$OO_PS4_TOOLCHAIN
```

---

## Linking

### CMake

```cmake
find_package(opengnm CONFIG REQUIRED)
target_link_libraries(my_renderer PRIVATE opengnm::opengnm)
```

### pkg-config

```sh
cc $(pkg-config --cflags opengnm) -c renderer.c
cc renderer.o $(pkg-config --libs --static opengnm)
```

### PS4 manual linking

When linking a PS4 target, link `opengnm` together with the firmware libraries
it delegates to:

```sh
-lopengnm -lkernel -lSceGnmDriver -lSceVideoOut
```

The Orbis backend forwards submit and flip paths to firmware-provided
`sceGnmSubmit*` exports in `libSceGnmDriver`. The generic backend provides host
test no-op implementations, but an Orbis archive is expected to keep those
symbols resolved by the platform SDK libraries.

---

## Using the API

### Include the master header

```c
#include <gnm.h>
```

This pulls in the complete API surface. Or include individual headers for
specific subsystems:

```c
#include <gnm_types.h>          // Core types and enums
#include <gnm_drawcommandbuffer.h>  // Draw command buffer building
#include <gnmdriver.h>          // sceGnm* runtime functions
#include <gpuaddr.h>            // Surface computation
#include <gnm_helpers.h>        // Convenience helpers
```

### freegnm compatibility

If your code used the old `gnm*` wrapper API:

```c
#include <compat/freegnm.h>
```

This maps `gnm*` names to `sceGnm*` via preprocessor defines — no second ABI is
exported. See [freegnm Compatibility](freegnm-compat.md) for details.

---

## Platform Selection

The `OPENGNM_PLATFORM` CMake variable controls which backend is compiled:

| Value | Backend | Description |
|-------|---------|-------------|
| `auto` | (detected) | Auto-detects based on toolchain |
| `orbis` | Orbis | Delegates to firmware `libSceGnmDriver` |
| `generic` | Generic | Pure software PM4 emission for host testing |

When `auto` is selected, opengnm checks for `ORBIS`, `PLATFORM_PS4`, or
`CMAKE_SYSTEM_NAME STREQUAL "OpenOrbis"` to determine the platform.

---

## Verification

### Host tests

The generic backend includes a comprehensive test suite (54+ tests):

```sh
cmake --build build && cd build && ctest --verbose
```

Tests cover:

- Surface computation (`test_surface.c`)
- Draw command buffer building (`test_drawcmd.c`)
- Command buffer validation (`test_validate.c`)
- API surface completeness (`test_api.c`)
- freegnm compatibility (`test_compat.c`)
- Helper functions (`test_helpers.c`)
- PM4 encoding (`test_pm4.c`)

### PS4 hardware smoke

The verified PS4 hardware smoke result is a full-screen green status view with
scrolling white bar and digit `0`, confirming VideoOut presentation and the GNM
submit/EOP path. See [Hardware Smoke Test](examples/hardware-smoke.md).
