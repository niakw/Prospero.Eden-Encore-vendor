# DLSS-NR Min: isolated ViT pool candidate

DRAFT RESEARCH ONLY — disabled by default, do not merge into vendor main.

Engine context: https://github.com/niakw/ProsperoEden-Encore/pull/43

The native R09 frame session's MakeVitBuffers function now has an
explicit compile-time-gated candidate nine-buffer pool, only if
EDEN_R09_MIN_VIT_POOL_EXPERIMENTAL=1 is deliberately passed in a research
build. The macro is not defined by this repository; the default compiled
path preserves the original 65-buffer allocator under its #else branch.

All nine buffers belong to one FrameSession::State; no buffers are shared
between sessions. Each ViT block has distinct buffer handles for input,
hidden, ffwd, attention, output, scratch, query, key and value. The pool
contains two ping-pong input/output buffers and seven further independent
temporary buffers. The previous output exactly matches the next input.

Requested ViT buffer bytes, theoretical:

| Raster | Existing allocations | Candidate |
| --- | ---: | ---: |
| 1920x1080 (2560 tokens) | 462.5 MiB, 65 buffers | 60 MiB, 9 buffers |
| 1280x720 (960 tokens) | 173.4375 MiB, 65 buffers | 22.5 MiB, 9 buffers |

These figures are logical requests, not VRAM residency, freed GPU memory,
measured frame time, or whole R09 graph allocations. The real model still
uses 1080p fixed geometry; 720p is only a structural candidate.

Pinned source audit and host Vulkan tests in the engine PR verify that
the native ViT run registers blocks sequentially and that each terminal
residual epilogue records a compute-to-compute memory barrier. A test with
one missing barrier correctly triggers Khronos SYNC-HAZARD messages under
llvmpipe. This is not a proof of the real model's full GPU lifetime,
negative rollback behavior, fence ownership, or PS5-driver correctness.

The independent host workflow compiles positive/negative role-map tests
under GCC/Clang with UBSan, and verifies the unchanged default allocation
body. It does NOT compile the vendor's complete frame-session translation
unit or execute a native PS5 build.

Integration gates:
- Keep this vendor PR in draft and do not alter main or Encore's vendor pin.
- Do not enable the experimental macro in any shipping or normal PS5 build.
- Verify full C++ integration on an isolated engine development build.
- Test actual model weights, guest frame output, Vulkan fence retirement,
  rollback, VRAM, image quality, and frame pacing on PS5 before enablement.


## Exact allocator-method host compilation

The vendor CI additionally extracts MakeVitBuffers verbatim from the
checked-in nr_rdna2_frame_session.cpp, compiles its real guarded body
against fake descriptor/allocator types with BOTH macro settings,
and executes positive and negative tests under GCC/Clang UBSan. It checks
65 vs 9 calls, the exact byte totals, intra-block role uniqueness,
cross-block input/output identity, a failed allocation before publication,
and invalid zero-token rejection. This is more meaningful than testing
only the pure policy header, but it is still NOT a full native vendor
translation-unit compile or a real Vulkan memory-allocation test.
