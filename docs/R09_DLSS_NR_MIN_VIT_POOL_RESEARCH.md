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
| 1920x1080 (2560 tokens) | 462.5 MiB, 65 buffers | 37.5 MiB, 9 buffers |
| 1280x720 (960 tokens) | 173.4375 MiB, 65 buffers | 14.0625 MiB, 9 buffers |

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


## Exhaustive host failure injection

The extracted native MakeVitBuffers method is also tested with a failed
allocation at **every** requested buffer index, for the 960-token and
2560-token candidate shapes, on both GCC and Clang. The opt-in nine-buffer
path must fail immediately and publish zero logical ViT blocks; the
legacy 65-buffer path is intentionally not modified and may hold a
previously completed prefix until the parent FrameSession::State is
destroyed. Both paths must return failure, never a full eight-block result.
The native Initialize function creates the State as a unique_ptr and
publishes it only on success. No Vulkan allocators or live PS5 fences are
involved in this host fixture.

## Lower bound under the current real ViT buffer-owner contract

The exact native NrRdna2VitBlock::CanOwnVitBuffers validation requires
all nine logical roles to have pairwise-distinct VkBuffer handles within
a block. Therefore a compliant pool cannot use fewer than nine
physical handles per in-flight block. Original requested per-role
capacities conservatively become 15 token-planes when the real packed
Q/K/V outputs are sized to one plane each while retaining the current
FFN scratch size and batch cadence. The candidate reaches 9 buffers
and 15 planes without changing learned dispatch counts. This proves nine handles are minimal under CURRENT buffer identity checks;
it does NOT prove a global minimum of 15 planes under alternative batching.

The independent source guard at tools/check_r09_min_vit_optimality_host.py
reads the real ViT block, allocator and policy directly and rejects six
deliberate unsafe source mutations. If the native source contract changes,
this proof must be re-audited. No GPU throughput, trained-model image or
PS5 memory residency is established by the static proof.

## Safe Q/K/V capacity trim without changing learned dispatch cadence

The QKV output buffers had been provisioned at four token-planes each,
while the real E4M3FN Q/K/V normalization producer and attention consumer
require only 1024 bytes per token, exactly one plane. The new policy
allocates one plane for each of Q, K and V. Other roles and GEMM scratch
retain their prior capacities and batch schedule.

At 1080p, ViT buffer allocations are now projected as 37.5 MiB across
nine buffers, versus 462.5 MiB before pooling (425 MiB / ~91.9% less).
The 720p structural candidate requests 14.0625 MiB but remains unqualified
for the fixed native R09 graph. Neither VRAM residency nor FPS improvement
has been demonstrated. The earlier 60 MiB candidate was conservative.

## Selectable experimental scratch Pareto profiles (1080p)

An explicit experimental build can now select one of four source-policy
Pareto scratch sizes with -DEDEN_R09_MIN_VIT_POOL_SCRATCH_PLANES=4, 5, 6
or 12, but **only** if the entire nine-buffer experimental allocator is
also enabled with -DEDEN_R09_MIN_VIT_POOL_EXPERIMENTAL=1. An absent
scratch macro selects four planes; default/shipping builds continue using
the original 65-buffer path regardless of any scratch flag.

| Scratch planes | Requested ViT MiB at 1080p | FFN Expand batches | QKV batches |
| ---: | ---: | ---: | ---: |
| 4 | 37.5 | 4 | 4 |
| 5 | 40 | 4 | 3 |
| 6 | 42.5 | 3 | 2 |
| 12 | 57.5 | 3 | 1 |

These are verified source-policy candidates, not observed GPU FPS.
Non-Pareto values including 8 are deliberately rejected at the native
allocator policy boundary to prevent memory waste at 1080p. Every profile
is host-compiled in the extracted native method, runs failure injection,
and is validated for correct per-role scratch sizes. A real PS5 benchmark
and complete model image/fence proof remain mandatory. No new option is
implicitly enabled on production builds.
