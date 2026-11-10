// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

// SPDX-FileCopyrightText: Copyright 2024 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "common/scope_exit.h"
#ifdef EDEN_NS2_PROCESS_RUN_GUARD
#include "common/logging.h"
#include "core/loader/ns2_svc_trace.h"
#include "core/hle/service/filesystem/filesystem.h"
#endif

#include "core/hle/kernel/k_process.h"
#include "core/hle/kernel/svc_types.h"
#include "core/hle/service/os/process.h"
#include "core/loader/loader.h"

namespace Service {

bool Process::Initialize(Loader::AppLoader& loader, Loader::ResultStatus& out_load_result) {
    // First, ensure we are not holding another process.
    this->Finalize();

    // Create the process.
    auto* const process = Kernel::KProcess::Create(m_system.Kernel());
    // The slab allocator can fail before an ExeFS/NSO handoff exists.
    // Never register or dereference a null kernel process on that path.
    if (!process) {
        out_load_result = Loader::ResultStatus::ErrorNotInitialized;
        return false;
    }
    Kernel::KProcess::Register(m_system.Kernel(), process);

    // On exit, ensure we free the additional reference to the process.
    SCOPE_EXIT {
        process->Close(m_system.Kernel());
    };
#ifdef EDEN_NS2_PROCESS_RUN_GUARD
    // The NS2 loader may register its SVC trace before Initialize has
    // published the KProcess. Roll it back on every rejected handoff,
    // including missing NPDM parameters or a later allocation failure.
    // This guard runs before the KProcess reference is closed.
    bool process_committed = false;
    SCOPE_EXIT {
        if (!process_committed) {
            // The loader may register RomFS before it installs the SVC
            // trace. A malformed NPDM/NSO or allocation failure in that
            // interval must still release the process-owned filesystem
            // record. UnregisterProcessIfPresent is idempotent for a PID
            // with no registration (including non-NS2 failed loads).
            const auto pid = process->GetProcessId();
            m_system.GetFileSystemController().UnregisterProcessIfPresent(pid);
            Eden::NS2::SvcTrace::Unregister(pid);
        }
    };
#endif

    // Insert process modules into memory.
    const auto [load_result, load_parameters] = loader.Load(*process, m_system);
    out_load_result = load_result;

    // Ensure loading was successful.
    if (load_result != Loader::ResultStatus::Success) {
        return false;
    }
    // Success without loader parameters is malformed: touching the optional
    // would be undefined behavior and may publish a partially initialized
    // KProcess into the scheduler. The scope guard closes it on failure.
    if (!load_parameters) {
        out_load_result = Loader::ResultStatus::ErrorUnableToParseKernelMetadata;
        return false;
    }

    // TODO: remove this, kernel already tracks this
    m_system.Kernel().AppendNewProcess(process);

    // Note the load parameters from NPDM.
    m_main_thread_priority = load_parameters->main_thread_priority;
    m_main_thread_stack_size = load_parameters->main_thread_stack_size;

    // This process has not started yet.
    m_process_started = false;

    // Take ownership of the process object.
    m_process = process;
    m_process->Open(m_system.Kernel());
#ifdef EDEN_NS2_PROCESS_RUN_GUARD
    process_committed = true;
#endif

    // We succeeded.
    return true;
}

void Process::Finalize() {
    // Terminate, if we are currently holding a process.
    this->Terminate();

    // Close the process.
    if (m_process) {
#ifdef EDEN_NS2_PROCESS_RUN_GUARD
        // Do not retain a stale NS2 trace after process teardown.
        if (Eden::NS2::SvcTrace::active_pid.load(std::memory_order_acquire) ==
            m_process->GetProcessId()) {
            // Normal NS2 teardown must also retire its process-owned
            // filesystem registration before the kernel PID is recyclable.
            m_system.GetFileSystemController().UnregisterProcessIfPresent(
                m_process->GetProcessId());
            Eden::NS2::SvcTrace::Unregister(m_process->GetProcessId());
        }
#endif
        m_process->Close(m_system.Kernel());
        // TODO: remove this, kernel already tracks this
        m_system.Kernel().RemoveProcess(m_process);
    }

    // Clean up.
    m_process = nullptr;
    m_main_thread_priority = 0;
    m_main_thread_stack_size = 0;
    m_process_started = false;
}

bool Process::Run() {
    // If we already started the process, don't start again.
    if (m_process_started) {
        return false;
    }

#ifdef EDEN_NS2_PROCESS_RUN_GUARD
    // N52: only the real NS2 ExeFS process registered during loader handoff
    // must treat a kernel scheduling failure as a failed launch. Keep Switch 1
    // semantics byte-for-byte below, until generic behavior is independently audited.
    if (m_process && m_process->GetProcessId() != 0 &&
        Eden::NS2::SvcTrace::active_pid.load(std::memory_order_acquire) ==
            m_process->GetProcessId()) {
        const auto result =
            m_process->Run(m_system.Kernel(), m_main_thread_priority, m_main_thread_stack_size);
        if (R_FAILED(result)) {
            LOG_ERROR(Service_LDR, "NS2 KProcess::Run rejected pid={} result={:#x}",
                      m_process->GetProcessId(), result.raw);
            // KProcess may already have allocated/rolled back resources; never
            // start it twice without Finalize and a fresh loader handoff.
            m_process_started = true;
            // This NS2 run was already registered by the loader but was
            // never scheduled successfully. Retire its RomFS owner BEFORE
            // clearing the trace; Finalize cannot identify the NS2 process
            // once the trace is gone. No NS1 behavior changes.
            m_system.GetFileSystemController().UnregisterProcessIfPresent(
                m_process->GetProcessId());
            Eden::NS2::SvcTrace::Unregister(m_process->GetProcessId());
            return false;
        }
        m_process_started = true;
        return true;
    }
#endif

    // Start.
    if (m_process) {
        m_process->Run(m_system.Kernel(), m_main_thread_priority, m_main_thread_stack_size);
    }

    // Mark as started.
    m_process_started = true;

    // We succeeded.
    return true;
}

void Process::Terminate() {
    if (m_process) {
        m_process->Terminate(m_system.Kernel());
    }
}

void Process::ResetSignal() {
    if (m_process) {
        m_process->Reset(m_system.Kernel());
    }
}

bool Process::IsRunning() const {
    if (m_process) {
        const auto state = m_process->GetState();
        return state == Kernel::KProcess::State::Running ||
               state == Kernel::KProcess::State::RunningAttached ||
               state == Kernel::KProcess::State::DebugBreak;
    }

    return false;
}

bool Process::IsTerminated() const {
    if (m_process) {
        return m_process->IsTerminated();
    }

    return false;
}

u64 Process::GetProcessId() const {
    if (m_process) {
        return m_process->GetProcessId();
    }

    return 0;
}

u64 Process::GetProgramId() const {
    if (m_process) {
        return m_process->GetProgramId();
    }

    return 0;
}

void Process::Suspend(bool suspended) {
    if (m_process) {
        m_process->SetActivity(m_system.Kernel(), suspended ? Kernel::Svc::ProcessActivity::Paused : Kernel::Svc::ProcessActivity::Runnable);
    }
}

} // namespace Service
