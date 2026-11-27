// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

// SPDX-FileCopyrightText: Copyright 2023 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "core/core.h"
#include "core/hle/kernel/k_process.h"
#include "core/hle/kernel/svc.h"
#include "core/hle/kernel/svc_results.h"
#include "core/hle/kernel/svc_types.h"

namespace Kernel::Svc {

void FlushEntireDataCache(Core::System& system) {
    // An entire-CPU cache invalidation cannot be modeled by flushing a single
    // process range. Remain fail-closed until a coherent global operation exists.
    UNIMPLEMENTED();
}

Result FlushDataCache(Core::System& system, uint64_t address, uint64_t size) {
    R_UNLESS(size > 0, ResultInvalidSize);
    // Reject an overflowing guest interval before checking its page table.
    R_UNLESS(address < address + size, ResultInvalidCurrentMemory);

    auto& process = GetCurrentProcess(system.Kernel());
    R_UNLESS(process.GetPageTable().Contains(address, size), ResultInvalidCurrentMemory);
    // Actual guest CPU -> GPU visibility, not a successful no-op.
    R_RETURN(GetCurrentMemory(system.Kernel()).FlushDataCache(address, size));
}

Result InvalidateProcessDataCache(Core::System& system, Handle process_handle, uint64_t address,
                                  uint64_t size) {
    R_UNLESS(size > 0, ResultInvalidSize);
    R_UNLESS(address < address + size, ResultInvalidCurrentMemory);

    KScopedAutoObject process = GetCurrentProcess(system.Kernel()).GetHandleTable().GetObject<KProcess>(
        system.Kernel(), process_handle);
    R_UNLESS(process.IsNotNull(), ResultInvalidHandle);
    R_UNLESS(process->GetPageTable().Contains(address, size), ResultInvalidCurrentMemory);

    // An invalidation must make GPU writes visible to the TARGET process.
    R_RETURN(process->GetMemory().InvalidateDataCache(address, size));
}

Result StoreProcessDataCache(Core::System& system, Handle process_handle, uint64_t address,
                             uint64_t size) {
    R_UNLESS(size > 0, ResultInvalidSize);
    R_UNLESS(address < address + size, ResultInvalidCurrentMemory);

    KScopedAutoObject process = GetCurrentProcess(system.Kernel()).GetHandleTable().GetObject<KProcess>(
        system.Kernel(), process_handle);
    R_UNLESS(process.IsNotNull(), ResultInvalidHandle);
    R_UNLESS(process->GetPageTable().Contains(address, size), ResultInvalidCurrentMemory);

    // A store must publish the TARGET process's CPU writes to the rasterizer.
    R_RETURN(process->GetMemory().StoreDataCache(address, size));
}

Result FlushProcessDataCache(Core::System& system, Handle process_handle, u64 address, u64 size) {
    // Validate address/size.
    R_UNLESS(size > 0, ResultInvalidSize);
    R_UNLESS(address < address + size, ResultInvalidCurrentMemory);

    // Get the process from its handle.
    KScopedAutoObject process = GetCurrentProcess(system.Kernel()).GetHandleTable().GetObject<KProcess>(system.Kernel(), process_handle);
    R_UNLESS(process.IsNotNull(), ResultInvalidHandle);

    // Verify the region is within range.
    auto& page_table = process->GetPageTable();
    R_UNLESS(page_table.Contains(address, size), ResultInvalidCurrentMemory);

    // Perform the operation.
    R_RETURN(process->GetMemory().FlushDataCache(address, size));
}

void FlushEntireDataCache64(Core::System& system) {
    FlushEntireDataCache(system);
}

Result FlushDataCache64(Core::System& system, uint64_t address, uint64_t size) {
    R_RETURN(FlushDataCache(system, address, size));
}

Result InvalidateProcessDataCache64(Core::System& system, Handle process_handle, uint64_t address,
                                    uint64_t size) {
    R_RETURN(InvalidateProcessDataCache(system, process_handle, address, size));
}

Result StoreProcessDataCache64(Core::System& system, Handle process_handle, uint64_t address,
                               uint64_t size) {
    R_RETURN(StoreProcessDataCache(system, process_handle, address, size));
}

Result FlushProcessDataCache64(Core::System& system, Handle process_handle, uint64_t address,
                               uint64_t size) {
    R_RETURN(FlushProcessDataCache(system, process_handle, address, size));
}

void FlushEntireDataCache64From32(Core::System& system) {
    return FlushEntireDataCache(system);
}

Result FlushDataCache64From32(Core::System& system, uint32_t address, uint32_t size) {
    R_RETURN(FlushDataCache(system, address, size));
}

Result InvalidateProcessDataCache64From32(Core::System& system, Handle process_handle,
                                          uint64_t address, uint64_t size) {
    R_RETURN(InvalidateProcessDataCache(system, process_handle, address, size));
}

Result StoreProcessDataCache64From32(Core::System& system, Handle process_handle, uint64_t address,
                                     uint64_t size) {
    R_RETURN(StoreProcessDataCache(system, process_handle, address, size));
}

Result FlushProcessDataCache64From32(Core::System& system, Handle process_handle, uint64_t address,
                                     uint64_t size) {
    R_RETURN(FlushProcessDataCache(system, process_handle, address, size));
}

} // namespace Kernel::Svc
