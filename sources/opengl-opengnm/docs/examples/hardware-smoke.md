# Hardware Smoke Test

The PS4 hardware smoke test is the verified end-to-end test that confirms
VideoOut presentation and the GNM submit/EOP (End of Pipe) path work correctly
on real PS4 hardware.

---

## Result

The verified PS4 hardware smoke result is a **full-screen green status view**
with a scrolling white bar and digit `0`, confirming:

- VideoOut presentation is working
- The GNM submit path is functioning
- The EOP (End of Pipe) event completes successfully

---

## Build

### Docker build (recommended)

```sh
./build.sh docker-hardware-smoke   # PS4 hardware-smoke ELF only
./build.sh docker-hardware-pkg     # PS4 hardware-smoke package
./build.sh stage-hardware-pkg      # build/upload package to the configured PS4
```

### Manual build

```sh
export OO_PS4_TOOLCHAIN=/path/to/openorbis
cp config.orbis.mak config.mak
make
```

The hardware smoke test source is in `tests/hardware_smoke.c`.

---

## What It Tests

The smoke test exercises the complete rendering pipeline:

1. **VideoOut open** — `sceGnmVideoOutOpen` with default 1920x1080 config
2. **Direct memory allocation** — `sceGnmDirectMemoryAllocate` for display buffer
3. **Render target creation** — color RT with `GNM_TM_DISPLAY_LINEAR_ALIGNED`
4. **Command buffer init** — `sceGnmCmdInit` with a 256KB buffer
5. **Default hardware state** — `sceGnmDrawCmdInitDefaultHardwareState`
6. **Render target binding** — `sceGnmDrawCmdSetRenderTarget`
7. **Clear/fill** — fill the display buffer with green
8. **Submit** — `sceGnmSubmitAndFlipCommandBuffers`
9. **Submit done** — `sceGnmSubmitDone`
10. **Flip** — `sceGnmVideoOutSubmitFlipAndWait`

---

## Link Smoke Test

There is also a link smoke test (`tests/link_smoke.c`) that verifies the PS4
target links correctly against opengnm and the firmware libraries:

```sh
./build.sh docker-link-smoke   # PS4-target link smoke only
```

This confirms that all `sceGnm*` symbols resolve correctly when linking with:

```sh
-lopengnm -lkernel -lSceGnmDriver -lSceVideoOut
```

---

## CI Pipeline

The Docker-based build pipeline supports:

| Command | Description |
|---------|-------------|
| `./build.sh docker-build` | OpenOrbis build + link/hardware-smoke ELFs |
| `./build.sh docker-link-smoke` | PS4-target link smoke only |
| `./build.sh docker-hardware-smoke` | PS4 hardware-smoke ELF only |
| `./build.sh docker-hardware-pkg` | PS4 hardware-smoke package |
| `./build.sh stage-hardware-pkg` | Build and upload package to the configured PS4 |
| `./build.sh tests` | Build and run host tests |

---

## Host Tests

In addition to the hardware smoke test, the generic backend passes 54+ tests
via CMake/CTest:

```sh
cmake -B build -DCMAKE_BUILD_TYPE=Debug -DOPENGNM_PLATFORM=generic
cmake --build build
cd build && ctest --verbose
```

Test files:

| File | Tests |
|------|-------|
| `test_surface.c` | Surface computation and tiling |
| `test_drawcmd.c` | Draw command buffer building |
| `test_validate.c` | Command buffer validation |
| `test_api.c` | API surface completeness |
| `test_compat.c` | freegnm compatibility |
| `test_helpers.c` | Helper functions |
| `test_pm4.c` | PM4 encoding |

---

## See Also

- [Getting Started](../getting-started.md) — full build instructions
- [Rendering Pipeline Guide](../guides/rendering-pipeline.md) — pipeline walkthrough
- [Backends](../backends.md) — orbis vs generic backend details
