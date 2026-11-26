// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

// SPDX-FileCopyrightText: 2021 yuzu Emulator Project
// SPDX-FileCopyrightText: 2021 Skyline Team and Contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include <algorithm>
#include <cstring>
#include <utility>

#include "common/alignment.h"
#include "common/assert.h"
#include <limits>
#include <new>
#include <set>
#include <vector>
#include "core/hle/kernel/k_process.h"
#include "core/hle/service/nvdrv/devices/native_gpu_kind.h"
#include "core/hle/service/nvdrv/devices/native_gpu_process_registry.h"
#include "core/hle/service/am/service/gpu_worker_shared.h"
#include "core/hle/service/nvdrv/devices/native_gpu_va_layout.h"
#include "core/hle/service/nvdrv/devices/native_gpu_allocation_range.h"
#include "common/common_types.h"
#include "common/logging.h"
#include "core/core.h"
#include "core/hle/service/nvdrv/core/container.h"
#include "core/hle/service/nvdrv/core/nvmap.h"
#include "core/hle/service/nvdrv/devices/ioctl_serialization.h"
#include "core/hle/service/nvdrv/devices/nvhost_as_gpu.h"
#include "core/hle/service/nvdrv/devices/nvhost_gpu.h"
#include "core/hle/service/nvdrv/nvdrv.h"
#include "video_core/control/channel_state.h"
#include "video_core/gpu.h"
#include "video_core/memory_manager.h"
#include "video_core/rasterizer_interface.h"

