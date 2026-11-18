# NS2 parked acquire diagnostics — upstream triage — 2026-11-18

## Scope

Diagnostic-only candidate for PS5 NS2/Ampere black-screen or loading stalls. No semaphore value is fabricated, no fence is added, no scheduler cadence changes, and NS1 paths are untouched.

## Upstream-first classification

### Already covered in Encore

- **Lichii #1700** — syncpoint actions outside the Host1x guard: Encore already has DueActions, unique action IDs, action_done_cv and the maintained Host1x action lifetime model.
- **Lichii #333** — GPU page-table range too small: Encore's NS2 address-space creation already requests 0x08000000 .. 1 TiB, with a 768 GiB small/big-page split, and AllocAsEx sizes the GMMU from the supplied end.
- **Lichii issue #540** — Accurate GPU can leave SyncOperation releases uncommitted while all channels wait. Encore's native ResumeNativeAcquire already calls Rasterizer::ReleaseFences() before each coherent semaphore read. Vulkan ReleaseFences(true) reaches FenceManager::WaitPendingFences(), whose dummy SignalFence consumes uncommitted_operations into pending_operations and waits for retirement. Do not import CommitSyncOperations blindly.

### Not proven for Encore

- **Lichii #1410** — stop a vi2 present fence wait when only the guest CPU can release GPU progress. Closed/unmerged upstream; no direct Encore evidence yet.
- **Eglantine #712** — delayed-fence / flip-counter rescue. Closed/unmerged and changes guest-visible semaphore behavior; unsafe to port without a matching ring signature.
- **Lichii #231** — fast semaphore-readable path reduces split drains. Closed/unmerged performance work, dependent on other GPU-split changes; not a black-screen fix by itself.

## Missing evidence in current logs

Encore can park an Ampere SEM_EXECUTE acquire and retry it from the GPU worker roughly every millisecond. Before this candidate, a long-lived wait records no structured advanced-log event containing:
- channel/bind ID;
- GPU virtual address and CPU backing;
- operation, target and current value;
- wait duration and poll count;
- process/channel generation.

Without those fields, a black screen cannot be reliably separated into guest-only release, delayed fence, GMMU/backing lifetime or absent CPU progress.

## Candidate

When Detailed Logging is enabled, ResumeNativeAcquire records:
- 50 ms;
- 500 ms;
- 2 s;
- then at most once every 10 s.

The event is:
`EDEN_NS2_ACQUIRE_WAIT`

Quiet mode performs only the existing detailed_gpu_profile atomic check and does not read the wall clock or emit logs. The observer is downstream of the existing epoch/backing validation and coherent read. It never calls SignalFence, SignalReference, SyncOperation or any guest-memory write.

## Validation boundary

Host/source test: `tests/ns2_pending_acquire_diagnostics_source.py`.

A future console log can justify behavior changes only if it proves a matching upstream signature. In particular:
- target N+1 / current N stable for seconds after ReleaseFences: investigate guest-only/fence ownership;
- changing current value that repeatedly misses AcquireEqual: investigate passed-value semantics / #712-like behavior;
- backing/epoch errors: lifetime/remap bug, not semaphore rescue;
- no acquire-wait events while GPU commands remain zero: investigate GPFIFO/guest progress earlier in the path.

No runtime bug is declared fixed by this diagnostic branch.
