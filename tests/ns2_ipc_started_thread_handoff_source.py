#!/usr/bin/env python3
from pathlib import Path
R=Path(__file__).resolve().parents[1]
hdr=(R/"encore-active/eden/src/core/hle/kernel/k_thread.h").read_text()
thr=(R/"encore-active/eden/src/core/hle/kernel/k_thread.cpp").read_text()
start=(R/"encore-active/eden/src/core/hle/kernel/svc/svc_thread.cpp").read_text()
ipc=(R/"encore-active/eden-core/src/core/hle/kernel/svc/svc_ipc_standard.cpp").read_text()
assert "std::vector<KThread*>& GetPendingStartedThreads()" in hdr
assert "std::vector<KThread*> m_pending_started_threads;" in hdr
assert "for (KThread* thread : m_pending_started_threads)" in thr
assert "m_pending_started_threads.clear();" in thr
for x in (
 "thread->GetActiveCore() == cur_thread.GetActiveCore()",
 "thread->GetPriority() > cur_thread.GetPriority()",
 "thread->Open(system.Kernel());",
 "cur_thread.GetPendingStartedThreads().push_back(thread.GetPointerUnsafe());",
): assert x in start,x
for x in (
 "#include <chrono>",
 '#include "core/hle/kernel/k_scheduler.h"',
 "std::chrono::milliseconds(20)",
 "KScopedSchedulerLock sl{kernel};",
 "thread->GetState() == ThreadState::Runnable",
 "thread->GetWaitingLockInfo() != nullptr",
 "kernel.HardwareTimer().GetTick() + 50'000",
 "thread->Close(kernel);",
 "pending.clear();",
): assert x in ipc,x
assert ipc.index("session->SendSyncRequest") < ipc.index("GetPendingStartedThreads")
assert ipc.index("GetPendingStartedThreads") < ipc.index("R_RETURN(result)")
print("NS2_IPC_STARTED_THREAD_HANDOFF_SOURCE_PASS upstream=3140648 bound_ms=20 scheduler_lock=1")


# Current Lichii/Eglantine thread-start ABI: NS2 core ids 6/7 are folded
# after validation, and StartThread gives the worker a bounded chance to
# consume stack-backed startup arguments before the creator resumes.
assert "bool HasEnteredKernel() const" in hdr
assert "void SetEnteredKernel()" in hdr
assert "std::atomic<bool> m_entered_kernel{false};" in hdr
for x in (
    "constexpr int32_t FoldExtraCoreId",
    "constexpr u64 FoldExtraCoreMask",
    "core_id = FoldExtraCoreId(core_id);",
    "affinity_mask = FoldExtraCoreMask(affinity_mask);",
    "!thread->HasEnteredKernel()",
    "std::chrono::milliseconds(5)",
    "std::this_thread::yield();",
): assert x in start,x
active_svc=(R/"encore-active/ns2-runtime/gpu_access_svc/core/hle/kernel/svc.cpp").read_text()
assert active_svc.index("SaveSvcArguments(process, args)") < active_svc.index("GetCurrentThread(kernel).SetEnteredKernel()")
print("NS2_THREAD_FIRST_SVC_HANDOFF_SOURCE_PASS upstream=current-lichii-eglantine core_fold=1 bound_ms=5")
