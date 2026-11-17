#!/usr/bin/env python3
from pathlib import Path
R=Path(__file__).resolve().parents[1]
hdr=(R/"encore-active/eden-threading/src/core/hle/kernel/k_thread.h").read_text()
thr=(R/"encore-active/eden-threading/src/core/hle/kernel/k_thread.cpp").read_text()
start=(R/"encore-active/eden-threading/src/core/hle/kernel/svc/svc_thread.cpp").read_text()
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
