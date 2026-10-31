// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

// SPDX-FileCopyrightText: Copyright 2018 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <cstring>
#include "common/logging.h"
#include "common/random.h"
#include "common/settings.h"
#include "core/core.h"
#include "core/file_sys/content_archive.h"
#include "core/file_sys/control_metadata.h"
#include "core/file_sys/patch_manager.h"
#include "core/file_sys/romfs_factory.h"
#include "core/file_sys/romfs.h"
#include "core/hle/kernel/k_page_table.h"
#include "core/hle/kernel/k_process.h"
#include "core/hle/service/filesystem/filesystem.h"
#include "core/loader/deconstructed_rom_directory.h"
#include "core/loader/nso.h"
#include "core/loader/ns2_nso_preflight.h"
#include "core/loader/ns2_process_handoff.h"
#include "core/loader/ns2_kprocess_code_admission.h"
#include "core/loader/ns2_main_thread_admission.h"
#include "core/loader/ns2_public_homebrew_svc.h"
#include "core/loader/ns2_svc_trace.h"
#include "core/memory.h"

#ifdef HAS_NCE
#include "core/arm/nce/patcher.h"
#endif

#ifndef HAS_NCE
namespace Core::NCE {
class Patcher {};
} // namespace Core::NCE
#endif

namespace Loader {

struct PatchCollection {
    explicit PatchCollection(bool is_application_) : is_application{is_application_} {
        module_patcher_indices.fill(-1);
        patchers.emplace_back();
    }

    std::vector<Core::NCE::Patcher>* GetPatchers() {
        if (is_application && Settings::IsNceEnabled()) {
            return &patchers;
        }
        return nullptr;
    }

    size_t GetTotalPatchSize() const {
        size_t total_size{};
#ifdef HAS_NCE
        for (auto& patcher : patchers) {
            total_size += patcher.GetSectionSize();
        }
#endif
        return total_size;
    }

    void SaveIndex(size_t module) {
        module_patcher_indices[module] = static_cast<s32>(patchers.size() - 1);
    }

    s32 GetIndex(size_t module) const {
        return module_patcher_indices[module];
    }

    s32 GetLastIndex() const {
        return static_cast<s32>(patchers.size()) - 1;
    }

