# Focused SPIR-V scalar constant cache — 2026-11-14

Upstream: [Lichii #1320](https://github.com/zortos293/Lichii/pull/1320), itself crediting Eglantine #825. Encore's shared native shader header was exact blob `002eff294162f6f44276d39017c9174dd37c2da2` and still called Sirit `Constant` for every scalar u32/f32 immediate.

This port is intentionally only the scalar-constant subchange: cache returned IDs by exact 32-bit value bits in two maps. It preserves +0/-0 and NaN payload distinctions and keeps integer/float type domains separate. No SSA representation, storage-buffer search, pipeline eviction, cache file format, async scheduling, descriptor policy or game-specific behavior changes.

Host proof extracts the exact helper method into a C++20 UBSan harness and checks duplicate reuse plus +0/-0, NaN payload and equal-bits/different-type separation. It does **not** execute the full shader compiler or prove identical complete SPIR-V yet. Engine integration must run the existing shared shader compiler syntax/ABI gates and, before a performance claim, compare actual emitted SPIR-V on the retained corpus. PS5/FC27 runtime unverified.
