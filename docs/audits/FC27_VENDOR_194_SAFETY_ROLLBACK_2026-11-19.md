# FC27 — PR #194 controlled safety rollback (19 November 2026)

## Evidence and regression boundary
The physical PS5 FC27 test reported in `logs19111615.zip` used engine `4a5b8cc05a662130b9033f7d9b97869c8df3310a`, vendor `8e564f61e6c8b86149ce0a0d6d2aa323a268dda6`: gameplay works but player-jersey texture defects have newly appeared, while HUD latency and frame pacing are still bad. The earlier FC27 hardware sample used vendor `9a83a6ba1cb464c9e5d14457cab18ad511c1104b`. Exact source comparison reveals our [vendor PR #194](https://github.com/niakw/Prospero.Eden-Encore-vendor/pull/194) as the principal intervening *render/memory-path* change: deferred page-protection decrements from `OnCPUWrite` after releasing `buffer_cache.mutex`.

[Upstream Lichii PR #1518](https://github.com/zortos293/Lichii/pull/1518) explicitly limits deferral to threads not owning the GPU scheduler (`!GpuSplit::OwnsScheduler()`) and tests remap/unmap sequencing. Encore's adaptation does **not** have a proven equivalent of the scheduler-owner exclusion, notwithstanding its published-unmap counter and previous helper unit tests. Therefore source correctness of the adaptation cannot be inferred merely from upstream or a CI green state.

## Deliberate production rollback
Restore the exact pre-#194 synchronous page-protection path from the earlier, PS5-exercised vendor:
- Selected `encore-active/eden-vulkan/vulkan_rasterizer.cpp`, Git blob `01c6880e5a8836da45601b0a3cb7c0fd6c46cf35`.
- Selected `encore-active/eden-core/overrides/core/device_memory_manager.inc`, Git blob `daf493ce6e863397ec44bfae467c4f7533570477`.
- Remove the deferral-only header `deferred_cached_updates.h`, which must no longer be compiled or hidden behind an experimental runtime branch.
- Retain all other vendor and engine fixes, especially NS2 loader index-0 RomFS, JIT uniform 16MiB ABI, N60 dense/sparse `ImagePages::Mark` active texture registrations (blob `744bf30e1421dcce212146f7e26e9d90558676a8`), GPU sibling data preservation and shader/texture work.

The formerly named `tests/fc27_deferred_page_protection_host.py` is repurposed to **disallow** silent reintroduction. It verifies the exact selected C++ `OnCPUWrite` method at host C++20 O0/O2 with UBSan and asserts all buffer/texture/shader invalidation cases and retained active image ownership. This preserves historical CI call sites, not an experimental implementation.

## Validation and exit criteria
This is a precautionary removal of a strongly temporally correlated new optimization, **not yet a demonstrated image repair**. Validate vendor source test, engine source CMake and historical backport gates, native PS5 ELF/package before device installation. On PS5 compare *the same FC27 match and camera* with previous user report: jersey texture, menu/HUD/clock/player names, intro video, worst frame stalls, first match cold and warm, same-process title exit/relaunch. A performance regression from reverting the deferral is possible; correctness precedes a speculative speed gain. NS2 first-frame boot remains open and requires independent SVC+GPFIFO milestones. No downstream behavior should be labeled repaired from host CI.