    bool is_application;
    std::vector<Core::NCE::Patcher> patchers;
    std::array<s32, 13> module_patcher_indices{};
};

AppLoader_DeconstructedRomDirectory::AppLoader_DeconstructedRomDirectory(FileSys::VirtualFile file_, bool override_update_)
    : AppLoader(std::move(file_))
    , override_update(override_update_)
{
    const auto file_dir = file->GetContainingDirectory();

    // Title ID
    const auto npdm = file_dir->GetFile("main.npdm");
    if (npdm != nullptr) {
        const auto res = metadata.Load(npdm);
        if (res == ResultStatus::Success)
            title_id = metadata.GetTitleID();
    }

    // Icon
    FileSys::VirtualFile icon_file = nullptr;
    for (const auto& language : FileSys::LANGUAGE_NAMES) {
        icon_file = file_dir->GetFile("icon_" + std::string(language) + ".dat");
        if (icon_file != nullptr) {
            icon_data = icon_file->ReadAllBytes();
            break;
        }
    }

    if (icon_data.empty()) {
        // Any png, jpeg, or bmp file
        const auto& files = file_dir->GetFiles();
        const auto icon_iter =
            std::find_if(files.begin(), files.end(), [](const FileSys::VirtualFile& f) {
                return f->GetExtension() == "png" || f->GetExtension() == "jpg" ||
                       f->GetExtension() == "bmp" || f->GetExtension() == "jpeg";
            });
        if (icon_iter != files.end())
            icon_data = (*icon_iter)->ReadAllBytes();
    }

    // Metadata
    FileSys::VirtualFile nacp_file = file_dir->GetFile("control.nacp");
    if (nacp_file == nullptr) {
        const auto& files = file_dir->GetFiles();
        const auto nacp_iter =
            std::find_if(files.begin(), files.end(),
                         [](const FileSys::VirtualFile& f) { return f->GetExtension() == "nacp"; });
        if (nacp_iter != files.end())
            nacp_file = *nacp_iter;
    }

    if (nacp_file != nullptr) {
        FileSys::NACP nacp(nacp_file);
        name = nacp.GetApplicationName();
    }
}

AppLoader_DeconstructedRomDirectory::AppLoader_DeconstructedRomDirectory(
    FileSys::VirtualDir directory, bool override_update_)
    : AppLoader(directory->GetFile("main"))
    , dir(std::move(directory))
    , override_update(override_update_)
    , standalone(false)
{}

FileType AppLoader_DeconstructedRomDirectory::IdentifyType(const FileSys::VirtualFile& dir_file) {
    if (!dir_file || dir_file->GetName() != "main") {
        return FileType::Error;
    }
    const auto directory = dir_file->GetContainingDirectory();
    if (directory && FileSys::IsDirectoryExeFS(directory) &&
        AppLoader_NSO::IdentifyType(dir_file) == FileType::NSO) {
        return FileType::DeconstructedRomDirectory;
    }
    return FileType::Error;
}

AppLoader_DeconstructedRomDirectory::LoadResult AppLoader_DeconstructedRomDirectory::Load(
    Kernel::KProcess& process, Core::System& system) {
    if (is_loaded) {
        return {ResultStatus::ErrorAlreadyLoaded, {}};
    }

    if (dir == nullptr) {
        if (file == nullptr) {
            return {ResultStatus::ErrorNullFile, {}};
        }

        dir = file->GetContainingDirectory();
    }

    // Read meta to determine title ID
    FileSys::VirtualFile npdm = dir->GetFile("main.npdm");
    if (npdm == nullptr) {
        return {ResultStatus::ErrorMissingNPDM, {}};
    }

    const ResultStatus result = metadata.Load(npdm);
    if (result != ResultStatus::Success) {
        return {result, {}};
    }

    if (override_update) {
        const FileSys::PatchManager patch_manager(
            metadata.GetTitleID(), system.GetFileSystemController(), system.GetContentProvider());
        dir = patch_manager.PatchExeFS(dir);
    }

    // Reread in case PatchExeFS affected the main.npdm
    npdm = dir->GetFile("main.npdm");
    if (npdm == nullptr) {
        return {ResultStatus::ErrorMissingNPDM, {}};
    }

    const ResultStatus result2 = metadata.Reload(npdm);
    if (result2 != ResultStatus::Success) {
        return {result2, {}};
    }
    metadata.Print();

    // Enable NCE only for applications with 39-bit address space.
    const bool is_39bit =
        metadata.GetAddressSpaceType() == FileSys::ProgramAddressSpaceType::Is39Bit;
    const bool is_application = metadata.GetPoolPartition() == FileSys::PoolPartition::Application;
    Settings::SetNceEnabled(is_39bit && !metadata.UsesExtendedAcid());

    const std::array static_modules = {"rtld",    "main",    "subsdk0", "subsdk1", "subsdk2",
                                       "subsdk3", "subsdk4", "subsdk5", "subsdk6", "subsdk7",
                                       "subsdk8", "subsdk9", "sdk"};

    // N3q: the maintained NSO0 loader sizes temporary buffers from
    // untrusted segment headers. Before ANY process sizing or guest-memory
    // admission, validate all NS2 ExeFS modules against the actual file
    // length. Switch 1 and regular standalone NSO paths remain untouched.
    if (metadata.IsSwitch2()) {
        for (const auto* module : static_modules) {
            const auto module_file = dir->GetFile(module);
            if (!module_file) continue;
            std::array<u8, Eden::NS2::ExeFS::NsoHeaderBytes> header{};
            if (module_file->ReadBytes(header.data(), header.size(), 0) != header.size() ||
                !Eden::NS2::ExeFS::PreflightNso(header, module_file->GetSize()).Ready()) {
                LOG_ERROR(Loader, "NS2 ExeFS module {} rejected by NSO0 bounds preflight", module);
                return {ResultStatus::ErrorLoadingNSO, {}};
            }
        }
    }


    std::size_t code_size{};

    // Define an nce patch context for each potential module.
    PatchCollection patch_ctx{is_application};

    // Use the NSO module loader to figure out the code layout
    for (size_t i = 0; i < static_modules.size(); i++) {
        const auto& module = static_modules[i];
        const FileSys::VirtualFile module_file{dir->GetFile(module)};
        if (!module_file) {
            continue;
        }

        const bool should_pass_arguments = std::strcmp(module, "rtld") == 0;
        const auto tentative_next_load_addr = AppLoader_NSO::LoadModule(
            process, system, *module_file, code_size, should_pass_arguments, false, {},
            patch_ctx.GetPatchers(), patch_ctx.GetLastIndex());
        if (!tentative_next_load_addr) {
            return {ResultStatus::ErrorLoadingNSO, {}};
        }

        patch_ctx.SaveIndex(i);
        code_size = *tentative_next_load_addr;
    }

    // Enable direct memory mapping in case of NCE.
    const u64 fastmem_base = [&]() -> size_t {
        if (is_application && Settings::IsNceEnabled()) {
            auto& buffer = system.DeviceMemory().buffer;
            buffer.EnableDirectMappedAddress();
            return reinterpret_cast<u64>(buffer.VirtualBasePointer());
        }
        return 0;
    }();

    // N45: the real KProcess loader narrows code_size / 4 KiB to s32 pages.
    // Admit only a positive, aligned and representable total, including patches.
    const std::size_t patch_bytes = patch_ctx.GetTotalPatchSize();
    if (metadata.IsSwitch2()) {
        const auto admitted = Eden::NS2::ExeFS::AdmitKProcessCode(code_size, patch_bytes);
        if (!admitted.Ready()) {
            LOG_ERROR(Loader,
                      "NS2 ExeFS KProcess code admission denied (status={} modules={} patches={})",
                      static_cast<unsigned>(admitted.status), code_size, patch_bytes);
            return {ResultStatus::ErrorUnableToParseKernelMetadata, {}};
        }
    }
    code_size += patch_bytes;

    // TODO: this is bad form of ASLR, it sucks
    std::uintptr_t aslr_offset = ((::Settings::values.rng_seed_enabled.GetValue()
        ? ::Settings::values.rng_seed.GetValue() : Common::Random::Random64(0)) << 14) & 0xffc000;

    // Setup the process code layout
    if (process.LoadFromMetadata(system.Kernel(), metadata, code_size, fastmem_base, aslr_offset).IsError()) {
        return {ResultStatus::ErrorUnableToParseKernelMetadata, {}};
    }
    if (metadata.IsSwitch2()) {
        // The real kernel has installed the user's capabilities at this point.
        // Fail closed before loading guest code if the NPDM main thread is
        // outside the *actual* KProcess core/priority permission masks.
        const auto core_id = metadata.GetMainThreadCore();
        const auto priority = metadata.GetMainThreadPriority();
        const auto allowed_cores = process.GetCoreMask();
        const auto allowed_priorities = process.GetPriorityMask();
        if (!Eden::NS2::ExeFS::MainThreadPermitted(allowed_cores, allowed_priorities,
                                                   core_id, priority)) {
            LOG_ERROR(Loader,
                      "NS2 KProcess denied main thread core={} priority={} (core_mask={:#x}, priority_mask={:#x})",
                      core_id, priority, allowed_cores, allowed_priorities);
            return {ResultStatus::ErrorUnableToParseKernelMetadata, {}};
        }
        LOG_INFO(Loader,
                 "NS2 KProcess admitted main thread core={} priority={} (core_mask={:#x}, priority_mask={:#x})",
                 core_id, priority, allowed_cores, allowed_priorities);
        // The public synthetic homebrew has a known SVC contract. Verify
        // actual kernel-granted permissions, never synthesize them.
        if (Eden::NS2::ExeFS::IsPublicHelloFixture(metadata.GetTitleID(),
                                                   metadata.GetName())) {
            const auto missing = Eden::NS2::ExeFS::MissingPublicHelloSvcs(
                [&](u32 svc) { return process.IsPermittedSvc(svc); });
            if (missing != 0) {
                LOG_ERROR(Loader, "NS2 public homebrew denied HOS SVC permissions (mask={:#x})",
                          missing);
                return {ResultStatus::ErrorUnableToParseKernelMetadata, {}};
            }
            LOG_INFO(Loader, "NS2 public homebrew real KProcess: five required SVCs permitted");
        }
    }

    // Load NSO modules
    modules.clear();
    const VAddr base_address{GetInteger(process.GetEntryPoint())};
    VAddr next_load_addr{base_address};
    const FileSys::PatchManager pm{metadata.GetTitleID(), system.GetFileSystemController(),
                                   system.GetContentProvider()};
    for (size_t i = 0; i < static_modules.size(); i++) {
        const auto& module = static_modules[i];
        const FileSys::VirtualFile module_file{dir->GetFile(module)};
        if (!module_file) {
            continue;
        }

        const VAddr load_addr{next_load_addr};
        const bool should_pass_arguments = std::strcmp(module, "rtld") == 0;
        const auto tentative_next_load_addr = AppLoader_NSO::LoadModule(
            process, system, *module_file, load_addr, should_pass_arguments, true, pm,
            patch_ctx.GetPatchers(), patch_ctx.GetIndex(i));
        if (!tentative_next_load_addr) {
            return {ResultStatus::ErrorLoadingNSO, {}};
        }

        // N3u: prove the real KProcess contains text written by LoadModule.
        // Do not compare compressed or modified text with on-disk bytes.
        if (metadata.IsSwitch2()) {
            std::array<u8, Eden::NS2::ExeFS::NsoHeaderBytes> header{};
            if (module_file->ReadBytes(header.data(), header.size(), 0) != header.size()) {
                LOG_ERROR(Loader, "NS2 KProcess handoff: unreadable {} header", module);
                return {ResultStatus::ErrorLoadingNSO, {}};
            }
            FileSys::PatchManager::BuildID build_id{};
            std::copy_n(header.data() + 0x40, build_id.size(), build_id.begin());
            if (pm.HasNSOPatch(build_id, module) || Settings::values.dump_nso) {
                LOG_DEBUG(Loader, "NS2 KProcess handoff: skip modified {} text", module);
            } else {
                const auto verdict = Eden::NS2::ExeFS::VerifyUncompressedTextHandoff(
                    header, module_file->GetSize(), load_addr,
                    [&](u64 offset, u8* out, std::size_t size) {
                        return module_file->ReadBytes(out, size, offset);
                    },
                    [&](u64 address, u8* out, std::size_t size) {
                        return process.GetMemory().ReadBlock(address, out, size);
                    });
                using Verdict = Eden::NS2::ExeFS::TextHandoffStatus;
                if (verdict != Verdict::Verified &&
                    verdict != Verdict::SkippedCompressed &&
                    verdict != Verdict::SkippedEmptyText) {
                    LOG_ERROR(Loader, "NS2 KProcess {} text readback failed ({})",
                              module, static_cast<int>(verdict));
                    return {ResultStatus::ErrorLoadingNSO, {}};
                }
                LOG_DEBUG(Loader, "NS2 KProcess {} text handoff status {}",
                          module, static_cast<int>(verdict));
            }
        }
        next_load_addr = *tentative_next_load_addr;
        modules.insert_or_assign(load_addr, module);
        LOG_DEBUG(Loader, "loaded module {} @ {:#x}", module, load_addr);
    }

    if (metadata.IsSwitch2()) {
        // The real KProcess is initialized and all NSO modules now exist in
        // guest memory. Only THIS process is eligible for SVC instrumentation.
        Eden::NS2::SvcTrace::Register(process.GetProcessId());
        LOG_INFO(Loader, "NS2 KProcess ExeFS handoff pid={} entry={:#x} modules={}",
                 process.GetProcessId(), base_address, modules.size());
    }
    is_loaded = true;
    if (standalone) {
        title_id = metadata.GetTitleID();
        system.GetFileSystemController().RegisterProcess(
            process.GetProcessId(), {},
            std::make_shared<FileSys::RomFSFactory>(*this, system.GetContentProvider(),
                                                    system.GetFileSystemController()));
    }
    return {ResultStatus::Success,
            LoadParameters{metadata.GetMainThreadPriority(), metadata.GetMainThreadStackSize()}};
}

ResultStatus AppLoader_DeconstructedRomDirectory::ReadRomFS(FileSys::VirtualFile& out_dir) {
    if (romfs == nullptr && standalone && file) {
        const auto directory = file->GetContainingDirectory();
        if (directory) {
            for (const auto& entry : directory->GetFiles()) {
                if (entry->GetExtension() == "romfs") {
                    romfs = entry;
                    break;
                }
            }
            if (!romfs) {
                auto loose = directory->GetSubdirectory("romfs");
                if (!loose && directory->GetName() == "exefs") {
                    const auto parent = directory->GetParentDirectory();
                    if (parent) {
                        loose = parent->GetSubdirectory("romfs");
                    }
                }
                if (loose) {
                    romfs = FileSys::CreateRomFS(std::move(loose));
                }
            }
        }
    }
    if (romfs == nullptr) {
        return ResultStatus::ErrorNoRomFS;
    }

    out_dir = romfs;
    return ResultStatus::Success;
}

ResultStatus AppLoader_DeconstructedRomDirectory::ReadIcon(std::vector<u8>& out_buffer) {
    if (icon_data.empty()) {
        return ResultStatus::ErrorNoIcon;
    }

    out_buffer = icon_data;
    return ResultStatus::Success;
}

ResultStatus AppLoader_DeconstructedRomDirectory::ReadProgramId(u64& out_program_id) {
    out_program_id = title_id;
    return ResultStatus::Success;
}

ResultStatus AppLoader_DeconstructedRomDirectory::ReadTitle(std::string& out_title) {
    if (name.empty()) {
        return ResultStatus::ErrorNoControl;
    }

    out_title = name;
    return ResultStatus::Success;
}

bool AppLoader_DeconstructedRomDirectory::IsRomFSUpdatable() const {
    return false;
}

ResultStatus AppLoader_DeconstructedRomDirectory::ReadNSOModules(Modules& out_modules) {
    if (!is_loaded) {
        return ResultStatus::ErrorNotInitialized;
    }

    out_modules = this->modules;
    return ResultStatus::Success;
}

} // namespace Loader
