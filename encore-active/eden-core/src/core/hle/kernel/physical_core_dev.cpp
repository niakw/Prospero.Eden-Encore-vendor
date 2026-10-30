#include <atomic>
#include <cstdio>
#include "guest_fault.h"
#include "performance.h"
#include "core/hle/kernel/k_process.h"
#include "core/arm/debug.h"
// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

// SPDX-FileCopyrightText: Copyright 2020 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "common/scope_exit.h"
#include "common/settings.h"
#include "core/core.h"
#include "core/debugger/debugger.h"
#include "core/hle/kernel/k_process.h"
#include "core/hle/kernel/k_thread.h"
#include "core/hle/kernel/kernel.h"
#include "core/hle/kernel/physical_core.h"
#include "core/hle/kernel/svc.h"

namespace Kernel {

PhysicalCore::PhysicalCore(KernelCore& kernel, std::size_t core_index)
    : m_core_index{core_index}
{
    m_is_single_core = !kernel.IsMulticore();
}
PhysicalCore::~PhysicalCore() = default;

void PhysicalCore::RunThread(KernelCore& kernel, Kernel::KThread* thread) {
    auto* process = thread->GetOwnerProcess();
    auto& system = kernel.System();
    auto* interface = process->GetArmInterface(m_core_index);

    interface->Initialize();

    const auto EnterContext = [&]() {
        // Lock the core context.
        std::scoped_lock lk{m_guard};

        // Check if we are already interrupted. If we are, we can just stop immediately.
        if (__atomic_load_n(&m_is_interrupted, __ATOMIC_RELAXED)) {
            return false;
        }

        // Mark that we are running.
        m_arm_interface = interface;
        m_current_thread = thread;

        // Acquire the lock on the thread parameters.
        // This allows us to force synchronization with Interrupt.
        interface->LockThread(thread);

        return true;
    };

    const auto ExitContext = [&]() {
        // Save the JIT context back to the thread so the debugger can
        // read the current register state. Debug halt paths (step,
        // breakpoint, watchpoint) return from RunThread without going
        // through the scheduler's Unload/SaveContext.
        if (system.DebuggerEnabled()) {
            interface->GetContext(thread->GetContext());
        }

        // Unlock the thread.
        interface->UnlockThread(thread);

        // Lock the core context.
        std::scoped_lock lk{m_guard};

        // On exit, we no longer are running.
        m_arm_interface = nullptr;
        m_current_thread = nullptr;
    };

    while (true) {
        // If the thread is scheduled for termination, exit.
        if (thread->HasDpc() && thread->IsTerminationRequested()) {
            thread->Exit(kernel);
        }

        // Notify the debugger and go to sleep if a step was performed
        // and this thread has been scheduled again.
        if (thread->GetStepState() == StepState::StepPerformed) {
            system.GetDebugger().NotifyThreadStopped(thread);
            thread->RequestSuspend(kernel, SuspendType::Debug);
            return;
        }

        // Otherwise, run the thread.
        Core::HaltReason hr{};
        {
            // If we were interrupted, exit immediately.
            if (!EnterContext()) {
                return;
            }

            if (thread->GetStepState() == StepState::StepPending) {
                hr = interface->StepThread(thread);

                if (True(hr & Core::HaltReason::StepThread)) {
                    thread->SetStepState(StepState::StepPerformed);
                }
            } else {
                hr = interface->RunThread(thread);
            }

            ExitContext();
        }

        // Determine why we stopped.
        // If a step completed successfully, skip other halt reason handlers
        // the step takes priority (e.g. step may also set InstructionBreakpoint
        // if the next instruction happens to be a breakpoint).
        const bool step_completed = True(hr & Core::HaltReason::StepThread)
                                    && thread->GetStepState() == StepState::StepPerformed;
        const bool supervisor_call = !step_completed && True(hr & Core::HaltReason::SupervisorCall);
        const bool prefetch_abort = !step_completed && True(hr & Core::HaltReason::PrefetchAbort);
        const bool breakpoint = !step_completed && True(hr & Core::HaltReason::InstructionBreakpoint);
        const bool data_abort = !step_completed && True(hr & Core::HaltReason::DataAbort);
        const bool interrupt = !step_completed && True(hr & Core::HaltReason::BreakLoop);

        // Since scheduling may occur here, we cannot use any cached
        // state after returning from calls we make.

        // Notify the debugger and go to sleep if a breakpoint was hit,
        // or if the thread is unable to continue for any reason.
        if (breakpoint || prefetch_abort) {
            if (breakpoint) {
                interface->RewindBreakpointInstruction();
                // RewindBreakpointInstruction sets the JIT state to the
                // saved breakpoint context. Update the thread context to
                // match, since ExitContext already saved the post-execution
                // state.
                interface->GetContext(thread->GetContext());
            }
            if (system.DebuggerEnabled()) {
                system.GetDebugger().NotifyThreadStopped(thread);
            } else {
                interface->LogBacktrace(process);
                if (prefetch_abort) {
                    // ExitContext saves guest registers only for attached debuggers.
                    // Capture the faulting PC at the actual prefetch abort before
                    // the suspended thread can be unloaded or switched.
                    interface->GetContext(thread->GetContext());
                    const auto eden_name = Core::GetThreadName(thread).value_or("?");
                    // A faulting PC alone cannot identify a corrupt
                    // indirect-branch target vs a stale JIT return. Preserve
                    // the original guest return address and stack registers
                    // on this single fatal path; never sample per-frame.
                    const auto& fault_context = thread->GetContext();
                    // One bounded page-table query at the fatal fault only.
                    // Never dereference an untrusted guest address or alter JIT.
                    Kernel::KMemoryInfo lr_info{};
                    Kernel::Svc::PageInfo lr_page{};
                    const bool lr_query = process &&
                        !process->GetPageTable().QueryInfo(
                            &lr_info, &lr_page, fault_context.lr).IsError();
                    std::fprintf(stderr,
                        "EDEN_GUEST_FAULT_LR_MAP lr=0x%llx query=%s start=0x%llx size=0x%llx state=%u\n",
                        static_cast<unsigned long long>(fault_context.lr),
                        lr_query ? "ok" : "failed",
                        static_cast<unsigned long long>(lr_info.m_address),
                        static_cast<unsigned long long>(lr_info.m_size),
                        static_cast<unsigned>(lr_info.m_state));
                    char eden_fault[320]{};
                    std::snprintf(eden_fault, sizeof(eden_fault),
                        "guest thread %.64s tid=%llu pc=0x%llx lr=0x%llx sp=0x%llx fp=0x%llx jumped to an unmapped address",
                        eden_name.c_str(),
                        static_cast<unsigned long long>(thread->GetThreadId()),
                        static_cast<unsigned long long>(fault_context.pc),
                        static_cast<unsigned long long>(fault_context.lr),
                        static_cast<unsigned long long>(fault_context.sp),
                        static_cast<unsigned long long>(fault_context.fp));
                    ::Eden::RecordGuestFault(eden_fault);
                }
            }
            thread->RequestSuspend(kernel, SuspendType::Debug);
            return;
        }

        // Notify the debugger and go to sleep on data abort.
        if (data_abort) {
            if (system.DebuggerEnabled()) {
                system.GetDebugger().NotifyThreadWatchpoint(thread, *interface->HaltedWatchpoint());
            }
            thread->RequestSuspend(kernel, SuspendType::Debug);
            return;
        }

        // Handle system calls.
        if (supervisor_call) {
            // Perform call.
            Svc::Call(system, interface->GetSvcNumber());
            return;
        }

        // Handle external interrupt sources.
        if (interrupt || m_is_single_core) {
            return;
        }
    }
}

void PhysicalCore::LoadContext(const KThread* thread) {
    auto* const process = thread->GetOwnerProcess();
    if (!process) {
        // Kernel threads do not run on emulated CPU cores.
        return;
    }

    auto* interface = process->GetArmInterface(m_core_index);
    if (interface) {
        interface->SetContext(thread->GetContext());
        interface->SetTpidrroEl0(GetInteger(thread->GetTlsAddress()));
        interface->SetWatchpointArray(&process->GetWatchpoints());
    }
}

void PhysicalCore::LoadSvcArguments(const KProcess& process, std::span<const uint64_t, 8> args) {
    process.GetArmInterface(m_core_index)->SetSvcArguments(args);
}

void PhysicalCore::SaveContext(KThread* thread) const {
    auto* const process = thread->GetOwnerProcess();
    if (!process) {
        // Kernel threads do not run on emulated CPU cores.
        return;
    }

    auto* interface = process->GetArmInterface(m_core_index);
    if (interface) {
        interface->GetContext(thread->GetContext());
    }
}

void PhysicalCore::SaveSvcArguments(KProcess& process, std::span<uint64_t, 8> args) const {
    process.GetArmInterface(m_core_index)->GetSvcArguments(args);
}

void PhysicalCore::CloneFpuStatus(KThread* dst) const {
    auto* process = dst->GetOwnerProcess();

    Svc::ThreadContext ctx{};
    process->GetArmInterface(m_core_index)->GetContext(ctx);

    dst->GetContext().fpcr = ctx.fpcr;
    dst->GetContext().fpsr = ctx.fpsr;
}

void PhysicalCore::LogBacktrace(KernelCore& kernel) {
    auto* process = GetCurrentProcessPointer(kernel);
    if (!process) {
        return;
    }

    auto* interface = process->GetArmInterface(m_core_index);
    if (interface) {
        interface->LogBacktrace(process);
    }
}

void PhysicalCore::Idle() {
        const bool profile_idle = ::Eden::Performance::detailed_gpu_profile.load(std::memory_order_relaxed);
        const long long start = profile_idle ? ::Eden::Performance::NowNs() : 0;
        // The interrupt flag is guarded by m_guard. An unlocked atomic
        // builtin read races with Interrupt()/ClearInterrupt() plain stores.
        // Diagnostic idle sampling uses the original condition variable.
        bool slept;
        {
            std::unique_lock lk{m_guard};
            slept = !__atomic_load_n(&m_is_interrupted, __ATOMIC_RELAXED);
            m_on_interrupt.wait(lk, [this] { return __atomic_load_n(&m_is_interrupted, __ATOMIC_RELAXED); });
        }
        if (profile_idle)
            ::Eden::Performance::CountIdle(m_core_index, ::Eden::Performance::NowNs() - start, slept);
    }

bool PhysicalCore::IsInterrupted() const {
    return __atomic_load_n(&m_is_interrupted, __ATOMIC_RELAXED);
}

void PhysicalCore::Interrupt() {
    // Lock core context.
    std::scoped_lock lk{m_guard};

    // Load members.
    auto* arm_interface = m_arm_interface;
    auto* thread = m_current_thread;

    // Add interrupt flag.
    __atomic_store_n(&m_is_interrupted, true, __ATOMIC_RELAXED);

    // Interrupt ourselves.
    m_on_interrupt.notify_one();

    // If there is no thread running, we are done.
    if (arm_interface == nullptr) {
        return;
    }

    // Interrupt the CPU.
    arm_interface->SignalInterrupt(thread);
}

void PhysicalCore::ClearInterrupt() {
    std::scoped_lock lk{m_guard};
    __atomic_store_n(&m_is_interrupted, false, __ATOMIC_RELAXED);
}

} // namespace Kernel