namespace Service::Nvidia::Devices {

nvhost_as_gpu::nvhost_as_gpu(Core::System& system_, Module& module_, NvCore::Container& core)
    : nvdevice{system_}, module{module_}, container{core}, nvmap{core.GetNvMapFile()}, vm{},
      gmmu{} {}

nvhost_as_gpu::~nvhost_as_gpu() {
    ReleaseNativeVmBinding();
    // Closing a native AS can retire its last physical NVMap pins. Finish
    // previously submitted GPU work BEFORE releasing backing, and do not
    // touch System::GPU after shutdown has already joined/destroyed it.
    // No cross-FD GMMU sharing is permitted: every AS owns its own table.
    if (m_native_process && gmmu &&
        NativeGpuProcessRegistry::Instance().Contains(m_native_process) &&
        system.IsPoweredOn() && !system.IsShuttingDown() &&
        (!mapping_map.empty() || !native_sparse_pins.empty()))
        system.GPU().WaitForIdle();
    // Unpin the GPU pages while this AS still owns its exact NvCore session:
    // CloseSession may otherwise retire the SMMU mappings before cleanup.
    {
        const std::scoped_lock lock{mutex};
        for (const auto& [address, mapping] : mapping_map) {
            nvmap.UnpinHandle(mapping.handle);
        }
        mapping_map.clear();
        for (const auto& [handle, address] : native_sparse_pins) {
            nvmap.UnpinHandle(handle);
        }
        native_sparse_pins.clear();
    }
    // Never call CloseSession holding the AS mutex: final session close can
    // finalize a process and indirectly close other service objects.
    if (m_channel_session_retained && m_nv_session)
        container.CloseSession(*m_nv_session);
    if (m_native_process) m_native_process->Close(system.Kernel());
}

NvResult nvhost_as_gpu::Ioctl1(DeviceFD fd, Ioctl command, std::span<const u8> input,
                               std::span<u8> output) {
    if (m_native_vm_retired ||
        (m_native_generation && !NativeEpochCurrent()))
        return NvResult::InvalidState;
    switch (command.group) {
    case 'A':
        switch (command.cmd) {
        case 0x1:
            return WrapFixed(this, &nvhost_as_gpu::BindChannel, input, output);
        case 0x2:
            return WrapFixed(this, &nvhost_as_gpu::AllocateSpace, input, output);
        case 0x3:
            // FreeSpace itself drains native GPU work before taking AS mutex,
            // including when invoked directly outside the ioctl dispatcher.
            return WrapFixed(this, &nvhost_as_gpu::FreeSpace, input, output);
        case 0x4: {
            // NS2 NVGPU_AS_IOCTL_MAP_BUFFER (0xC0184104) is a 24-byte
            // mapping of the complete NvMap handle. Route its real GPU
            // work through the same transactional native MapBufferEx.
            if (command.raw != 0xC0184104U) break;
            if (!m_native_process ||
                !NativeGpuProcessRegistry::Instance().Contains(m_native_process))
                return NvResult::NotSupported;
            struct NativeMapBuffer {
                MappingFlags flags;
                u32_le reserved;
                u32_le handle;
                u32_le reserved1; // Opaque inout, not a PTE page size.
                s64_le offset;
            };
            static_assert(sizeof(NativeMapBuffer) == 24);
            if (input.size() != sizeof(NativeMapBuffer) ||
                output.size() != sizeof(NativeMapBuffer))
                return NvResult::InvalidSize;
            NativeMapBuffer request{};
            std::memcpy(&request, input.data(), sizeof(request));
            // Both reserved words are opaque inout per the NVGPU AS
            // contract. Do not reject or reinterpret them as PTE granularity.
            IoctlMapBufferEx mapping{};
            mapping.flags = request.flags;
            mapping.kind = std::numeric_limits<u32>::max();
            mapping.handle = request.handle;
            mapping.page_size = 0;
            mapping.buffer_offset = 0;
            mapping.mapping_size = 0;
            mapping.offset = request.offset;
            const auto result = MapBufferEx(mapping);
            if (result == NvResult::Success) {
                request.offset = mapping.offset;
                std::memcpy(output.data(), &request, sizeof(request));
            }
            return result;
        }
        case 0x5:
            // UnmapBuffer owns the one required native GPU idle barrier.
            return WrapFixed(this, &nvhost_as_gpu::UnmapBuffer, input, output);
        case 0x6:
            return WrapFixed(this, &nvhost_as_gpu::MapBufferEx, input, output);
        case 0x7: {
            // NS2 NVGPU_AS_IOCTL_ALLOC_AS, compact 16-byte format. Unlike
            // ALLOC_AS_EX, the big-page size is word 0 and there are no VA
            // ranges. Initialize a real GMMU, never acknowledge a stub AS.
            if (command.raw != 0x40104107U) break;
            if (!m_native_process ||
                !NativeGpuProcessRegistry::Instance().Contains(m_native_process))
                return NvResult::NotSupported;
            struct NativeAllocAs {
                u32_le big_page_size;
                s32_le as_fd;
                u64_le reserved; // The 16-byte ioctl has no flags field.
            };
            static_assert(sizeof(NativeAllocAs) == 16);
            if (input.size() != sizeof(NativeAllocAs))
                return NvResult::InvalidSize;
            NativeAllocAs request{};
            std::memcpy(&request, input.data(), sizeof(request));
            // This reserved u64 is ignored by the documented compact
            // ABI. Native flags=5 belongs only to ALLOC_AS_EX (40 bytes).
            IoctlAllocAsEx parameters{};
            parameters.as_fd = request.as_fd;
            parameters.big_page_size = request.big_page_size;
            parameters.flags = 0;
            return AllocAsEx(parameters);
        }
        case 0x8:
            return WrapFixed(this, &nvhost_as_gpu::GetVARegions1, input, output);
        case 0x9: {
            // libounce Switch 2: 0x40284109 is AS_IMPORT, not ALLOC_AS_EX.
            // The GSV VM id was created by main 33 and registered by root 434
            // before this independent NVDRV descriptor was opened.
            if (command.raw == 0x40284109U && m_native_process &&
                NativeGpuProcessRegistry::Instance().Contains(m_native_process)) {
                if (input.size() != sizeof(NativeVmImport))
                    return NvResult::InvalidSize;
                NativeVmImport parameters{};
                std::memcpy(&parameters, input.data(), sizeof(parameters));
                return ImportNativeVm(parameters);
            }
            // Preserve the legacy Maxwell/Yuzu A:9 allocation ABI for NS1.
            return WrapFixed(this, &nvhost_as_gpu::AllocAsEx, input, output);
        }
        case 0xa: {
            // NVGPU_AS_IOCTL_MAP_BUFFER_EX2 (56 bytes) extends the original
            // 40-byte map. Do not silently ignore new VMA/page constraints:
            // those require their own validated GPU VA ownership.
            if (command.raw != 0xC038410AU) break;
            if (!m_native_process ||
                !NativeGpuProcessRegistry::Instance().Contains(m_native_process))
                return NvResult::NotSupported;
            struct NativeMapBufferEx2 {
                MappingFlags flags;
                u32_le kind;
                u32_le handle;
                u32_le reserved0;
                s64_le buffer_offset;
                u64_le mapping_size;
                s64_le offset;
                u64_le vma_address;
                u32_le pages;
                u32_le reserved1;
            };
            static_assert(sizeof(NativeMapBufferEx2) == 56);
            if (input.size() != sizeof(NativeMapBufferEx2) ||
                output.size() != sizeof(NativeMapBufferEx2))
                return NvResult::InvalidSize;
            NativeMapBufferEx2 args{};
            std::memcpy(&args, input.data(), sizeof(args));
            // A nonzero VMA/page span needs real extra ownership. The
            // reserved inout words have no such semantics and are copied
            // back byte-identically, never treated as a page-size override.
            if (args.vma_address || args.pages)
                return NvResult::NotSupported;
            IoctlMapBufferEx mapped{};
            mapped.flags = args.flags;
            mapped.kind = args.kind;
            mapped.handle = args.handle;
            mapped.page_size = 0;
            mapped.buffer_offset = args.buffer_offset;
            mapped.mapping_size = args.mapping_size;
            mapped.offset = args.offset;
            const auto status = MapBufferEx(mapped);
            if (status == NvResult::Success) {
                args.kind = mapped.kind;
                args.offset = mapped.offset;
                std::memcpy(output.data(), &args, sizeof(args));
            }
            return status;
        }
        case 0xb: {
            // GET_SYNC_RO_MAP is an NS2-only, output-only 16-byte ioctl.
            // Checking cmd alone would route an unrelated Maxwell/NS1
            // ioctl through the native syncpoint memory shim.
            if (command.raw != 0x8010410BU || !m_native_process ||
                !NativeGpuProcessRegistry::Instance().Contains(m_native_process))
                return NvResult::NotSupported;
            if (output.size() != sizeof(IoctlNx2SyncpointShimInfo) ||
                (!input.empty() && input.size() != sizeof(IoctlNx2SyncpointShimInfo)))
                return NvResult::InvalidSize;
            return WrapFixed(this, &nvhost_as_gpu::Nx2GetSyncpointShimInfo, input, output);
        }
        case 0xc: {
            // Switch 2 NVGPU_AS_IOCTL_MAPPING_MODIFY (0xC020410C),
            // a 32-byte request distinct from the legacy Maxwell ioctls.
            if (command.raw != 0xC020410CU) break;
            if (!m_native_process ||
                !NativeGpuProcessRegistry::Instance().Contains(m_native_process))
                return NvResult::NotSupported;
            struct NativeModify {
                u16_le compressed_kind;
                u16_le fallback_kind;
                u32_le reserved;
                u64_le buffer_offset;
                u64_le size;
                u64_le map_address;
            };
            static_assert(sizeof(NativeModify) == 32);
            if (input.size() != sizeof(NativeModify) ||
                output.size() != sizeof(NativeModify))
                return NvResult::InvalidSize;
            NativeModify args{};
            std::memcpy(&args, input.data(), sizeof(args));
            const u32 requested = args.compressed_kind;
            const u32 fallback = args.fallback_kind;
            const u64 base = args.map_address;
            const u64 offset = args.buffer_offset;
            const u64 bytes = args.size;
            // The middle 4 bytes are ABI alignment padding, not a
            // command flag. Preserve them untouched in the reply.
            if (fallback > std::numeric_limits<u8>::max() ||
                (requested > std::numeric_limits<u8>::max() && requested != 0xffff) ||
                !bytes || offset > std::numeric_limits<u64>::max() - base)
                return NvResult::BadParameter;
            const auto kind = SelectNativeMemoryKind(
                static_cast<u8>(requested == 0xffff ? fallback : requested),
                static_cast<u8>(fallback));
            if (!kind) return NvResult::NotSupported;
            if (system.IsPoweredOn() && !system.IsShuttingDown())
                system.GPU().WaitForIdle();
            const auto status = ModifyNativeKind(base + offset, bytes, kind->backend);
            if (status == NvResult::Success)
                std::memcpy(output.data(), input.data(), sizeof(args));
            return status;
        }
        case 0xd: {
            // Switch 2 NVGPU_AS_IOCTL_REMAP (0xC???410D) is an array
            // of 40-byte descriptors, not legacy A:14's 20-byte entries.
            if ((command.raw & 0xC000FFFFU) != 0xC000410DU) break;
            if (!m_native_process ||
                !NativeGpuProcessRegistry::Instance().Contains(m_native_process))
                return NvResult::NotSupported;
            if (input.empty() || (input.size() % sizeof(NativeSparseRemapEntry)) ||
                input.size() > 256 * sizeof(NativeSparseRemapEntry) ||
                output.size() < input.size())
                return NvResult::InvalidSize;
            std::vector<NativeSparseRemapEntry> entries;
            try {
                entries.resize(input.size() / sizeof(NativeSparseRemapEntry));
            } catch (const std::bad_alloc&) {
                return NvResult::InsufficientMemory;
            }
            std::memcpy(entries.data(), input.data(), input.size());
            // The newer ABI uses -1 to request its uncompressed fallback.
            for (auto& entry : entries)
                if (u16{entry.kind} == 0xffff)
                    entry.kind = entry.fallback_kind;
            if (system.IsPoweredOn() && !system.IsShuttingDown())
                system.GPU().WaitForIdle();
            const auto status = RemapNativeSparse(entries);
            if (status == NvResult::Success)
                std::memcpy(output.data(), input.data(), input.size());
            return status;
        }
        case 0x14:
            return WrapVariable(this, &nvhost_as_gpu::Remap, input, output);
        default:
            break;
        }
        break;
    default:
        break;
    }

    UNIMPLEMENTED_MSG("Unimplemented ioctl={:08X}", command.raw);
    return NvResult::NotImplemented;
}

NvResult nvhost_as_gpu::Ioctl2(DeviceFD fd, Ioctl command, std::span<const u8> input,
                               std::span<const u8> inline_input, std::span<u8> output) {
    if (m_native_vm_retired ||
        (m_native_generation && !NativeEpochCurrent()))
        return NvResult::InvalidState;
    UNIMPLEMENTED_MSG("Unimplemented ioctl={:08X}", command.raw);
    return NvResult::NotImplemented;
}

NvResult nvhost_as_gpu::Ioctl3(DeviceFD fd, Ioctl command, std::span<const u8> input,
                               std::span<u8> output, std::span<u8> inline_output) {
    if (m_native_vm_retired ||
        (m_native_generation && !NativeEpochCurrent()))
        return NvResult::InvalidState;
    switch (command.group) {
    case 'A':
        switch (command.cmd) {
        case 0x8:
            return WrapFixedInlOut(this, &nvhost_as_gpu::GetVARegions3, input, output,
                                   inline_output);
        default:
            break;
        }
        break;
    default:
        break;
    }
    UNIMPLEMENTED_MSG("Unimplemented ioctl={:08X}", command.raw);
    return NvResult::NotImplemented;
}

void nvhost_as_gpu::OnOpen(NvCore::SessionId session_id, DeviceFD fd) {
    if (const auto* session = container.GetSession(session_id)) {
        m_nv_session = session_id;
        m_native_process = session->process;
        if (m_native_process) {
            m_native_process->Open(system.Kernel());
            // OunceVK creates/imports the GSV VM before opening this AS FD.
            // Preserve that process generation for the descriptor lifetime.
            if (const auto ticket =
                    NativeGpuProcessRegistry::Instance().CaptureTicket(m_native_process))
                m_native_generation = ticket->generation;
        }
    }
}
void nvhost_as_gpu::OnClose(DeviceFD fd) {
    ReleaseNativeVmBinding();
}

NvResult nvhost_as_gpu::ImportNativeVm(const NativeVmImport& params) {
    if (!m_native_process || m_native_vm_retired || !NativeEpochCurrent() ||
        !NativeGpuProcessRegistry::Instance().Contains(m_native_process) ||
        u32{params.version} != 5 || u32{params.vm_id} == 0 ||
        u64{params.user_first} != NativeNs2GpuVaStart ||
        u64{params.user_end} != NativeNs2GpuVaSmallSplit)
        return NvResult::BadParameter;
    if (m_native_vm_id || vm.initialised || gmmu)
        return NvResult::InvalidState;

    auto owner = NativeGpuProcessRegistry::Instance().FindRegisteredVmOwner(
        m_native_process, u32{params.vm_id});
    if (!owner) return NvResult::InvalidState;

    IoctlAllocAsEx layout{};
    layout.big_page_size = 0x20000;
    layout.va_range_start = NativeNs2GpuVaStart;
    layout.va_range_end = NativeNs2GpuVaEnd;
    layout.va_range_split = NativeNs2GpuVaSmallSplit;
    const auto initialized = AllocAsEx(layout);
    if (initialized != NvResult::Success) return initialized;

    const auto bound = owner->BindImportedAddressSpace(
        u32{params.vm_id}, shared_from_this());
    if (bound != NvResult::Success) {
        if (!ResetFailedNativeVmImport()) {
            // A failed replay left physical ownership unproven. An
            // uncommitted VM id cannot be retired through RetireNativeVm.
            // Revoke this FD directly before any further ioctl/submission.
            PoisonNativeAddressSpace();
            m_native_vm_retired.store(true, std::memory_order_release);
            return NvResult::InvalidState;
        }
        return bound;
    }
    // GSV commits the local ID under its publication lock. Never rewrite it
    // here: the VM may already have been retired when the call returns.
    return NvResult::Success;
}

bool nvhost_as_gpu::CommitNativeVmImport(u32 id) {
    const std::scoped_lock lock{mutex};
    if (!id || m_native_vm_id || m_native_vm_retired || !NativeEpochCurrent() ||
        !vm.initialised || !gmmu)
        return false;
    m_native_vm_id = id;
    return true;
}

bool nvhost_as_gpu::ResetFailedNativeVmImport() {
    const std::scoped_lock lock{mutex};
    if (!mapping_map.empty() || !allocation_map.empty() ||
        !native_sparse_regions.empty() || !native_sparse_pins.empty() ||
        native_sparse_poisoned)
        return false;
    gmmu.reset();
    // FlatAllocator cannot be assigned, so neither std::optional allocator
    // nor this VM can be assigned from a temporary on the native PS5 toolchain.
    // Destroy both allocator owners explicitly and restore the default VA
    // geometry. The guard above proves no live mapping or pin survives.
    vm.big_page_allocator.reset();
    vm.small_page_allocator.reset();
    vm.big_page_size = VM::DEFAULT_BIG_PAGE_SIZE;
    vm.big_page_size_bits = static_cast<u32>(std::countr_zero<u32>(VM::DEFAULT_BIG_PAGE_SIZE));
    vm.va_range_start = static_cast<u64>(VM::DEFAULT_BIG_PAGE_SIZE) << VM::VA_START_SHIFT;
    vm.va_range_split = VM::DEFAULT_VA_SPLIT;
    vm.va_range_end = VM::DEFAULT_VA_RANGE;
    vm.initialised = false;
    nx2_syncpoint_shim = false;
    return true;
}

void nvhost_as_gpu::ReleaseNativeVmBinding() {
    u32 id{};
    Kernel::KProcess* process{};
    {
        const std::scoped_lock lock{mutex};
        if (!m_native_vm_id || !m_native_process) return;
        id = m_native_vm_id;
        process = m_native_process;
        m_native_vm_id = 0;
    }
    // Never hold the AS mutex while entering the GSV shared owner.
    if (auto owner = NativeGpuProcessRegistry::Instance().FindRegisteredVmOwner(
            process, id))
        owner->UnbindImportedAddressSpace(id, this);
}

bool nvhost_as_gpu::NativeEpochCurrent() const {
    return m_native_process && m_native_generation &&
           NativeGpuProcessRegistry::Instance().IsCurrent(
               {m_native_process, *m_native_generation});
}

void nvhost_as_gpu::RetireNativeVm(u32 id) {
    if (!id) return;
    const std::scoped_lock lock{mutex};
    if (m_native_vm_id != id) return;
    // Logical GSV ownership ends here, but ChannelState may still retain this
    // AS/GMMU until queued GPU payloads are destroyed. Fail future ioctls
    // closed and let the normal final AS destructor release physical backing.
    m_native_vm_id = 0;
    m_native_vm_retired.store(true, std::memory_order_release);
    m_native_submission_lease->store(false, std::memory_order_release);
}

void nvhost_as_gpu::PoisonNativeAddressSpace() noexcept {
    native_sparse_poisoned.store(true, std::memory_order_release);
    m_native_submission_lease->store(false, std::memory_order_release);
}

bool nvhost_as_gpu::NativeSubmissionAllowed() const {
    // The scheduler lease protects storage, not permission to enqueue work.
    // GSV may revoke the VM from a different service thread.
    // A failed destructive GMMU mutation leaves page-table ownership uncertain.
    // Do not admit new work on that AS, even while its epoch remains live.
    return m_native_submission_lease->load(std::memory_order_acquire) &&
           !m_native_vm_retired.load(std::memory_order_acquire) &&
           !native_sparse_poisoned.load(std::memory_order_acquire) &&
           NativeEpochCurrent();
}

std::shared_ptr<const std::atomic_bool> nvhost_as_gpu::CaptureNativeSubmissionLease() const {
    if (!NativeSubmissionAllowed()) return {};
    return m_native_submission_lease;
}

bool nvhost_as_gpu::OwnsNativeProcess(Kernel::KProcess* process) {
    const std::scoped_lock lock{mutex};
    return process != nullptr && m_native_process == process &&
           !m_native_vm_retired && NativeEpochCurrent() &&
           vm.initialised && gmmu;
}

// NS2 research: operate on a real, exclusive, 4-KiB guest GMMU allocation.
// Unlike the old fixed IOCTL path, validation+reservation+mapping happen
// under one address-space mutex and no failed/overlap request returns success.
NvResult nvhost_as_gpu::MapNative4K(u32 handle_id, u64 address, u64 source, u64 size,
                                          u8 kind, bool gpu_cacheable) {
    constexpr u64 page = VM::YUZU_PAGESIZE;
    if (!m_native_process || !handle_id || !size || (address | source | size) & (page - 1) ||
        (size >> VM::PAGE_SIZE_BITS) > std::numeric_limits<u32>::max() ||
        source > std::numeric_limits<u64>::max() - size)
        return NvResult::BadParameter;
    const auto handle = nvmap.GetHandle(handle_id);
    if (!handle || !handle->allocated || source > handle->orig_size ||
        size > handle->orig_size - source)
        return NvResult::BadParameter;
    const auto* session = container.GetSession(handle->session_id);
    if (!session || session->process != m_native_process)
        return NvResult::BadParameter;

    const std::scoped_lock lock{mutex};
    if (!vm.initialised || !gmmu || native_sparse_poisoned ||
        address < vm.va_range_start || address >= vm.va_range_split ||
        size > vm.va_range_split - address || !vm.small_page_allocator)
        return NvResult::BadParameter;

    // Raw 0xe3 / worker 211 may map a 4-KiB subrange of one already-reserved
    // 214 sparse VA. That range already owns its allocator pages; a 211 view
    // owns only the physical pin and PTEs. This mirrors OunceVK's distinction
    // between a VA lease and a mapping of registered backing into that lease.
    bool sparse_overlay = false;
    const auto sparse_owner = native_sparse_regions.upper_bound(address);
    if (sparse_owner != native_sparse_regions.begin()) {
        const auto previous = std::prev(sparse_owner);
        sparse_overlay =
            address >= previous->first &&
            address - previous->first < previous->second.size &&
            size <= previous->second.size - (address - previous->first);
    }

    // A 216 physical remap and a 211 overlay must never own the same PTE.
    // Unbacked (handle==0) sparse spans are replaceable; physically backed
    // 216 spans remain exclusive until 216/102 restores them.
    if (sparse_overlay) {
        const u64 end = address + size;
        const auto& sparse_ranges = native_sparse_mappings.Ranges();
        auto range = sparse_ranges.lower_bound(address);
        if (range != sparse_ranges.begin()) --range;
        for (; range != sparse_ranges.end() && range->second.address < end; ++range) {
            const auto& current = range->second;
            if (!current.size || current.address > std::numeric_limits<u64>::max() - current.size)
                return NvResult::InvalidState;
            if (current.address + current.size <= address) continue;
            if (current.handle) return NvResult::BadParameter;
        }
    } else {
        // Ordinary 211 owns a fresh fixed VA. Sparse reservations are the one
        // intentional exception; every other allocation overlap stays invalid.
        const auto reservation = allocation_map.lower_bound(address);
        if ((reservation != allocation_map.end() && reservation->first < address + size) ||
            (reservation != allocation_map.begin() &&
             std::prev(reservation)->first + std::prev(reservation)->second.size > address))
            return NvResult::BadParameter;
    }

    const auto next = mapping_map.lower_bound(address);
    if ((next != mapping_map.end() && next->first < address + size) ||
        (next != mapping_map.begin() &&
         std::prev(next)->first + std::prev(next)->second.size > address))
        return NvResult::BadParameter;

    if ((address >> VM::PAGE_SIZE_BITS) > std::numeric_limits<u32>::max() ||
        ((address + size) >> VM::PAGE_SIZE_BITS) > std::numeric_limits<u32>::max())
        return NvResult::BadParameter;

    const DAddr pinned = nvmap.PinHandle(handle_id, false);
    if (!NativePinnedPhysicalSpan(pinned, source, size)) {
        if (pinned) nvmap.UnpinHandle(handle_id);
        return NvResult::InsufficientMemory;
    }
    if (((pinned + source) & (page - 1)) != 0) {
        nvmap.UnpinHandle(handle_id);
        return NvResult::BadValue;
    }

    const u32 first_page = static_cast<u32>(address >> VM::PAGE_SIZE_BITS);
    const u32 page_count = static_cast<u32>(size >> VM::PAGE_SIZE_BITS);
    bool va_reserved = false;
    try {
        if (!sparse_overlay) {
            vm.small_page_allocator->AllocateFixed(first_page, page_count);
            va_reserved = true;
            allocation_map.emplace(address,
                                   Allocation{{}, size, VM::YUZU_PAGESIZE, false, false});
            allocation_map.at(address).mappings.push_back(address);
        }
        mapping_map.emplace(address, Mapping(handle_id, pinned + source, address, size,
                                             true, false, sparse_overlay, gpu_cacheable));
    } catch (const std::bad_alloc&) {
        mapping_map.erase(address);
        if (!sparse_overlay) {
            allocation_map.erase(address);
            if (va_reserved) vm.small_page_allocator->Free(first_page, page_count);
        }
        nvmap.UnpinHandle(handle_id);
        return NvResult::InsufficientMemory;
    }

    try {
        gmmu->Map(address, pinned + source, size, static_cast<Tegra::PTEKind>(kind), false);
    } catch (const std::bad_alloc&) {
        // A partial 211 publication must return the pre-existing sparse pages
        // to sparse state, not unmap the reservation itself.
        try {
            if (sparse_overlay)
                gmmu->MapSparse(address, size, false);
            else
                gmmu->Unmap(address, size);
        } catch (const std::bad_alloc&) {
            PoisonNativeAddressSpace();
            return NvResult::InvalidState;
        }
        mapping_map.erase(address);
        if (!sparse_overlay) {
            allocation_map.erase(address);
            vm.small_page_allocator->Free(first_page, page_count);
        }
        nvmap.UnpinHandle(handle_id);
        return NvResult::InsufficientMemory;
    }
    return NvResult::Success;
}

NvResult nvhost_as_gpu::MapNativeImage(u32 handle_id, u64 source, u64 size, u64& address) {
    address = 0;
    constexpr u64 page = VM::YUZU_PAGESIZE;
    if (!m_native_process || !handle_id || !size || ((source | size) & (page - 1)) ||
        (size >> VM::PAGE_SIZE_BITS) > std::numeric_limits<u32>::max() ||
        source > std::numeric_limits<u64>::max() - size)
        return NvResult::BadParameter;
    const auto handle = nvmap.GetHandle(handle_id);
    if (!handle || !handle->allocated || source > handle->orig_size ||
        size > handle->orig_size - source)
        return NvResult::BadParameter;
    const auto* session = container.GetSession(handle->session_id);
    if (!session || session->process != m_native_process)
        return NvResult::BadParameter;

    const std::scoped_lock lock{mutex};
    if (!vm.initialised || !gmmu || !vm.small_page_allocator || native_sparse_poisoned)
        return NvResult::InvalidState;
    // A single allocation owns its VA and mapping. Do not use MapBufferEx:
    // its legacy failure path can leak a pin or overlap a reserved region.
    const u32 pages = static_cast<u32>(size >> VM::PAGE_SIZE_BITS);
    // Allocate() can throw while splitting the underlying FlatAllocator
    // vector, not only return zero on exhaustion. No pin exists yet.
    u32 allocated{};
    try {
        allocated = vm.small_page_allocator->Allocate(pages);
    } catch (const std::bad_alloc&) {
        return NvResult::InsufficientMemory;
    }
    if (!allocated) return NvResult::InsufficientMemory;
    const u64 gpu_address = static_cast<u64>(allocated) << VM::PAGE_SIZE_BITS;
    if (gpu_address < vm.va_range_start || gpu_address >= vm.va_range_split ||
        size > vm.va_range_split - gpu_address ||
        gpu_address > std::numeric_limits<u64>::max() - size ||
        ((gpu_address + size) >> VM::PAGE_SIZE_BITS) > std::numeric_limits<u32>::max()) {
        vm.small_page_allocator->Free(allocated, pages);
        return NvResult::BadParameter;
    }
    const DAddr pinned = nvmap.PinHandle(handle_id, false);
    if (!NativePinnedPhysicalSpan(pinned, source, size)) {
        if (pinned) nvmap.UnpinHandle(handle_id);
        vm.small_page_allocator->Free(allocated, pages);
        return NvResult::InsufficientMemory;
    }
    if (((pinned + source) & (page - 1)) != 0) {
        nvmap.UnpinHandle(handle_id);
        vm.small_page_allocator->Free(allocated, pages);
        return NvResult::BadValue;
    }
    // The VA allocator already reserved the pages. Mark the GMMU mapping
    // fixed so FreeMappingLocked will unpin and unmap, while UnmapNative4K
    // releases the single owning allocation exactly once.
    try {
        allocation_map.emplace(gpu_address,
                               Allocation{{gpu_address}, size, VM::YUZU_PAGESIZE, false, false});
        mapping_map.emplace(gpu_address, Mapping(handle_id, pinned + source,
                                                gpu_address, size, true, false, false));
    } catch (const std::bad_alloc&) {
        mapping_map.erase(gpu_address);
        allocation_map.erase(gpu_address);
        nvmap.UnpinHandle(handle_id);
        vm.small_page_allocator->Free(allocated, pages);
        return NvResult::InsufficientMemory;
    }
    try {
        gmmu->Map(gpu_address, pinned + source, size, Tegra::PTEKind{}, false);
    } catch (const std::bad_alloc&) {
        // Never release the physical backing of partially published GPU PTEs.
        try {
            gmmu->Unmap(gpu_address, size);
        } catch (const std::bad_alloc&) {
            PoisonNativeAddressSpace();
            return NvResult::InvalidState;
        }
        mapping_map.erase(gpu_address);
        allocation_map.erase(gpu_address);
        vm.small_page_allocator->Free(allocated, pages);
        nvmap.UnpinHandle(handle_id);
        return NvResult::InsufficientMemory;
    }
    address = gpu_address;
    return NvResult::Success;
}

NvResult nvhost_as_gpu::ReserveNativeSparse4K(u64 address, u64 size, u8 page_shift) {
    if ((page_shift != 12 && page_shift != 16) ||
        !m_native_process || !size ||
        ((address | size) & ((u64{1} << page_shift) - 1)) ||
        (size >> VM::PAGE_SIZE_BITS) > std::numeric_limits<u32>::max())
        return NvResult::BadParameter;
    const std::scoped_lock lock{mutex};
    if (!vm.initialised || !gmmu || !vm.small_page_allocator || native_sparse_poisoned ||
        address < vm.va_range_start || address >= vm.va_range_split ||
        size > vm.va_range_split - address ||
        native_sparse_regions.size() >= 4096)
        return NvResult::BadParameter;
    const auto reservation = allocation_map.lower_bound(address);
    if ((reservation != allocation_map.end() && reservation->first < address + size) ||
        (reservation != allocation_map.begin() &&
         std::prev(reservation)->first + std::prev(reservation)->second.size > address))
        return NvResult::BadParameter;
    const auto mapping = mapping_map.lower_bound(address);
    if ((mapping != mapping_map.end() && mapping->first < address + size) ||
        (mapping != mapping_map.begin() &&
         std::prev(mapping)->first + std::prev(mapping)->second.size > address))
        return NvResult::BadParameter;
    if ((address >> VM::PAGE_SIZE_BITS) > std::numeric_limits<u32>::max() ||
        ((address + size) >> VM::PAGE_SIZE_BITS) > std::numeric_limits<u32>::max())
        return NvResult::BadParameter;
    const u32 base = static_cast<u32>(address >> VM::PAGE_SIZE_BITS);
    const u32 pages = static_cast<u32>(size >> VM::PAGE_SIZE_BITS);
    // AllocateFixed can allocate and throw before any map node is inserted.
    // Only free a reservation that was actually returned successfully;
    // the FlatAllocator vector insert has the strong exception guarantee.
    bool va_reserved = false;
    try {
        vm.small_page_allocator->AllocateFixed(base, pages);
        va_reserved = true;
        allocation_map.emplace(address, Allocation{{}, size, VM::YUZU_PAGESIZE, true, false});
        native_sparse_regions.emplace(address, NativeSparseRegion{size, page_shift});
    } catch (const std::bad_alloc&) {
        native_sparse_regions.erase(address);
        allocation_map.erase(address);
        if (va_reserved) vm.small_page_allocator->Free(base, pages);
        return NvResult::InsufficientMemory;
    }
    try {
        gmmu->MapSparse(address, size, false);
    } catch (const std::bad_alloc&) {
        // Page-table construction can fail after modifying some PTEs.
        // Reuse the VA only if the incomplete GMMU mapping was retired.
        try {
            gmmu->Unmap(address, size);
        } catch (const std::bad_alloc&) {
            PoisonNativeAddressSpace();
            return NvResult::InvalidState;
        }
        native_sparse_regions.erase(address);
        allocation_map.erase(address);
        vm.small_page_allocator->Free(base, pages);
        return NvResult::InsufficientMemory;
    }
    return NvResult::Success;
}

// Remap the contents of already-reserved native 4-KiB sparse VA only.
// Validate the COMPLETE batch, prepare new pins and mapping-table ownership,
// then modify GMMU pages in order. No guest page-table change occurs before
// all errors that can be reported have been checked.
bool nvhost_as_gpu::IsNativeSparsePoisoned() {
    const std::scoped_lock lock{mutex};
    return native_sparse_poisoned;
}

NvResult nvhost_as_gpu::RemapNativeSparse(std::span<const NativeSparseRemapEntry> entries) {
    if (!m_native_process || entries.empty() || entries.size() > 256)
        return NvResult::BadParameter;
    const std::scoped_lock lock{mutex};
    if (!vm.initialised || !gmmu || native_sparse_poisoned)
        return NvResult::InvalidState;

    NativeSparseMappingTable updated;
    std::vector<NativeSparseRange> ranges;
    std::vector<NativeMemoryKind> kinds;
    std::vector<bool> big_page_ranges;
    std::set<u32> retained;
    std::set<u32> needed;
    // Prepare the entire replacement table and descriptor sets before any
    // NVMap pin or guest GMMU change. An OOM cannot escape the GPU worker
    // with a partially prepared sparse transaction.
    try {
        updated = native_sparse_mappings;
        ranges.reserve(entries.size());
        kinds.reserve(entries.size());
        big_page_ranges.reserve(entries.size());
        for (const auto& entry : entries) {
            auto normalized = entry;
            // The native A:0D ABI permits cacheability flags without an
            // explicit page-size selector. Resolve such entries from one
            // ALREADY OWNED sparse VA, never guess 4K or a big-page layout.
            // GSV 216 and compatibility A:14 supply their own selectors.
            if ((u32{entry.flags} & 0x7000U) == 0) {
                bool matched = false;
                for (const u32 bits : {0x1000U, 0x2000U, 0x4000U}) {
                    auto candidate = entry;
                    candidate.flags = u32{entry.flags} | bits;
                    const auto trial = DecodeNativeSparseRemap(candidate);
                    if (!trial) continue;
                    const u8 shift = bits == 0x1000U ? 12 : bits == 0x2000U ? 16 : 17;
                    bool owned_by_shift = false;
                    const auto region = native_sparse_regions.upper_bound(trial->address);
                    if (region != native_sparse_regions.begin()) {
                        const auto previous = std::prev(region);
                        owned_by_shift = previous->second.page_shift == shift &&
                            trial->address >= previous->first &&
                            trial->address - previous->first < previous->second.size &&
                            trial->size <= previous->second.size -
                                           (trial->address - previous->first);
                    }
                    if (!owned_by_shift && shift == vm.big_page_size_bits) {
                        const auto owner = NativeGpuAllocation::FindContaining(
                            allocation_map, trial->address, trial->size);
                        owned_by_shift = owner != allocation_map.end() &&
                            owner->second.sparse && owner->second.big_pages &&
                            owner->second.page_size == vm.big_page_size;
                    }
                    if (!owned_by_shift) continue;
                    if (matched) return NvResult::BadParameter; // Ambiguous guest VA.
                    normalized = candidate;
                    matched = true;
                }
                if (!matched) return NvResult::BadParameter;
            }
            const auto range = DecodeNativeSparseRemap(normalized);
            const u32 page_flags = u32{normalized.flags} & 0x7000U;
            const u8 page_shift = page_flags == 0x1000U ? 12 :
                                  page_flags == 0x2000U ? 16 :
                                  page_flags == 0x4000U ? 17 : 0;
            if (!range || !page_shift)
                return NvResult::BadParameter;
    
            // A GSV 214 owns 4K/64K sparse tiles. NVDRV A:14 owns a
            // big-page sparse allocation created by A:2. Both paths must
            // share the same pin and PTE replacement lifetime.
            bool owned = false;
            bool use_big_pages = false;
            const auto region = native_sparse_regions.upper_bound(range->address);
            if (region != native_sparse_regions.begin()) {
                const auto owner = std::prev(region);
                owned = owner->second.page_shift == page_shift &&
                        range->address >= owner->first &&
                        range->address - owner->first < owner->second.size &&
                        range->size <= owner->second.size -
                                           (range->address - owner->first);
            }
            if (!owned && page_shift == vm.big_page_size_bits) {
                const auto allocation = NativeGpuAllocation::FindContaining(
                    allocation_map, range->address, range->size);
                if (allocation != allocation_map.end() &&
                    allocation->second.sparse && allocation->second.big_pages &&
                    allocation->second.page_size == vm.big_page_size) {
                    owned = true;
                    use_big_pages = true;
                }
            }
            if (!owned) return NvResult::BadParameter;
            // A GSV 216 remap or native A:14 sparse update must not
            // overwrite a live MapBufferEx mapping with an independent
            // NVMap pin/owner. That would leave two ledgers claiming the
            // same GPU PTE, and one of the eventual unmaps would release
            // the other owner's backing.
            const auto mapped = mapping_map.lower_bound(range->address);
            if ((mapped != mapping_map.end() &&
                 mapped->first < range->address + range->size) ||
                (mapped != mapping_map.begin() &&
                 std::prev(mapped)->second.size >
                     range->address - std::prev(mapped)->first))
                return NvResult::BadParameter;

            NativeMemoryKind kind{};
            if (range->handle) {
                const auto handle = nvmap.GetHandle(range->handle);
                const auto* session = handle ? container.GetSession(handle->session_id) : nullptr;
                if (!handle || !handle->allocated || !session ||
                    session->process != m_native_process ||
                    range->source_offset > handle->orig_size ||
                    range->size > handle->orig_size - range->source_offset)
                    return NvResult::BadParameter;
                if (u16{entry.kind} > 255 || u16{entry.fallback_kind} > 255)
                    return NvResult::NotSupported;
                const auto selected = SelectNativeMemoryKind(
                    static_cast<u8>(u16{entry.kind}),
                    static_cast<u8>(u16{entry.fallback_kind}));
                if (!selected) return NvResult::NotSupported;
                kind = *selected;
            }
            updated.Replace(*range);
            ranges.push_back(*range);
            kinds.push_back(kind);
            big_page_ranges.push_back(use_big_pages);
        }
        if (updated.Ranges().size() > 65536) return NvResult::InsufficientMemory;
        retained = updated.ReferencedHandles();
        if (retained.size() > 4096) return NvResult::InsufficientMemory;
    
        // A handle overwritten later in this batch still needs a pin until
        // all intermediate GMMU updates are applied in order.
        needed = retained;
        for (const auto& range : ranges) if (range.handle) needed.insert(range.handle);
        if (needed.size() > 4096) return NvResult::InsufficientMemory;
    } catch (const std::bad_alloc&) {
        return NvResult::InsufficientMemory;
    }
    std::map<u32, DAddr> newly_pinned;
    // Allocate the map node BEFORE asking NVMap to pin physical memory.
    // Otherwise std::map::emplace can OOM after a successful PinHandle,
    // orphaning a physical pin with no owner or rollback handle.
    for (const u32 handle : needed) {
        if (native_sparse_pins.contains(handle)) continue;
        std::map<u32, DAddr>::iterator pending;
        try {
            pending = newly_pinned.try_emplace(handle, DAddr{}).first;
        } catch (const std::bad_alloc&) {
            for (const auto& [id, address] : newly_pinned)
                if (address) nvmap.UnpinHandle(id);
            return NvResult::InsufficientMemory;
        }
        const DAddr pinned = nvmap.PinHandle(handle, false);
        if (!pinned) {
            newly_pinned.erase(pending);
            for (const auto& [id, address] : newly_pinned)
                nvmap.UnpinHandle(id);
            return NvResult::InsufficientMemory;
        }
        pending->second = pinned;
    }

    // Preflight every pinned source span before changing ANY GMMU page.
    // A wrapped physical offset in a later sparse entry must not leave the
    // earlier entries committed to the wrong host allocation.
    for (std::size_t i = 0; i < ranges.size(); ++i) {
        const auto& range = ranges[i];
        if (!range.handle) continue;
        const auto old = native_sparse_pins.find(range.handle);
        const DAddr pinned = old == native_sparse_pins.end()
                                 ? newly_pinned.at(range.handle) : old->second;
        const u64 alignment = big_page_ranges[i] ? vm.big_page_size : VM::YUZU_PAGESIZE;
        if (!NativePinnedPhysicalSpan(pinned, range.source_offset, range.size) ||
            ((pinned + range.source_offset) & (alignment - 1)) != 0) {
            // No GMMU mutation has begun; every newly pinned NvMap owner
            // is still local and can be safely released on rejection.
            for (const auto& [id, address] : newly_pinned) nvmap.UnpinHandle(id);
            return NvResult::BadParameter;
        }
    }
    // Publish every physical pin owner BEFORE touching a guest PTE.
    // If the GPU page table itself OOMs mid-batch, keep all old and new
    // backing pinned and freeze sparse mutation until channel teardown.
    // Releasing either set while a partially mapped page survives is UAF.
    native_sparse_pins.merge(newly_pinned);
    try {
        for (std::size_t i = 0; i < ranges.size(); ++i) {
            const auto& range = ranges[i];
            if (!range.handle) {
                gmmu->MapSparse(range.address, range.size, big_page_ranges[i]);
                continue;
            }
            const auto pinned = native_sparse_pins.find(range.handle);
            if (pinned == native_sparse_pins.end()) {
                PoisonNativeAddressSpace();
                return NvResult::InvalidState;
            }
            gmmu->Map(range.address, pinned->second + range.source_offset, range.size,
                      static_cast<Tegra::PTEKind>(kinds[i].backend), big_page_ranges[i]);
        }
    } catch (const std::bad_alloc&) {
        PoisonNativeAddressSpace();
        return NvResult::InsufficientMemory;
    }
    // Replacement is complete. Only now may old physical backing retire.
    native_sparse_mappings = std::move(updated);
    for (auto it = native_sparse_pins.begin(); it != native_sparse_pins.end();) {
        if (retained.contains(it->first)) { ++it; continue; }
        nvmap.UnpinHandle(it->first);
        it = native_sparse_pins.erase(it);
    }
    return NvResult::Success;
}

NvResult nvhost_as_gpu::SnapshotNativeSparseHandle(
    u32 handle_id, std::vector<NativeSparseRange>& ranges) {
    if (!handle_id) return NvResult::BadParameter;
    const std::scoped_lock lock{mutex};
    if (!vm.initialised || !gmmu) return NvResult::InvalidState;

    ranges.clear();
    try {
        const auto count = std::count_if(
            native_sparse_mappings.Ranges().begin(),
            native_sparse_mappings.Ranges().end(),
            [handle_id](const auto& item) { return item.second.handle == handle_id; });
        ranges.reserve(count);
        for (const auto& [address, range] : native_sparse_mappings.Ranges()) {
            (void)address;
            if (range.handle != handle_id) continue;
            // The GPU worker sub-102 only owns its own GSV 214/216 sparse
            // regions; a native NVDRV A:14 big-page alias is external to
            // this worker and must keep its pin until A:3 FreeSpace.
            // Do not synthesize a GSV 216 restore for an alien allocation.
            const auto owner = native_sparse_regions.upper_bound(range.address);
            if (owner == native_sparse_regions.begin()) continue;
            const auto previous = std::prev(owner);
            if (range.address >= previous->first &&
                range.address - previous->first < previous->second.size &&
                range.size <= previous->second.size -
                                  (range.address - previous->first)) {
                ranges.push_back(range);
            }
        }
    } catch (const std::bad_alloc&) {
        ranges.clear();
        return NvResult::InsufficientMemory;
    }
    return NvResult::Success;
}

NvResult nvhost_as_gpu::ReleaseNativeSparse4K(u64 address, u64 size) {
    constexpr u64 page = VM::YUZU_PAGESIZE;
    if (!size || ((address | size) & (page - 1)) ||
        (size >> VM::PAGE_SIZE_BITS) > std::numeric_limits<u32>::max())
        return NvResult::BadParameter;
    const std::scoped_lock lock{mutex};
    if (native_sparse_poisoned) return NvResult::InvalidState;
    const auto owned = native_sparse_regions.find(address);
    const auto allocation = allocation_map.find(address);
    if (!vm.initialised || !gmmu || !vm.small_page_allocator ||
        owned == native_sparse_regions.end() || owned->second.size != size ||
        allocation == allocation_map.end() || !allocation->second.sparse ||
        allocation->second.big_pages || allocation->second.page_size != page ||
        allocation->second.size != size || !allocation->second.mappings.empty())
        return NvResult::BadParameter;
    // Refuse disposal of a VA range overlaid by a legacy or direct map.
    auto mapping = mapping_map.lower_bound(address);
    if ((mapping != mapping_map.end() && mapping->first < address + size) ||
        (mapping != mapping_map.begin() &&
         std::prev(mapping)->first + std::prev(mapping)->second.size > address))
        return NvResult::BadParameter;
    // Purge all physical mappings covered by this reservation; preserve
    // handles still referenced by other independent native sparse regions.
    NativeSparseMappingTable updated;
    std::set<u32> retained;
    // Prepare ALL owning data before changing the guest GMMU or pin ledger.
    // Copy/Replace/ReferencedHandles can allocate; a host OOM must leave the
    // reservation, mappings and pins intact and return a guest-visible error.
    try {
        updated = native_sparse_mappings;
        updated.Replace(NativeSparseRange{address, size, 0, 0});
        retained = updated.ReferencedHandles();
    } catch (const std::bad_alloc&) {
        return NvResult::InsufficientMemory;
    }
    try {
        gmmu->Unmap(address, size);
        // FlatAllocator::Free can split a coalesced block and allocate.
        // Keep every pin and both ledgers until VA retirement succeeds.
        vm.small_page_allocator->Free(static_cast<u32>(address >> VM::PAGE_SIZE_BITS),
                                      static_cast<u32>(size >> VM::PAGE_SIZE_BITS));
    } catch (const std::bad_alloc&) {
        // GMMU or VA retirement may be partial. Quarantine instead of
        // publishing a retryable pre-mutation failure or dropping backing.
        PoisonNativeAddressSpace();
        return NvResult::InvalidState;
    }
    native_sparse_mappings = std::move(updated);
    for (auto it = native_sparse_pins.begin(); it != native_sparse_pins.end();) {
        if (retained.contains(it->first)) { ++it; continue; }
        nvmap.UnpinHandle(it->first);
        it = native_sparse_pins.erase(it);
    }
    allocation_map.erase(allocation);
    native_sparse_regions.erase(owned);
    return NvResult::Success;
}

NvResult nvhost_as_gpu::UnmapNative4K(u64 address, u64 size) {
    constexpr u64 page = VM::YUZU_PAGESIZE;
    if (!size || (address | size) & (page - 1) ||
        (size >> VM::PAGE_SIZE_BITS) > std::numeric_limits<u32>::max())
        return NvResult::BadParameter;
    const std::scoped_lock lock{mutex};
    const auto mapping = mapping_map.find(address);
    if (!vm.initialised || !gmmu || !vm.small_page_allocator || native_sparse_poisoned ||
        mapping == mapping_map.end() || mapping->second.size != size ||
        !mapping->second.fixed)
        return NvResult::BadParameter;

    if (mapping->second.sparse_alloc) {
        // Worker 213 retires only the 211 physical view. The 214 reservation
        // and its allocator ownership survive and the PTEs return to sparse.
        bool owned = false;
        const auto region = native_sparse_regions.upper_bound(address);
        if (region != native_sparse_regions.begin()) {
            const auto previous = std::prev(region);
            owned = address >= previous->first &&
                    address - previous->first < previous->second.size &&
                    size <= previous->second.size - (address - previous->first);
        }
        if (!owned || mapping->second.big_page)
            return NvResult::BadParameter;
        try {
            gmmu->MapSparse(address, size, false);
        } catch (const std::bad_alloc&) {
            PoisonNativeAddressSpace();
            return NvResult::InvalidState;
        }
        nvmap.UnpinHandle(mapping->second.handle);
        mapping_map.erase(mapping);
        map_buffer_offsets.erase(static_cast<s64>(address));
        return NvResult::Success;
    }

    const auto reservation = allocation_map.find(address);
    if (reservation == allocation_map.end() || reservation->second.size != size ||
        reservation->second.page_size != page ||
        reservation->second.mappings.size() != 1 ||
        reservation->second.mappings[0] != address || reservation->second.sparse)
        return NvResult::BadParameter;

    try {
        gmmu->Unmap(address, size);
        vm.small_page_allocator->Free(static_cast<u32>(address >> VM::PAGE_SIZE_BITS),
                                      static_cast<u32>(size >> VM::PAGE_SIZE_BITS));
    } catch (const std::bad_alloc&) {
        PoisonNativeAddressSpace();
        return NvResult::InvalidState;
    }
    nvmap.UnpinHandle(mapping->second.handle);
    mapping_map.erase(mapping);
    allocation_map.erase(reservation);
    map_buffer_offsets.erase(static_cast<s64>(address));
    return NvResult::Success;
}

NvResult nvhost_as_gpu::ModifyNativeKind(u64 address, u64 size, u8 kind) {
    constexpr u64 small_page = VM::YUZU_PAGESIZE;
    if (!size || ((address | size) & (small_page - 1)) ||
        address > std::numeric_limits<u64>::max() - size)
        return NvResult::BadParameter;
    const std::scoped_lock lock{mutex};
    if (!vm.initialised || !gmmu || native_sparse_poisoned ||
        address >= vm.va_range_end || size > vm.va_range_end - address)
        return NvResult::InvalidState;

    // The direct NvMap view is a distinct pin owner. Preserve this
    // existing path, including its exact GPU PTE page class.
    auto direct = mapping_map.upper_bound(address);
    if (direct != mapping_map.begin()) {
        --direct;
        const auto& mapping = direct->second;
        if (address >= direct->first &&
            address - direct->first < mapping.size) {
            const u64 delta = address - direct->first;
            if (size > mapping.size - delta)
                return NvResult::BadParameter;
            const u64 page = mapping.big_page ? vm.big_page_size : small_page;
            if (((address | size) & (page - 1)) ||
                !NativePinnedPhysicalSpan(mapping.ptr, delta, size) ||
                ((mapping.ptr + delta) & (page - 1)))
                return NvResult::BadParameter;
            try {
                gmmu->Map(address, mapping.ptr + delta, size,
                          static_cast<Tegra::PTEKind>(kind), mapping.big_page);
            } catch (const std::bad_alloc&) {
                PoisonNativeAddressSpace();
                return NvResult::InsufficientMemory;
            }
            return NvResult::Success;
        }
    }

    // A:0x0D/A:0x14 and GSV216 share one native sparse pin ledger.
    // The earlier consolidated version searched mapping_map only:
    // sparse views could never change kind despite valid GPU backing.
    // Admit exactly one owned sparse allocation, never an unknown VA.
    bool big_page = false;
    bool owned = false;
    const auto region = native_sparse_regions.upper_bound(address);
    if (region != native_sparse_regions.begin()) {
        const auto& prior = *std::prev(region);
        owned = address >= prior.first &&
                address - prior.first < prior.second.size &&
                size <= prior.second.size - (address - prior.first);
    }
    if (!owned) {
        const auto allocation = NativeGpuAllocation::FindContaining(
            allocation_map, address, size);
        if (allocation != allocation_map.end() && allocation->second.sparse) {
            owned = true;
            big_page = allocation->second.big_pages;
        }
    }
    if (!owned)
        return NvResult::BadParameter;
    const u64 page = big_page ? vm.big_page_size : small_page;
    if (((address | size) & (page - 1)) != 0)
        return NvResult::BadParameter;

    const auto& ranges = native_sparse_mappings.Ranges();
    const u64 end = address + size;
    u64 cursor = address;
    // No fallible GMMU mutation before ALL sparse segments and physical
    // backing have passed validation. A hole or unpinned alias is an error,
    // not an implicit zero-filled successful kind change.
    while (cursor < end) {
        auto it = ranges.upper_bound(cursor);
        if (it == ranges.begin()) return NvResult::BadParameter;
        --it;
        const auto& range = it->second;
        if (cursor < range.address || cursor - range.address >= range.size ||
            !range.handle)
            return NvResult::BadParameter;
        const u64 delta = cursor - range.address;
        const u64 length = std::min(end - cursor, range.size - delta);
        if (!length ||
            range.source_offset > std::numeric_limits<u64>::max() - delta)
            return NvResult::BadParameter;
        const u64 source = range.source_offset + delta;
        const auto pin = native_sparse_pins.find(range.handle);
        if (pin == native_sparse_pins.end() ||
            !NativePinnedPhysicalSpan(pin->second, source, length) ||
            ((pin->second + source) & (page - 1)))
            return NvResult::BadParameter;
        cursor += length;
    }

    // All old and new pinned owners remain live until the page table
    // changes finish. An OOM after a partial map quarantines them rather
    // than allowing an unpin/unmap against still-referenced guest backing.
    cursor = address;
    try {
        while (cursor < end) {
            const auto it = std::prev(ranges.upper_bound(cursor));
            const auto& range = it->second;
            const u64 delta = cursor - range.address;
            const u64 length = std::min(end - cursor, range.size - delta);
            const auto pin = native_sparse_pins.find(range.handle);
            gmmu->Map(cursor, pin->second + range.source_offset + delta, length,
                      static_cast<Tegra::PTEKind>(kind), big_page);
            cursor += length;
        }
    } catch (const std::bad_alloc&) {
        PoisonNativeAddressSpace();
        return NvResult::InsufficientMemory;
    }
    return NvResult::Success;
}

NvResult nvhost_as_gpu::Nx2GetSyncpointShimInfo(IoctlNx2SyncpointShimInfo& params) {
    std::scoped_lock lock{mutex};
    if (!m_native_process ||
        !NativeGpuProcessRegistry::Instance().Contains(m_native_process) ||
        !vm.initialised || !gmmu || native_sparse_poisoned ||
        vm.va_range_end < VM::YUZU_PAGESIZE) {
        return NvResult::NotSupported;
    }

    const u64 address = vm.va_range_end - VM::YUZU_PAGESIZE;
    if (!Common::IsAligned(address, u64{VM::YUZU_PAGESIZE})) {
        return NvResult::BadValue;
    }
    if (!nx2_syncpoint_shim) {
        gmmu->SetNativeSyncpointShim(address);
        nx2_syncpoint_shim = true;
    }
    params.address = address;
    params.stride = sizeof(u32);
    params.count = Tegra::MemoryManager::NumShimSyncpoints;
    LOG_INFO(Service_NVDRV, "EDEN_NS2_SYNCPOINT_SHIM address={:#x} stride={} count={}",
             address, params.stride, params.count);
    return NvResult::Success;
}

NvResult nvhost_as_gpu::AllocAsEx(IoctlAllocAsEx& params) {
    LOG_DEBUG(Service_NVDRV, "called, big_page_size={:#x}", params.big_page_size);

    std::scoped_lock lock(mutex);

    if (vm.initialised) {
        // An NS2 guest may retry ALLOC_AS/ALLOC_AS_EX on an already open FD.
        // This is an invalid ioctl state, not a reason to assert or stop the
        // emulator's host process. Preserve the legacy Maxwell diagnostic.
        if (m_native_process &&
            NativeGpuProcessRegistry::Instance().Contains(m_native_process))
            return NvResult::InvalidState;
        ASSERT_MSG(false, "Cannot initialise an address space twice!");
        return NvResult::InvalidState;
    }

    // Validate the entire proposed address space before touching VM state.
    // FlatAllocator expects absolute upper bounds; subtracting the split
    // truncates the available high VA region and misreports GPU partitions.
    const bool native = m_native_process &&
        NativeGpuProcessRegistry::Instance().Contains(m_native_process);
    // The documented native ALLOC_AS_EX ignores as_fd and uses zero.
    // No synchronised cross-FD VA allocator or GMMU ownership contract
    // exists for a nonzero parent descriptor: never pretend to share an
    // active GPU page table with independent allocators.
    if (native && params.as_fd != 0)
        return NvResult::NotSupported;
    u32 big_page_size = vm.big_page_size;
    u64 start = native ? NativeNs2GpuVaStart : vm.va_range_start;
    u64 split = native ? NativeNs2GpuVaSmallSplit : vm.va_range_split;
    u64 end = native ? NativeNs2GpuVaEnd : vm.va_range_end;
    if (params.big_page_size) {
        // NVN's flags-5 explicit VA view may request only 4-KiB pages
        // (big_page_size=0x1000, split=0). The backend still needs a
        // standard big-page geometry for its *empty* large-page table;
        // accepting 0x1000 as an actual big-page size would alias the
        // two PTE tables. An ordinary AS never enters this exception.
        const bool native_small_only =
            native && u32{params.flags} == 5 &&
            u32{params.big_page_size} == VM::YUZU_PAGESIZE &&
            u64{params.va_range_start} != 0 && u64{params.va_range_split} == 0;
        if (!native_small_only &&
            (!std::has_single_bit(u32{params.big_page_size}) ||
             (u32{params.big_page_size} & VM::SUPPORTED_BIG_PAGE_SIZES) == 0))
            return NvResult::BadValue;
        if (!native_small_only)
            big_page_size = params.big_page_size;
        // Maxwell derives its default VA base from the big-page size.
        // Native NS2 has a 1-TiB GPU VA layout even for compact A:7 or
        // A:9 calls without explicit bounds; do not silently fall back to
        // the old 128-GiB Maxwell address space.
        if (!native)
            start = static_cast<u64>(big_page_size) << VM::VA_START_SHIFT;
    }
    if (params.va_range_start) {
        start = params.va_range_start;
        split = params.va_range_split;
        end = params.va_range_end;
        // Native 4-KiB views and some standalone NVN clients pass no
        // big-page split at all. This means the entire [start,end) range
        // is small-page VA, NOT an invalid VA layout. Keep the big-page
        // region empty rather than guessing a smaller page-space limit.
        if (native && split == 0)
            split = end;
    }
    const auto layout = PlanNativeGpuVaLayout(start, split, end, big_page_size);
    if (!layout) return NvResult::BadValue;

    const u64 max_big_page_bits = Common::Log2Ceil(end);
    const u32 big_page_bits = static_cast<u32>(std::countr_zero(big_page_size));
    // FlatAllocator initializes dynamic address blocks and the GMMU owns
    // dynamically allocated page tables. Build all three before publishing
    // new VM parameters or handing a live GMMU to the GPU. A partial host
    // OOM must leave the FD uninitialized and retryable, not advertise a
    // half-created native GPU address space to its TIPC worker.
    try {
        vm.small_page_allocator.emplace(layout->small_begin, layout->small_end);
        vm.big_page_allocator.emplace(layout->big_begin, layout->big_end);
        gmmu = std::make_unique<Tegra::MemoryManager>(
            system, max_big_page_bits, split, big_page_bits, VM::PAGE_SIZE_BITS);
    } catch (const std::bad_alloc&) {
        gmmu.reset();
        vm.big_page_allocator.reset();
        vm.small_page_allocator.reset();
        return NvResult::InsufficientMemory;
    }
    // No more fallible allocator construction remains between publishing the
    // selected VA layout and binding the real GMMU to the host GPU.
    vm.big_page_size = big_page_size;
    vm.big_page_size_bits = big_page_bits;
    vm.va_range_start = start;
    vm.va_range_split = split;
    vm.va_range_end = end;
    system.GPU().InitAddressSpace(*gmmu);
    vm.initialised = true;

    return NvResult::Success;
}

NvResult nvhost_as_gpu::AllocateSpace(IoctlAllocSpace& params) {
    LOG_DEBUG(Service_NVDRV, "called, pages={:X}, page_size={:X}, flags={:X}", params.pages,
              params.page_size, params.flags);

    std::scoped_lock lock(mutex);

    if (native_sparse_poisoned && m_native_process &&
        NativeGpuProcessRegistry::Instance().Contains(m_native_process))
        return NvResult::InvalidState;
    if (!vm.initialised) {
        return NvResult::BadValue;
    }

    if (params.page_size != VM::YUZU_PAGESIZE && params.page_size != vm.big_page_size) {
        return NvResult::BadValue;
    }

    // Native GA10F can reserve sparse 4-KiB GPU VA without backing physical
    // pages. The original Maxwell ioctl below deliberately keeps its legacy
    // unsupported-small-sparse contract; never select this path by Title ID.
    const bool small_sparse =
        params.page_size == VM::YUZU_PAGESIZE &&
        ((params.flags & MappingFlags::Sparse) != MappingFlags::None);
    if (small_sparse && m_native_process &&
        NativeGpuProcessRegistry::Instance().Contains(m_native_process)) {
        if (!gmmu || !vm.small_page_allocator || params.pages == 0)
            return NvResult::BadValue;
        constexpr u32 allowed_flags =
            static_cast<u32>(MappingFlags::Fixed) | static_cast<u32>(MappingFlags::Sparse);
        if ((static_cast<u32>(params.flags) & ~allowed_flags) != 0)
            return NvResult::BadValue;

        // All VA arithmetic is 64-bit; the actual allocator stores 4K page
        // numbers in u32. A reservation never commits pages or pins NVMap.
        auto& allocator = *vm.small_page_allocator;
        const u64 first_va = static_cast<u64>(allocator.GetVAStart()) << VM::PAGE_SIZE_BITS;
        const u64 limit_va = static_cast<u64>(allocator.GetVALimit()) << VM::PAGE_SIZE_BITS;
        const u64 size = static_cast<u64>(params.pages) * VM::YUZU_PAGESIZE;
        if (size > limit_va - first_va)
            return NvResult::InsufficientMemory;
        const bool fixed = (params.flags & MappingFlags::Fixed) != MappingFlags::None;
        u64 address = fixed ? static_cast<u64>(params.offset) : 0;
        if (fixed) {
            if (address < first_va || address >= limit_va ||
                (address & (VM::YUZU_PAGESIZE - 1)) != 0 ||
                size > limit_va - address)
                return NvResult::BadValue;
            // FlatAllocator::AllocateFixed does not reject an overlapping
            // reservation itself. An alias must not steal another guest VA,
            // including existing fixed maps from MapBufferEx.
            const auto reserved = allocation_map.lower_bound(address);
            if ((reserved != allocation_map.end() &&
                 reserved->first < address + size) ||
                (reserved != allocation_map.begin() &&
                 std::prev(reserved)->second.size >
                     address - std::prev(reserved)->first))
                return NvResult::BadValue;
            const auto mapped = mapping_map.lower_bound(address);
            if ((mapped != mapping_map.end() &&
                 mapped->first < address + size) ||
                (mapped != mapping_map.begin() &&
                 std::prev(mapped)->second.size >
                     address - std::prev(mapped)->first))
                return NvResult::BadValue;
        }

        u32 first_page{};
        bool reserved_pages = false;
        bool tracked = false;
        try {
            if (fixed) {
                first_page = static_cast<u32>(address >> VM::PAGE_SIZE_BITS);
                allocator.AllocateFixed(first_page, params.pages);
            } else {
                first_page = allocator.Allocate(params.pages);
                if (!first_page)
                    return NvResult::InsufficientMemory;
                address = static_cast<u64>(first_page) << VM::PAGE_SIZE_BITS;
            }
            reserved_pages = true;
            const auto [it, inserted] = allocation_map.try_emplace(
                address, Allocation{{}, size, VM::YUZU_PAGESIZE, true, false});
            if (!inserted) {
                allocator.Free(first_page, params.pages);
                return NvResult::BadValue;
            }
            tracked = true;
            // The last operation only marks GMMU pages Reserved, not
            // Mapped. A later fixed MapBufferEx/worker sparse remap is
            // responsible for real backing and the corresponding fence.
            try {
                gmmu->MapSparse(address, size, false);
            } catch (const std::bad_alloc&) {
                // The page-table operation may have partially reserved VA.
                // Only release its ledger after undoing those GPU PTEs.
                try {
                    gmmu->Unmap(address, size);
                } catch (const std::bad_alloc&) {
                    // GMMU rollback failed after a partial MapSparse.
                    // Retain VA ownership and quarantine all further native
                    // mutations; an untracked live PTE must not be reused.
                    PoisonNativeAddressSpace();
                    return NvResult::InvalidState;
                }
                allocation_map.erase(address);
                allocator.Free(first_page, params.pages);
                return NvResult::InsufficientMemory;
            }
        } catch (const std::bad_alloc&) {
            if (tracked) allocation_map.erase(address);
            if (reserved_pages) allocator.Free(first_page, params.pages);
            return NvResult::InsufficientMemory;
        }
        params.offset = address;
        return NvResult::Success;
    }

    // NS2 regular 4K/big-page and big-page sparse reservations must not
    // publish a guest VA without a live owner in allocation_map. The
    // already-admitted native 4K sparse branch above is unchanged.
    // Legacy Maxwell follows its original path below.
    if (m_native_process &&
        NativeGpuProcessRegistry::Instance().Contains(m_native_process)) {
        constexpr u32 allowed_flags =
            static_cast<u32>(MappingFlags::Fixed) | static_cast<u32>(MappingFlags::Sparse);
        // The VA allocator counts 4K or negotiated big-page units.
        // An arbitrary page_size would reserve N small pages but publish
        // N*page_size bytes of GPU VA, aliasing an adjacent reservation.
        // Validate the exact page class BEFORE any allocator/GMMU change.
        if (!gmmu || !vm.small_page_allocator || !vm.big_page_allocator ||
            !params.pages ||
            (params.page_size != VM::YUZU_PAGESIZE &&
             params.page_size != vm.big_page_size) ||
            (static_cast<u32>(params.flags) & ~allowed_flags))
            return NvResult::BadValue;
        const bool big = params.page_size == vm.big_page_size;
        const bool fixed = (params.flags & MappingFlags::Fixed) != MappingFlags::None;
        const bool sparse = (params.flags & MappingFlags::Sparse) != MappingFlags::None;
        const u32 shift = big ? vm.big_page_size_bits : VM::PAGE_SIZE_BITS;
        auto& allocator = big ? *vm.big_page_allocator : *vm.small_page_allocator;
        const u64 min_va = static_cast<u64>(allocator.GetVAStart()) << shift;
        const u64 max_va = static_cast<u64>(allocator.GetVALimit()) << shift;
        const u64 size = static_cast<u64>(params.pages) * params.page_size;
        if (min_va >= max_va || !size || size > max_va - min_va)
            return NvResult::InsufficientMemory;

        u64 address = fixed ? static_cast<u64>(params.offset) : 0;
        u32 start_page = 0;
        if (fixed) {
            if (address < min_va || address >= max_va ||
                (address & (params.page_size - 1)) ||
                size > max_va - address)
                return NvResult::BadValue;
            const auto next_allocation = allocation_map.lower_bound(address);
            if ((next_allocation != allocation_map.end() &&
                 next_allocation->first < address + size) ||
                (next_allocation != allocation_map.begin() &&
                 std::prev(next_allocation)->second.size >
                     address - std::prev(next_allocation)->first))
                return NvResult::BadValue;
            const auto next_mapping = mapping_map.lower_bound(address);
            if ((next_mapping != mapping_map.end() &&
                 next_mapping->first < address + size) ||
                (next_mapping != mapping_map.begin() &&
                 std::prev(next_mapping)->second.size >
                     address - std::prev(next_mapping)->first))
                return NvResult::BadValue;
            start_page = static_cast<u32>(address >> shift);
        }

        bool reserved = false;
        try {
            if (fixed) {
                allocator.AllocateFixed(start_page, params.pages);
            } else {
                start_page = allocator.Allocate(params.pages);
                if (!start_page) return NvResult::InsufficientMemory;
                address = static_cast<u64>(start_page) << shift;
            }
            reserved = true;
            const auto [entry, inserted] = allocation_map.try_emplace(
                address, Allocation{{}, size, params.page_size, sparse, big});
            (void)entry;
            if (!inserted) {
                allocator.Free(start_page, params.pages);
                return NvResult::BadValue;
            }
        } catch (const std::bad_alloc&) {
            if (reserved) {
                allocation_map.erase(address);
                allocator.Free(start_page, params.pages);
            }
            return NvResult::InsufficientMemory;
        }
        if (sparse) {
            try {
                gmmu->MapSparse(address, size, big);
            } catch (const std::bad_alloc&) {
                // MapSparse may already have touched PTEs. Retain the
                // reservation if the rollback itself cannot complete:
                // freeing potentially live GPU VA would be unsafe.
                try {
                    gmmu->Unmap(address, size);
                } catch (const std::bad_alloc&) {
                    // The GMMU might still contain some reserved PTEs.
                    // Do not let a later ioctl overwrite these stale pages
                    // or release their owning VA on a second failed unwind.
                    PoisonNativeAddressSpace();
                    return NvResult::InvalidState;
                }
                allocation_map.erase(address);
                allocator.Free(start_page, params.pages);
                return NvResult::InsufficientMemory;
            }
        }
        params.offset = address;
        return NvResult::Success;
    }

    if (params.page_size != vm.big_page_size &&
        ((params.flags & MappingFlags::Sparse) != MappingFlags::None)) {
        UNIMPLEMENTED_MSG("Sparse small pages are not implemented!");
        return NvResult::NotImplemented;
    }

    const u32 page_size_bits{params.page_size == VM::YUZU_PAGESIZE ? VM::PAGE_SIZE_BITS
                                                                   : vm.big_page_size_bits};

    auto& allocator{params.page_size == VM::YUZU_PAGESIZE ? *vm.small_page_allocator
                                                          : *vm.big_page_allocator};

    if ((params.flags & MappingFlags::Fixed) != MappingFlags::None) {
        allocator.AllocateFixed(static_cast<u32>(params.offset >> page_size_bits), params.pages);
    } else {
        params.offset = static_cast<u64>(allocator.Allocate(params.pages)) << page_size_bits;
        if (!params.offset) {
            ASSERT_MSG(false, "Failed to allocate free space in the GPU AS!");
            return NvResult::InsufficientMemory;
        }
    }

    u64 size{static_cast<u64>(params.pages) * params.page_size};

    if ((params.flags & MappingFlags::Sparse) != MappingFlags::None) {
        gmmu->MapSparse(params.offset, size);
    }

    allocation_map[params.offset] = {
        .mappings{},
        .size = size,
        .page_size = params.page_size,
        .sparse = (params.flags & MappingFlags::Sparse) != MappingFlags::None,
        .big_pages = params.page_size != VM::YUZU_PAGESIZE,
    };

    return NvResult::Success;
}

bool nvhost_as_gpu::FreeMappingLocked(u64 offset) noexcept {
    if (auto const it = mapping_map.find(offset); it != mapping_map.end()) {
        auto const mapping = it->second;
        const bool native = m_native_process &&
            NativeGpuProcessRegistry::Instance().Contains(m_native_process);
        if (native) {
            // Retire the GPU PTE and its rasterizer cache owner BEFORE
            // releasing the pin or recycling the dynamic GPU VA. The old
            // path called UnpinHandle first: a zero-dupe handle could lose
            // physical backing while this GPU address still resolved to it.
            try {
                if (mapping.sparse_alloc)
                    gmmu->MapSparse(offset, mapping.size, mapping.big_page);
                else
                    gmmu->Unmap(offset, mapping.size);
                if (!mapping.fixed) {
                    auto& allocator = mapping.big_page ? *vm.big_page_allocator
                                                       : *vm.small_page_allocator;
                    const u32 page_size = mapping.big_page ? vm.big_page_size
                                                             : VM::YUZU_PAGESIZE;
                    const u32 bits = mapping.big_page ? vm.big_page_size_bits
                                                       : VM::PAGE_SIZE_BITS;
                    const u64 rounded = Common::AlignUp(mapping.size, u64{page_size});
                    allocator.Free(u32(mapping.offset >> bits), u32(rounded >> bits));
                }
            } catch (const std::bad_alloc&) {
                // A partial GMMU failure is not permission to unpin live
                // backing. Keep the map ledger for deferred retirement and
                // prevent any subsequent native VA mutation.
                PoisonNativeAddressSpace();
                return false;
            }
            nvmap.UnpinHandle(mapping.handle);
            mapping_map.erase(offset);
            map_buffer_offsets.erase(offset);
            return true;
        }
        if (!mapping.fixed) {
            auto& allocator{mapping.big_page ? *vm.big_page_allocator : *vm.small_page_allocator};
            u32 page_size_bits{mapping.big_page ? vm.big_page_size_bits : VM::PAGE_SIZE_BITS};
            u32 page_size{mapping.big_page ? vm.big_page_size : VM::YUZU_PAGESIZE};
            u64 aligned_size{Common::AlignUp(mapping.size, page_size)};
            allocator.Free(u32(mapping.offset >> page_size_bits), u32(aligned_size >> page_size_bits));
        }
        nvmap.UnpinHandle(mapping.handle);
        // Sparse mappings shouldn't be fully unmapped, just returned to their sparse state
        // Only FreeSpace can unmap them fully
        if (mapping.sparse_alloc) {
            gmmu->MapSparse(offset, mapping.size, mapping.big_page);
        } else {
            gmmu->Unmap(offset, mapping.size);
        }
        mapping_map.erase(offset);
        // FreeSpace also retires the UnmapBuffer ioctl lookup. Leaving the
        // old offset in map_buffer_offsets would make a later guest unmap
        // dereference mapping_map.end() after the NVMap pin was released.
        map_buffer_offsets.erase(offset);
        return true;
    }
    return false;
}

NvResult nvhost_as_gpu::FreeSpace(IoctlFreeSpace& params) {
    LOG_DEBUG(Service_NVDRV, "called, offset={:X}, pages={:X}, page_size={:X}", params.offset, params.pages, params.page_size);
    // A native GPU free retires GMMU PTEs and may drop the last NVMap
    // physical pin. A queued GPU command could still dereference that PA.
    // Drain BEFORE taking the AS mutex: GPU command processing can query
    // these page tables while WaitForIdle() runs. NS1 stays unchanged.
    if (m_native_process &&
        NativeGpuProcessRegistry::Instance().Contains(m_native_process) &&
        system.IsPoweredOn() && !system.IsShuttingDown())
        system.GPU().WaitForIdle();
    std::scoped_lock lock(mutex);
    if (!vm.initialised) {
        return NvResult::BadValue;
    }
    if (native_sparse_regions.contains(params.offset))
        return NvResult::BadParameter;
    if (native_sparse_poisoned && m_native_process &&
        NativeGpuProcessRegistry::Instance().Contains(m_native_process))
        return NvResult::InvalidState;
    if (auto const it = allocation_map.find(params.offset); it != allocation_map.end()) {
        // FreeSpace holds the AS mutex throughout retirement; no other
        // caller may mutate this allocation vector. Avoid copying the
        // entire owner ledger on a P0 memory-pressure exit path.
        const auto& allocation = it->second;
        if (allocation.page_size != params.page_size || allocation.size != (u64(params.pages) * params.page_size))
            return NvResult::BadValue;

        // Native A:14 and GSV 216 share the sparse range/pin registry.
        // Reserve all needed host nodes before unmapping any guest PTE.
        NativeSparseMappingTable updated_sparse;
        std::set<u32> retained_handles;
        const bool native_sparse = allocation.sparse && m_native_process &&
                                   NativeGpuProcessRegistry::Instance().Contains(m_native_process);
        if (native_sparse) {
            try {
                updated_sparse = native_sparse_mappings;
                updated_sparse.Replace(NativeSparseRange{
                    static_cast<u64>(params.offset), allocation.size, 0, 0});
                retained_handles = updated_sparse.ReferencedHandles();
            } catch (const std::bad_alloc&) {
                return NvResult::InsufficientMemory;
            }
        }

        for (const auto mapping_offset : allocation.mappings)
            if (!FreeMappingLocked(mapping_offset))
                return NvResult::BadValue;

        // Always retire GPU PTEs BEFORE dropping their pinned NVMap memory.
        if (allocation.sparse) {
            if (native_sparse) {
                try {
                    gmmu->Unmap(params.offset, allocation.size);
                } catch (const std::bad_alloc&) {
                    // Retain the ledger and pins on an incomplete unmap.
                    PoisonNativeAddressSpace();
                    return NvResult::InvalidState;
                }
            } else {
                gmmu->Unmap(params.offset, allocation.size);
            }
        }
        if (native_sparse) {
            native_sparse_mappings = std::move(updated_sparse);
            for (auto pin = native_sparse_pins.begin(); pin != native_sparse_pins.end();) {
                if (retained_handles.contains(pin->first)) { ++pin; continue; }
                nvmap.UnpinHandle(pin->first);
                pin = native_sparse_pins.erase(pin);
            }
        }

        auto& allocator{params.page_size == VM::YUZU_PAGESIZE ? *vm.small_page_allocator : *vm.big_page_allocator};
        u32 page_size_bits{params.page_size == VM::YUZU_PAGESIZE ? VM::PAGE_SIZE_BITS : vm.big_page_size_bits};

        allocator.Free(u32(params.offset >> page_size_bits), u32(allocation.size >> page_size_bits));
        allocation_map.erase(params.offset);
        return NvResult::Success;
    }
    return NvResult::BadValue;
}

NvResult nvhost_as_gpu::Remap(std::span<IoctlRemapEntry> entries) {
    LOG_DEBUG(Service_NVDRV, "called, num_entries={:#x}", entries.size());

    if (m_native_process &&
        NativeGpuProcessRegistry::Instance().Contains(m_native_process)) {
        // Native NVDRV A:14 shares the same transactional pin and mapping
        // table with GSV 216. Legacy A:14 pinned on every remap but never
        // recorded enough ownership to retire the backing.
        if (entries.empty()) return NvResult::Success;
        if (entries.size() > 256) return NvResult::BadParameter;
        if (!vm.initialised) return NvResult::InvalidState;
        std::vector<NativeSparseRemapEntry> transaction;
        try {
            transaction.reserve(entries.size());
            for (const auto& entry : entries) {
                // SwitchBrew A:14 remap_op flags bit2 controls
                // cacheability. Accept it without mistaking it for a
                // page-size selector or an unknown command bit.
                if ((u32{entry.flags} & ~0x4U) || !entry.big_pages ||
                    entry.kind > std::numeric_limits<u8>::max())
                    return NvResult::BadParameter;
                NativeSparseRemapEntry item{};
                item.flags = (vm.big_page_size_bits == 16 ? 0x2000U : 0x4000U) |
                             (u32{entry.flags} & 0x4U);
                item.kind = entry.kind;
                item.fallback_kind = entry.kind;
                item.handle = entry.handle;
                item.source_pages = entry.handle ? u64{entry.handle_offset_big_pages} : u64{0};
                item.destination_pages = entry.as_offset_big_pages;
                item.pages = entry.big_pages;
                transaction.push_back(item);
            }
        } catch (const std::bad_alloc&) {
            return NvResult::InsufficientMemory;
        }
        // The guest can have already queued reads of the old big-page
        // backing. Retire those commands before changing their GMMU PTEs.
        // GSV 216 has the same wait-before-remap contract; no NS1 wait.
        if (system.IsPoweredOn() && !system.IsShuttingDown())
            system.GPU().WaitForIdle();
        return RemapNativeSparse(transaction);
    }

    if (!vm.initialised) {
        return NvResult::BadValue;
    }

    for (const auto& entry : entries) {
        GPUVAddr virtual_address{static_cast<u64>(entry.as_offset_big_pages)
                                 << vm.big_page_size_bits};
        u64 size{static_cast<u64>(entry.big_pages) << vm.big_page_size_bits};

        const auto alloc = NativeGpuAllocation::FindContaining(
            allocation_map, virtual_address, size);
        if (alloc == allocation_map.end()) {
            LOG_WARNING(Service_NVDRV, "Cannot remap into an unallocated region!");
            return NvResult::BadValue;
        }

        if (!alloc->second.sparse) {
            LOG_WARNING(Service_NVDRV, "Cannot remap a non-sparse mapping!");
            return NvResult::BadValue;
        }

        const bool use_big_pages = alloc->second.big_pages;
        if (!entry.handle) {
            gmmu->MapSparse(virtual_address, size, use_big_pages);
        } else {
            auto handle{nvmap.GetHandle(entry.handle)};
            if (!handle) {
                return NvResult::BadValue;
            }

            const DAddr base = nvmap.PinHandle(entry.handle, false);
            const u64 source = static_cast<u64>(entry.handle_offset_big_pages)
                               << vm.big_page_size_bits;
            if (!NativePinnedPhysicalSpan(base, source, size)) {
                if (base) nvmap.UnpinHandle(entry.handle);
                return NvResult::InsufficientMemory;
            }
            const DAddr device_address = base + source;

            gmmu->Map(virtual_address, device_address, size,
                      static_cast<Tegra::PTEKind>(entry.kind), use_big_pages);
        }
    }

    return NvResult::Success;
}

NvResult nvhost_as_gpu::MapBufferEx(IoctlMapBufferEx& params) {
    LOG_DEBUG(Service_NVDRV,
              "called, flags={:X}, nvmap_handle={:X}, buffer_offset={}, mapping_size={}"
              ", offset={:#x}",
              params.flags, params.handle, params.buffer_offset, params.mapping_size,
              params.offset);

    // A native PTE-kind remap must drain prior GPU work BEFORE entering the
    // address-space critical section. The GPU worker may still query these
    // page tables while WaitForIdle drains the queue.
    const bool native_remap_requested =
        m_native_process &&
        NativeGpuProcessRegistry::Instance().Contains(m_native_process) &&
        (params.flags & MappingFlags::Remap) != MappingFlags::None;
    if (native_remap_requested && system.IsPoweredOn() && !system.IsShuttingDown())
        system.GPU().WaitForIdle();

    std::scoped_lock lock(mutex);

    if (!vm.initialised) {
        return NvResult::BadValue;
    }

    // A partially committed native sparse transaction has quarantined
    // physical owners. Do not let an unrelated ioctl overwrite its PTEs.
    const bool remap_native = m_native_process &&
        NativeGpuProcessRegistry::Instance().Contains(m_native_process);
    if (remap_native && native_sparse_poisoned)
        return NvResult::InvalidState;
    if (remap_native) {
        // Switch 2 allows FIXED bit0, CACHEABLE bit2 and the legacy
        // subrange REMAP bit8 only. Unknown bits cannot be acknowledged
        // as a successful mapping with silently different PTE ownership.
        constexpr u32 valid_flags = 0x105U;
        if ((static_cast<u32>(params.flags) & ~valid_flags) != 0)
            return NvResult::NotSupported;
        // The fourth u32 in MAP_BUFFER_EX/EX2 is documented as
        // reserved/inout, not a requested page size. Preserve it.
    }

    // Remaps a subregion of an existing mapping to a different PA
    if ((params.flags & MappingFlags::Remap) != MappingFlags::None) {
        // These two ioctl fields are SIGNED; reject negative input before
        // conversion, address arithmetic or locating an existing mapping.
        if (params.offset < 0 || params.buffer_offset < 0) {
            return NvResult::BadValue;
        }
        const u64 gpu_base = static_cast<u64>(params.offset);
        const u64 buffer_offset = static_cast<u64>(params.buffer_offset);
        if (auto const it = mapping_map.find(gpu_base); it != mapping_map.end()) {
            auto const mapping = it->second;
            // A remap into an existing buffer must fit its original offset
            // and size. Reject wrapped GPU VAs before any page-table change.
            constexpr u64 gpu_limit = u64{1} << 49;
            if (!NativeGpuAllocation::ContainsSubrange(
                    mapping.size, buffer_offset, params.mapping_size) ||
                gpu_base >= gpu_limit ||
                buffer_offset >= gpu_limit - gpu_base ||
                params.mapping_size > gpu_limit - gpu_base - buffer_offset ||
                mapping.ptr > std::numeric_limits<VAddr>::max() - buffer_offset) {
                LOG_WARNING(Service_NVDRV, "Cannot remap a partially mapped GPU address space region: {:#x}", params.offset);
                return NvResult::BadValue;
            }
            const u64 gpu_address = gpu_base + buffer_offset;
            const VAddr device_address{mapping.ptr + buffer_offset};
            if (remap_native) {
                // Native remap only changes the PTE kind/view of the
                // original NvMap owner. A different handle requires the
                // explicit sparse-remap transaction; silently ignoring it
                // would acknowledge a map to the WRONG physical memory.
                if (params.handle != 0 && params.handle != mapping.handle)
                    return NvResult::NotSupported;
                const u64 page = mapping.big_page ? vm.big_page_size
                                                  : VM::YUZU_PAGESIZE;
                if ((gpu_address | buffer_offset | params.mapping_size) & (page - 1))
                    return NvResult::BadValue;
                const auto backing = nvmap.GetHandle(mapping.handle);
                const auto* session = backing
                    ? container.GetSession(backing->session_id) : nullptr;
                if (!backing || !backing->allocated || !session ||
                    session->process != m_native_process)
                    return NvResult::BadValue;
                const u32 requested = params.kind == std::numeric_limits<u32>::max()
                                          ? u32{backing->kind} : u32{params.kind};
                if (requested > std::numeric_limits<u8>::max())
                    return NvResult::NotSupported;
                const u8 candidate = static_cast<u8>(requested);
                auto native_kind = SelectNativeMemoryKind(candidate, candidate);
                if (!native_kind && candidate != backing->kind)
                    native_kind = SelectNativeMemoryKind(candidate, backing->kind);
                if (!native_kind &&
                    candidate == static_cast<u8>(Tegra::PTEKind::GENERIC_16BX2))
                    native_kind = NativeMemoryKind{
                        static_cast<u8>(Tegra::PTEKind::GENERIC_16BX2), candidate};
                if (!native_kind) return NvResult::NotSupported;
                try {
                    gmmu->Map(gpu_address, device_address, params.mapping_size,
                              static_cast<Tegra::PTEKind>(native_kind->backend),
                              mapping.big_page);
                } catch (const std::bad_alloc&) {
                    // The original NVMap owner and pin remain valid even if
                    // this PTE edit was only partially completed.
                    PoisonNativeAddressSpace();
                    return NvResult::InsufficientMemory;
                }
                params.kind = native_kind->selected;
                return NvResult::Success;
            }
            gmmu->Map(gpu_address, device_address, params.mapping_size, Tegra::PTEKind(params.kind), mapping.big_page);
            return NvResult::Success;
        } else {
            LOG_WARNING(Service_NVDRV, "Cannot remap an unmapped GPU address space region: {:#x}", params.offset);
            return NvResult::BadValue;
        }
    }

    auto handle{nvmap.GetHandle(params.handle)};
    if (!handle) {
        return NvResult::BadValue;
    }
    // An authenticated NS2 GPU AS may only bind actually allocated NVMap
    // backing from its KProcess. An unrelated FD may expose a numeric
    // handle with the same value, but must not create an alias into it.
    const bool native =
        m_native_process && NativeGpuProcessRegistry::Instance().Contains(m_native_process);
    std::optional<NativeMemoryKind> native_kind;
    if (native) {
        const auto* owner = container.GetSession(handle->session_id);
        if (!handle->allocated || !owner || owner->process != m_native_process)
            return NvResult::BadValue;

        // In the native ioctl the kind is a GA10F MMU kind, not an Eden
        // Maxwell PTEKind ordinal. For example native generic kind 6 is
        // NOT backend PTE kind 6 (Z16 MS16). The same translation already
        // governs GSV GPU 210/216 sparse mappings. -1 selects the
        // allocated NvMap handle's kind, as specified by NVGPU_AS ioctl.
        const u32 requested = params.kind == std::numeric_limits<u32>::max()
                                  ? u32{handle->kind} : u32{params.kind};
        if (requested > std::numeric_limits<u8>::max())
            return NvResult::NotSupported;
        const u8 candidate = static_cast<u8>(requested);
        native_kind = SelectNativeMemoryKind(candidate, candidate);
        if (!native_kind && candidate != handle->kind)
            native_kind = SelectNativeMemoryKind(candidate, handle->kind);
        // The legacy generic backend kind is still used by imported
        // NvMap allocations, and has an exact 1:1 backend representation.
        if (!native_kind && candidate == static_cast<u8>(Tegra::PTEKind::GENERIC_16BX2))
            native_kind = NativeMemoryKind{
                static_cast<u8>(Tegra::PTEKind::GENERIC_16BX2), candidate};
        if (!native_kind) return NvResult::NotSupported;
    }

    const u64 size{params.mapping_size ? params.mapping_size : handle->orig_size};
    if (!NativeGpuAllocation::ContainsSubrange(handle->orig_size,
                                               params.buffer_offset, size))
        return NvResult::BadValue;
    // Validate the fixed GPU allocation BEFORE pinning NvMap. Refusing
    // an invalid ioctl after PinHandle leaked its underlying GPU pin.
    auto fixed_alloc = allocation_map.end();
    if ((params.flags & MappingFlags::Fixed) != MappingFlags::None) {
        fixed_alloc = NativeGpuAllocation::FindContaining(
            allocation_map, params.offset, size);
        if (fixed_alloc == allocation_map.end()) {
            LOG_WARNING(Service_NVDRV, "Fixed GPU map outside allocation: {:#x}", params.offset);
            return NvResult::BadValue;
        }
        if (native) {
            // A fixed map without the Remap flag may not replace a live GPU
            // view. insert_or_assign would forget its NVMap pin and leave
            // queued texture/compute work pointing at re-used pages.
            const u64 address = static_cast<u64>(params.offset);
            const auto next = mapping_map.lower_bound(address);
            if ((next != mapping_map.end() && next->first < address + size) ||
                (next != mapping_map.begin() &&
                 std::prev(next)->second.size > address - std::prev(next)->first))
                return NvResult::BadValue;
        }
    }
    const DAddr pinned = nvmap.PinHandle(params.handle, false);
    if (!NativePinnedPhysicalSpan(pinned, params.buffer_offset, size)) {
        if (pinned) nvmap.UnpinHandle(params.handle);
        return NvResult::InsufficientMemory;
    }
    const DAddr device_address = pinned + params.buffer_offset;

    bool big_page{[&]() {
        if (Common::IsAligned(handle->align, vm.big_page_size)) {
            return true;
        } else if (Common::IsAligned(handle->align, VM::YUZU_PAGESIZE)) {
            return false;
        } else {
            ASSERT(false);
            return false;
        }
    }()};
    if (native) {
        if (vm.big_page_allocator &&
            vm.big_page_allocator->GetVAStart() == vm.big_page_allocator->GetVALimit())
            // A native small-page-only address-space view cannot use a big
            // PTE merely because its physical NvMap handle is aligned.
            big_page = false;
        if (big_page && !Common::IsAligned(handle->align, vm.big_page_size)) {
            nvmap.UnpinHandle(params.handle);
            return NvResult::BadValue;
        }
    }

    // Native NS2: all fallible owner and lookup allocations must complete
    // BEFORE publishing GPU PTEs. The legacy NVDRV path below stays intact.
    // Otherwise a failed map/offset-vector allocation can orphan an NVMap
    // pin or publish backing that can no longer be retired.
    if (native) {
        const bool fixed = (params.flags & MappingFlags::Fixed) != MappingFlags::None;
        // The allocated VA determines which GMMU table can resolve this
        // fixed map. Mapping a 4K physical view into a big-page-only VA
        // would succeed at ioctl level but remain invisible to the guest
        // (GetEntry selects the big table above the split).
        if (fixed && fixed_alloc->second.big_pages && !big_page) {
            nvmap.UnpinHandle(params.handle);
            return NvResult::BadValue;
        }
        const bool use_big_pages = fixed ? fixed_alloc->second.big_pages : big_page;
        const u32 page_size = use_big_pages ? vm.big_page_size : VM::YUZU_PAGESIZE;
        const u32 page_bits = use_big_pages ? vm.big_page_size_bits : VM::PAGE_SIZE_BITS;
        if (!size || size > std::numeric_limits<u64>::max() - (page_size - 1)) {
            nvmap.UnpinHandle(params.handle);
            return NvResult::BadValue;
        }
        const u64 mapped_size = Common::AlignUp(size, u64{page_size});
        const u64 source_offset = static_cast<u64>(params.buffer_offset);
        // MemoryManager::Map walks whole PTEs even if the requested size
        // ends mid-page. Validate the full physical range and alignment.
        if ((source_offset & (page_size - 1)) ||
            source_offset > handle->aligned_size ||
            mapped_size > handle->aligned_size - source_offset ||
            !NativePinnedPhysicalSpan(pinned, source_offset, mapped_size) ||
            ((pinned + source_offset) & (page_size - 1)) != 0) {
            nvmap.UnpinHandle(params.handle);
            return NvResult::BadValue;
        }

        u64 address{};
        u32 allocated_page{};
        u32 allocated_pages{};
        bool owns_va = false;
        auto* allocator = use_big_pages ? &*vm.big_page_allocator : &*vm.small_page_allocator;
        if (fixed) {
            address = static_cast<u64>(params.offset);
            const auto& allocation = fixed_alloc->second;
            if (address < fixed_alloc->first ||
                ((address | mapped_size) & (page_size - 1)) ||
                mapped_size > allocation.size - (address - fixed_alloc->first)) {
                nvmap.UnpinHandle(params.handle);
                return NvResult::BadValue;
            }
        } else {
            const u64 page_count = mapped_size >> page_bits;
            if (!page_count || page_count > std::numeric_limits<u32>::max()) {
                nvmap.UnpinHandle(params.handle);
                return NvResult::BadValue;
            }
            allocated_pages = static_cast<u32>(page_count);
            try {
                allocated_page = allocator->Allocate(allocated_pages);
            } catch (const std::bad_alloc&) {
                nvmap.UnpinHandle(params.handle);
                return NvResult::InsufficientMemory;
            }
            if (!allocated_page) {
                nvmap.UnpinHandle(params.handle);
                return NvResult::InsufficientMemory;
            }
            owns_va = true;
            address = static_cast<u64>(allocated_page) << page_bits;
        }

        bool owner_inserted = false;
        bool token_inserted = false;
        bool allocation_indexed = false;
        const auto rollback = [&] {
            if (allocation_indexed) fixed_alloc->second.mappings.pop_back();
            if (token_inserted) map_buffer_offsets.erase(static_cast<s64>(address));
            if (owner_inserted) mapping_map.erase(address);
            if (owns_va) allocator->Free(allocated_page, allocated_pages);
            nvmap.UnpinHandle(params.handle);
        };

        // The sparse batch table is a distinct physical owner from
        // MapBufferEx. Never silently override its live PTE/pin by a fixed
        // mapping which would survive the sparse table's later release.
        const auto sparse = native_sparse_mappings.Ranges().lower_bound(address);
        if ((sparse != native_sparse_mappings.Ranges().end() &&
             sparse->first < address + mapped_size) ||
            (sparse != native_sparse_mappings.Ranges().begin() &&
             std::prev(sparse)->second.size >
                 address - std::prev(sparse)->first)) {
            rollback();
            return NvResult::BadValue;
        }

        try {
            if (fixed)
                fixed_alloc->second.mappings.reserve(fixed_alloc->second.mappings.size() + 1);
            const auto [it, inserted] = mapping_map.try_emplace(
                address, Mapping(params.handle, device_address, address,
                                 mapped_size, fixed, use_big_pages,
                                 fixed && fixed_alloc->second.sparse));
            if (!inserted) {
                rollback();
                return NvResult::BadValue;
            }
            owner_inserted = true;
            const auto [token, new_token] =
                map_buffer_offsets.insert(static_cast<s64>(address));
            (void)token;
            if (!new_token) {
                rollback();
                return NvResult::BadValue;
            }
            token_inserted = true;
            if (fixed) {
                fixed_alloc->second.mappings.push_back(address);
                allocation_indexed = true;
            }
        } catch (const std::bad_alloc&) {
            rollback();
            return NvResult::InsufficientMemory;
        }

        try {
            // No unowned GMMU page can become visible: every mapping has a
            // complete ledger and pin before this transaction commits.
            gmmu->Map(address, device_address, mapped_size,
                      static_cast<Tegra::PTEKind>(native_kind->backend), use_big_pages);
        } catch (const std::bad_alloc&) {
            // A GMMU allocation may have modified some PTEs before OOM.
            // Do not unpin live backing unless the page-table rollback
            // succeeds. A retained ledger is safer than a GPU UAF.
            try {
                if (fixed && fixed_alloc->second.sparse)
                    gmmu->MapSparse(address, mapped_size, use_big_pages);
                else
                    gmmu->Unmap(address, mapped_size);
            } catch (const std::bad_alloc&) {
                PoisonNativeAddressSpace();
                return NvResult::InvalidState;
            }
            rollback();
            return NvResult::InsufficientMemory;
        }
        params.offset = address;
        params.kind = native_kind->selected; // Native selected/fallback kind, never backend ordinal.
        // The caller's reserved/inout word is not the selected page size.
        return NvResult::Success;
    }

    if ((params.flags & MappingFlags::Fixed) != MappingFlags::None) {
        const auto alloc = fixed_alloc; // Validated before NvMap pin.
        const bool use_big_pages = alloc->second.big_pages && big_page;
        gmmu->Map(params.offset, device_address, size, static_cast<Tegra::PTEKind>(params.kind), use_big_pages);

        alloc->second.mappings.push_back(params.offset);
        mapping_map.insert_or_assign(params.offset, Mapping(params.handle, device_address, params.offset, size, true, use_big_pages, alloc->second.sparse));
    } else {
        auto& allocator{big_page ? *vm.big_page_allocator : *vm.small_page_allocator};
        u32 page_size{big_page ? vm.big_page_size : VM::YUZU_PAGESIZE};
        u32 page_size_bits{big_page ? vm.big_page_size_bits : VM::PAGE_SIZE_BITS};

        params.offset = u64(allocator.Allocate(u32(Common::AlignUp(size, page_size) >> page_size_bits))) << page_size_bits;
        if (!params.offset) {
            nvmap.UnpinHandle(params.handle); // Allocation failed after NvMap pin.
            ASSERT_MSG(false, "Failed to allocate free space in the GPU AS!");
            return NvResult::InsufficientMemory;
        }
        gmmu->Map(params.offset, device_address, Common::AlignUp(size, page_size), Tegra::PTEKind(params.kind), big_page);
        mapping_map.insert_or_assign(params.offset, Mapping(params.handle, device_address, params.offset, size, false, big_page, false));
    }

    map_buffer_offsets.insert(params.offset);

    return NvResult::Success;
}

NvResult nvhost_as_gpu::UnmapBuffer(IoctlUnmapBuffer& params) {
    // Native unmap also invalidates the GMMU and unpins NvMap memory.
    // Never let the producer retire backing ahead of an already queued
    // consumer. Do NOT wait with mutex held (GPU worker may read its PTEs).
    if (m_native_process &&
        NativeGpuProcessRegistry::Instance().Contains(m_native_process) &&
        system.IsPoweredOn() && !system.IsShuttingDown())
        system.GPU().WaitForIdle();
    std::scoped_lock lock(mutex);
    if (native_sparse_poisoned && m_native_process &&
        NativeGpuProcessRegistry::Instance().Contains(m_native_process))
        return NvResult::InvalidState;
    if (auto const offset_it = map_buffer_offsets.find(params.offset); offset_it != map_buffer_offsets.end()) {
        LOG_DEBUG(Service_NVDRV, "called, offset={:#x}", params.offset);
        if (!vm.initialised) {
            return NvResult::BadValue;
        }

        auto const it = mapping_map.find(params.offset);
        // A stale unmap token is never a valid mapping. In particular a
        // FreeSpace/UnmapBuffer race must not dereference end() or free a
        // different allocation whose VA was re-used.
        if (it == mapping_map.end()) {
            map_buffer_offsets.erase(offset_it);
            return NvResult::BadValue;
        }
        auto const mapping = it->second;
        const bool native = m_native_process &&
                            NativeGpuProcessRegistry::Instance().Contains(m_native_process);
        if (native) {
            // The GPU must relinquish its PTE/cache reference before VA or
            // NVMap backing can be released. On host OOM retain the ledger
            // and its physical pin, even if the unmap partially progressed.
            try {
                if (mapping.sparse_alloc)
                    gmmu->MapSparse(params.offset, mapping.size, mapping.big_page);
                else
                    gmmu->Unmap(params.offset, mapping.size);
                if (!mapping.fixed) {
                    auto& allocator = mapping.big_page ? *vm.big_page_allocator
                                                       : *vm.small_page_allocator;
                    const u32 page_size = mapping.big_page ? vm.big_page_size
                                                             : VM::YUZU_PAGESIZE;
                    const u32 page_bits = mapping.big_page ? vm.big_page_size_bits
                                                             : VM::PAGE_SIZE_BITS;
                    const u64 rounded = Common::AlignUp(mapping.size, u64{page_size});
                    allocator.Free(u32(mapping.offset >> page_bits),
                                   u32(rounded >> page_bits));
                }
            } catch (const std::bad_alloc&) {
                PoisonNativeAddressSpace();
                return NvResult::InsufficientMemory;
            }

            if (mapping.fixed) {
                const auto owner = NativeGpuAllocation::FindContaining(
                    allocation_map, static_cast<u64>(params.offset), mapping.size);
                if (owner != allocation_map.end()) {
                    auto& entries = owner->second.mappings;
                    entries.erase(std::remove(entries.begin(), entries.end(),
                                              static_cast<u64>(params.offset)), entries.end());
                }
            }
            nvmap.UnpinHandle(mapping.handle);
            mapping_map.erase(params.offset);
            map_buffer_offsets.erase(params.offset);
            return NvResult::Success;
        }
        if (!mapping.fixed) {
            auto& allocator{mapping.big_page ? *vm.big_page_allocator : *vm.small_page_allocator};
            u32 page_size_bits{mapping.big_page ? vm.big_page_size_bits : VM::PAGE_SIZE_BITS};
            // Allocate() reserved CEIL(size/page_size) pages, not the floor.
            // On native NS2 a partial final page must be returned too, or
            // repeated GPU texture/buffer streaming leaks virtual space.
            // Keep Maxwell's original release behavior untouched.
            const bool native =
                m_native_process &&
                NativeGpuProcessRegistry::Instance().Contains(m_native_process);
            const u64 rounded_size = native
                ? Common::AlignUp(mapping.size,
                                  mapping.big_page ? vm.big_page_size : VM::YUZU_PAGESIZE)
                : mapping.size;
            allocator.Free(u32(mapping.offset >> page_size_bits),
                           u32(rounded_size >> page_size_bits));
        }

        // Sparse mappings shouldn't be fully unmapped, just returned to their sparse state
        // Only FreeSpace can unmap them fully
        if (mapping.sparse_alloc) {
            gmmu->MapSparse(params.offset, mapping.size, mapping.big_page);
        } else {
            gmmu->Unmap(params.offset, mapping.size);
        }

        // A fixed mapping may have been explicitly UnmapBuffer'd before
        // FreeSpace. Retire its allocation ledger reference now so the
        // later FreeSpace cannot try to unpin an already retired mapping.
        if (m_native_process &&
            NativeGpuProcessRegistry::Instance().Contains(m_native_process) &&
            mapping.fixed) {
            auto owner = NativeGpuAllocation::FindContaining(
                allocation_map, static_cast<u64>(params.offset), mapping.size);
            if (owner != allocation_map.end()) {
                auto& mappings = owner->second.mappings;
                mappings.erase(std::remove(mappings.begin(), mappings.end(),
                                           static_cast<u64>(params.offset)),
                               mappings.end());
            }
        }
        nvmap.UnpinHandle(mapping.handle);
        mapping_map.erase(params.offset);
        map_buffer_offsets.erase(params.offset);
    }
    return NvResult::Success;
}

NvResult nvhost_as_gpu::BindChannel(IoctlBindChannel& params) {
    LOG_DEBUG(Service_NVDRV, "called, fd={:X}", params.fd);

    // Never bind a channel to an absent or uninitialized GPU address space.
    // The old path accepted a null GMMU and could crash on an unknown FD.
    if (params.fd <= 0 || module.VerifyFD(params.fd) != NvResult::Success)
        return NvResult::BadParameter;
    // Verify the device's concrete family through its virtual base BEFORE
    // the static pointer downcast. VerifyFD alone checks existence, not type.
    // A valid foreign FD (nvmap, ctrl, TSG...) is guest-controlled and used
    // to reinterpret an unrelated vtable/object here.
    const auto base_device = module.GetDevice<nvdevice>(params.fd);
    if (!base_device || !base_device->IsGpuChannelDevice())
        return NvResult::BadParameter;
    // Keep the exact validated shared_ptr: looking up the same FD again
    // could observe a different/recycled device if another service closes it.
    auto gpu_channel_device = std::static_pointer_cast<nvhost_gpu>(base_device);
    if (!gpu_channel_device || !gpu_channel_device->channel_state)
        return NvResult::BadParameter;
    const std::scoped_lock lock{mutex, gpu_channel_device->channel_mutex};
    if (m_native_vm_retired || (m_native_generation && !NativeEpochCurrent()))
        return NvResult::InvalidState;
    if (!vm.initialised || !gmmu || !m_native_process)
        return NvResult::InvalidState;

    // NVDRV opens a distinct device for each FD. Match the GPU-channel owner
    // to the AS owner, never bind a different title's GMMU by numeric FD.
    const auto gpu_session = gpu_channel_device->sessions.find(params.fd);
    if (gpu_session == gpu_channel_device->sessions.end())
        return NvResult::BadParameter;
    const auto* session = container.GetSession(gpu_session->second);
    if (!session || session->process != m_native_process)
        return NvResult::BadParameter;

    auto& channel = *gpu_channel_device->channel_state;
    if (channel.memory_manager && channel.memory_manager != gmmu.get())
        return NvResult::InvalidState;
    // Maxwell uses the same raw GMMU binding as before, but a channel retained
    // by the GPU worker/scheduler may outlive this AS descriptor's guest FD.
    // Retain the exact AS object and exact already-open NvCore session; no
    // extra ASID, mapping, guest VA or Ampere ioctl is created here.
    auto owner = weak_from_this().lock();
    if (!owner) {
        return NvResult::InvalidState;
    }
    if (!m_channel_session_retained) {
        if (!m_nv_session ||
            !container.RetainSession(*m_nv_session, m_native_process)) {
            return NvResult::InvalidState;
        }
        m_channel_session_retained = true;
    }
    channel.address_space_owner = std::move(owner);
    channel.memory_manager = gmmu.get();
    return NvResult::Success;
}

// GetVAStart is a 32-bit PAGE INDEX; promote before shifting to 64-bit GPU VA.
// A 16-GiB region must not wrap to address zero in this u64 ioctl field.
void nvhost_as_gpu::GetVARegionsImpl(IoctlGetVaRegions& params) {
    params.buf_size = 2 * sizeof(VaRegion);

    params.regions = std::array<VaRegion, 2>{
        VaRegion{
            .offset = static_cast<u64>(vm.small_page_allocator->GetVAStart()) << VM::PAGE_SIZE_BITS,
            .page_size = VM::YUZU_PAGESIZE,
            ._pad0_{},
            .pages = vm.small_page_allocator->GetVALimit() - vm.small_page_allocator->GetVAStart(),
        },
        VaRegion{
            .offset = static_cast<u64>(vm.big_page_allocator->GetVAStart()) << vm.big_page_size_bits,
            .page_size = vm.big_page_size,
            ._pad0_{},
            .pages = vm.big_page_allocator->GetVALimit() - vm.big_page_allocator->GetVAStart(),
        },
    };
}

NvResult nvhost_as_gpu::GetVARegions1(IoctlGetVaRegions& params) {
    LOG_DEBUG(Service_NVDRV, "called, buf_addr={:X}, buf_size={:X}", params.buf_addr, params.buf_size);

    std::scoped_lock lock(mutex);

    if (!vm.initialised) {
        return NvResult::BadValue;
    }

    GetVARegionsImpl(params);

    return NvResult::Success;
}

NvResult nvhost_as_gpu::GetVARegions3(IoctlGetVaRegions& params, std::span<VaRegion> regions) {
    LOG_DEBUG(Service_NVDRV, "called, buf_addr={:X}, buf_size={:X}", params.buf_addr,
              params.buf_size);

    std::scoped_lock lock(mutex);

    if (!vm.initialised) {
        return NvResult::BadValue;
    }

    GetVARegionsImpl(params);

    const size_t num_regions = (std::min)(params.regions.size(), regions.size());
    for (size_t i = 0; i < num_regions; i++) {
        regions[i] = params.regions[i];
    }

    return NvResult::Success;
}

Kernel::KEvent* nvhost_as_gpu::QueryEvent(u32 event_id) {
    LOG_CRITICAL(Service_NVDRV, "Unknown AS GPU Event {}", event_id);
    return nullptr;
}

} // namespace Service::Nvidia::Devices
