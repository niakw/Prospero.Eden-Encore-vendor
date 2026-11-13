# PS5 G21 GPU renderer/channel teardown — upstream-first P0 safety candidate

Date 2026-11-14. Related [Eglantine PR #462](https://github.com/Eglantine-Community/Eglantine/pull/462), [Eglantine shutdown issue #181](https://github.com/Eglantine-Community/Eglantine/issues/181), [engine P0 #97](https://github.com/niakw/ProsperoEden-Encore/issues/97) and [PS5 shader/exit register](https://github.com/niakw/ProsperoEden-Encore/blob/main/docs/audits/PS5_2026-11-13_BOTH_REPOS_OPEN_WORK_REGISTER.md).

## Exact upstream bug and selected-PS5 comparison

Eglantine's sanitizer sweep #462 reports `GPU::Impl` destroying channel-owned Maxwell3D state **before** its Vulkan renderer: the renderer destructor flushes work/dirty tracking and dereferences channel state already freed. Their upstream patch calls `renderer.reset()` inside `~Impl()` so it executes before destruction of members. Its tests were host ASan/TSan, not PS5 13.60.

Our active PS5 source is **not** the legacy `sources/eden/src/video_core/gpu.cpp` even though that legacy file also has `~Impl() = default`. The real CMake G21 selection uses one of:

- `encore-active/eden-core/src/video_core/gpu_vulkan.cpp` for Vulkan PS5.
- `encore-active/eden-core/src/video_core/gpu_gl_native.cpp` for native OpenGL.
- `encore-active/eden-core/src/video_core/gpu_gl_host.cpp` for host OpenGL.

All three declare `renderer` **before** `scheduler`/`channels` and `gpu_thread` **last**. Thus reverse C++ member destruction stops the worker, then destroys the channels, and only later destroys the renderer. This is precisely the resource-lifetime hazard described upstream; it is independent of the user's SIGSEGV address, which has not yet been symbolized to this destructor.

## PS5 adaptation

- Vulkan G21: `~Impl()` now calls `NotifyShutdown()`, `gpu_thread.Stop()`, then `renderer.reset()` **before member destruction**.
- OpenGL G21 native and host: preserve existing `gpu_thread.Stop()` and GL context `MakeCurrent(); glFinish();`, then destroy the renderer explicitly while channels remain alive; `NotifyShutdown()` wakes pending waiters before worker join.
- Never free the renderer while the worker is alive: our source's worker is the last declared member; the desktop upstream one-liner alone would introduce a GPU race here. No new per-frame work or API, no modification of immutable `sources/eden`, no TitleID-specific branch.

## Host tests and limits

`tests/ps5_gpu_renderer_channel_teardown_host.py` reads real maintained G21 destructor bodies, verifies exactly one stop, notify and renderer reset in that order, source declarations still have renderer before channels and worker last, then compiles each selected destructor against a lifetime-sensitive C++20 ASan/UBSan fixture. The stub renderer destructor checks channel lifetime, stopped GPU worker and notification; GL also verifies current context + glFinish before reset. The fixture is **not** a real Vulkan renderer or a PlayStation 5 runtime.

Before merge: pair this vendor PR with an engine CMake source-owner proof, update current source SHA receipts without weakening frozen hashes, host P0–P4 CI, inspect native compiler warnings and package, then on PS5 FW 13.60 repeatedly start/exit NS1 FC27 and NS2 MKW/Bananza, including app exit via Circle, crash-handler shutdown and several relaunches. Resolve issue #97 only after exact before/after native crash signatures show no SIGSEGV for multiple cycles; do not claim a direct match with external-library PC address without symbolization.

**Status:** source-only P0 correction candidate, full PS5 native compile/console validation pending. No merge or branch cleanup before acceptance.
