// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

/* This file is part of the dynarmic project.
 * Copyright (c) 2022 MerryMage
 * SPDX-License-Identifier: 0BSD
 */

#include <mutex>
#include <optional>
#include "dynarmic/backend/x64/xbyak.h"

#include "dynarmic/backend/x64/abi.h"
#include "dynarmic/backend/x64/hostloc.h"
#include "dynarmic/common/spin_lock.h"

#ifdef DYNARMIC_ENABLE_NO_EXECUTE_SUPPORT
static const auto default_cg_mode = Xbyak::DontSetProtectRWE;
#else
static const auto default_cg_mode = nullptr; //Allow RWE
#endif

namespace Dynarmic {

void EmitSpinLockLock(Xbyak::CodeGenerator& code, Xbyak::Reg64 ptr, Xbyak::Reg32 tmp, bool waitpkg) {
    Xbyak::Label start, loop;
    code.jmp(start, code.T_NEAR);
    code.L(loop);

    if (waitpkg) {
        // TODO: this is because we lack regalloc - so better to be safe :(
        code.push(Xbyak::util::rax);
        code.push(Xbyak::util::rbx);
        code.push(Xbyak::util::rdx);
        // TODO: This clobbers EAX and EDX did we tell the regalloc?
        // ARM ptr for address-monitoring

        // XBYAK BUG: code.umonitor(ptr); see issue #255
        // replace once xbyak has been fixed
        code.db(0xF3);
        if (ptr.getIdx() >= 8) code.db(0x41);
        code.db(0x0F); code.db(0xAE);
        code.db(uint8_t((3 << 6) | ((6 & 7) << 3) | (ptr.getIdx() & 7)));

        // tmp.bit[0] = 0: C0.1 | Slow Wakup | Better Savings
        // tmp.bit[0] = 1: C0.2 | Fast Wakup | Lesser Savings
        // edx:eax is implicitly used as a 64-bit deadline timestamp
        // Use the maximum so that we use the operating system's maximum
        // allowed wait time within the IA32_UMWAIT_CONTROL register
        // Enter power state designated by tmp and wait for a write to lock_ptr
        code.mov(Xbyak::util::eax, 0xFFFFFFFF);
        code.mov(Xbyak::util::edx, Xbyak::util::eax);
        // TODO: We can only be here because tmp is 1 already - however we repeatedly overwrite it...
        code.mov(Xbyak::util::ebx, 1);
        code.umwait(Xbyak::util::ebx);
        // CF == 1 if we hit the OS-timeout in IA32_UMWAIT_CONTROL without a write
        // CF == 0 if we exited the wait for any other reason
        code.pop(Xbyak::util::rdx);
        code.pop(Xbyak::util::rbx);
        code.pop(Xbyak::util::rax);
    } else {
        code.pause();
    }
    code.L(start);
    code.mov(tmp, 1);
    /*code.lock();*/ code.xchg(code.dword[ptr], tmp);
    code.test(tmp, tmp);
    code.jnz(loop, code.T_NEAR);
}

void EmitSpinLockUnlock(Xbyak::CodeGenerator& code, Xbyak::Reg64 ptr, Xbyak::Reg32 tmp) {
    code.xor_(tmp, tmp);
    code.xchg(code.dword[ptr], tmp);
    code.mfence();
}

// PS5 x64: retain the storage ABI used by generated monitor operations and
// the original exchange + full-fence unlock ordering. No executable helper
// allocation or call_once is needed for compiler-provided lock-free atomics.
static_assert(__atomic_always_lock_free(sizeof(int), nullptr));
extern "C" int sceKernelUsleep(unsigned int);

void SpinLock::Lock() noexcept {
    unsigned spins = 0;
    while (__atomic_exchange_n(&storage, 1, __ATOMIC_SEQ_CST)) {
        // Poll without repeatedly invalidating the owner's cache line. Guest CPU threads run at
        // critical priority; if an owner is preempted, an unbounded busy waiter can starve it.
        while (__atomic_load_n(&storage, __ATOMIC_RELAXED)) {
            if (++spins < 4096)
                __builtin_ia32_pause();
            else
                sceKernelUsleep(50);
        }
    }
}

void SpinLock::Unlock() noexcept {
    __atomic_exchange_n(&storage, 0, __ATOMIC_SEQ_CST);
    __atomic_thread_fence(__ATOMIC_SEQ_CST);
}

} // namespace Dynarmic
