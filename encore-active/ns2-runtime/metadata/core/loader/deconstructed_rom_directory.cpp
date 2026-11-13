// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

// SPDX-FileCopyrightText: Copyright 2018 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <charconv>
#include <cstring>
#include <new>
#include <optional>
#include <system_error>
#include <vector>
#include "common/logging.h"
#include "common/random.h"
#include "common/settings.h"
#include "core/core.h"
#include "core/file_sys/content_archive.h"
#include "core/file_sys/control_metadata.h"
#include "core/file_sys/patch_manager.h"
#include "core/file_sys/romfs_factory.h"
#include "core/file_sys/romfs.h"
#include "core/file_sys/vfs/vfs_layered.h"
#include "core/hle/kernel/k_page_table.h"
#include "core/hle/kernel/k_process.h"
#include "core/hle/service/filesystem/filesystem.h"
#include "core/loader/deconstructed_rom_directory.h"
#ifdef EDEN_NS2_GUEST_COUNTER
#include "common/cpu_features.h"
#include "encore/guest_counter_policy.h"
#endif
#include "core/loader/nso.h"
#include "core/loader/ns2_nso_preflight.h"
#include "core/loader/ns2_process_handoff.h"
#include "core/loader/ns2_kprocess_code_admission.h"
#include "core/loader/ns2_main_thread_admission.h"
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
    // A caller can supply an orphaned or absent NSO. Metadata probing in
    // this constructor must not dereference it before Load can return the
    // existing ErrorNullFile admission result.
    if (!file) return;
    const auto file_dir = file->GetContainingDirectory();
    if (!file_dir) return;

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
    : AppLoader(directory ? directory->GetFile("main") : FileSys::VirtualFile{})
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
    // A standalone NSO may have no containing directory. Do not dereference
    // a missing ExeFS or fabricate a KProcess handoff.
    if (!dir) {
        return {ResultStatus::ErrorNullFile, {}};
    }

    // Loose ExeFS content can have most of its RomFS in a sibling
    // updates/<version>/romfs. The standalone NS2 override previously looked
    // only for base/exefs/romfs or base/romfs in ReadRomFS(), silently losing
    // update-only files (System/HeapSize, RSDB and other title assets).
    // Keep the same bounded, title-scoped overlay policy as the maintained
    // eden-core deconstructed loader, without copying guest data into RAM.
    // The NUP adapter has its own VFS and is not a standalone loose ExeFS.
    if (standalone && file) {
        FileSys::VirtualDir content_root = dir;
        if (dir->GetName() == "exefs" && dir->GetParentDirectory())
            content_root = dir->GetParentDirectory();
        FileSys::VirtualDir game_root = content_root;
        unsigned depth = 0;
        for (auto cursor = content_root; cursor && depth < 9;
             cursor = cursor->GetParentDirectory(), ++depth) {
            if (cursor->GetName() == "base" && cursor->GetParentDirectory())
                game_root = cursor->GetParentDirectory();
            if (const auto parent = cursor->GetParentDirectory();
                parent && parent->GetName() == "roms") {
                game_root = cursor;
                break;
            }
        }
        FileSys::VirtualDir base_romfs;
        depth = 0;
        for (auto cursor = content_root; cursor && depth < 9;
             cursor = cursor->GetParentDirectory(), ++depth) {
            if (auto candidate = cursor->GetSubdirectory("romfs")) {
                base_romfs = std::move(candidate);
                break;
            }
            if (cursor == game_root) break;
        }
        if (const auto updates = game_root->GetSubdirectory("updates")) {
            struct Candidate {
                FileSys::VirtualDir directory;
                std::optional<u64> version;
                unsigned depth;
            };
            std::vector<Candidate> pending{{updates, std::nullopt, 0}};
            FileSys::VirtualDir newest;
            u64 newest_version = 0;
            std::size_t visited = 0;
            bool ambiguous = false;
            while (!pending.empty() && visited++ < 128) {
                Candidate candidate = std::move(pending.back());
                pending.pop_back();
                if (candidate.version &&
                    (candidate.directory->GetSubdirectory("exefs") ||
                     candidate.directory->GetSubdirectory("romfs"))) {
                    if (!newest || *candidate.version > newest_version) {
                        newest = candidate.directory;
                        newest_version = *candidate.version;
                        ambiguous = false;
                    } else if (*candidate.version == newest_version &&
                               candidate.directory != newest) {
                        // Never choose arbitrarily between duplicate update
                        // trees with the same version and different content.
                        ambiguous = true;
                    }
                }
                if (candidate.depth >= 5) continue;
                for (const auto& nested : candidate.directory->GetSubdirectories()) {
                    const auto name = nested->GetName();
                    if (name == "exefs" || name == "romfs" || name == "dlc")
                        continue;
                    auto version = candidate.version;
                    u64 parsed = 0;
                    const auto [end, error] = std::from_chars(
                        name.data(), name.data() + name.size(), parsed);
                    if (error == std::errc{} && end == name.data() + name.size())
                        version = parsed;
                    if (pending.size() < 128)
                        pending.push_back({nested, version, candidate.depth + 1});
                }
            }
            if (!pending.empty()) {
                LOG_WARNING(Loader, "EDEN_EXTRACTED_UPDATE status=scan_incomplete");
            } else if (ambiguous) {
                LOG_WARNING(Loader, "EDEN_EXTRACTED_UPDATE status=ambiguous_version");
            } else if (newest) {
                if (auto patch_exefs = newest->GetSubdirectory("exefs")) {
                    auto layered = FileSys::LayeredVfsDirectory::MakeLayeredDirectory(
                        {patch_exefs, dir});
                    if (!layered)
                        return {ResultStatus::ErrorMissingNPDM, {}};
                    dir = std::move(layered);
                }
                if (auto patch_romfs = newest->GetSubdirectory("romfs")) {
                    std::vector<FileSys::VirtualDir> layers{std::move(patch_romfs)};
                    if (base_romfs) layers.push_back(base_romfs);
                    auto layered = FileSys::LayeredVfsDirectory::MakeLayeredDirectory(
                        std::move(layers));
                    if (!layered)
                        return {ResultStatus::ErrorNoRomFS, {}};
                    // The first layer is the update. This includes files
                    // absent from the base rather than replacing only matches.
                    const bool has_heap = layered->GetFileRelative("System/HeapSize") != nullptr;
                    const bool has_rsdb = layered->GetSubdirectory("RSDB") != nullptr;
                    romfs = FileSys::CreateRomFS(std::move(layered), nullptr);
                    if (!romfs)
                        return {ResultStatus::ErrorNoRomFS, {}};
                    LOG_INFO(Loader,
                        "EDEN_EXTRACTED_UPDATE version={} exefs={} romfs=1"
                        " base_romfs={} heap_file={} rsdb_dir={}",
                        newest_version, int(newest->GetSubdirectory("exefs") != nullptr),
                        int(base_romfs != nullptr), int(has_heap), int(has_rsdb));
                } else {
                    LOG_INFO(Loader, "EDEN_EXTRACTED_UPDATE version={} romfs=0",
                             newest_version);
                }
            }
        }
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
        // A malformed/failed update may invalidate its layered ExeFS.
        // The base path remains unchanged when no update was requested.
        if (!dir) {
            return {ResultStatus::ErrorMissingNPDM, {}};
        }
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
    // A valid NPDM with only auxiliary ExeFS modules is not a bootable
    // native NS2 process. Reject the absent executable before KProcess
    // sizing, mapping or SVC registration; legacy NS1 remains unchanged.
    if (metadata.IsSwitch2() && !dir->GetFile("main")) {
        LOG_ERROR(Loader, "NS2 ExeFS rejected: missing main NSO");
        return {ResultStatus::ErrorLoadingNSO, {}};
    }
#ifdef EDEN_NS2_GUEST_COUNTER
    // This is the real KProcess loader, unlike ProgramMetadata::Load used
    // by title scanners. NS1 keeps its original selection point; malformed
    // NS2 ExeFS must not install global mode before NSO0 admission.
    if (!metadata.IsSwitch2())
        Eden::GuestCounter::SelectProgram(false, Common::g_cpu_caps.tsc_frequency);
#endif
    metadata.Print();

    // Enable NCE only for applications with 39-bit address space.
    const bool is_39bit =
        metadata.GetAddressSpaceType() == FileSys::ProgramAddressSpaceType::Is39Bit;
    const bool is_application = metadata.GetPoolPartition() == FileSys::PoolPartition::Application;
    if (!metadata.IsSwitch2())
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

    // Commit NS2-global CPU/NCE mode only after every module in the
    // patched ExeFS passes the real bounds preflight. A malformed second
    // or later module must never change the next title's execution mode.
    // Switch 1 retains the original selection points above.
#ifdef EDEN_NS2_GUEST_COUNTER
    if (metadata.IsSwitch2())
        Eden::GuestCounter::SelectProgram(true, Common::g_cpu_caps.tsc_frequency);
#endif
    if (metadata.IsSwitch2())
        Settings::SetNceEnabled(is_39bit && !metadata.UsesExtendedAcid());

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

    // RomFSFactory invokes ReadRomFS in its constructor, so the temporary
    // loaded state is needed during registration. Never commit the NS2 trace
    // while that registration can still fail or allocate.
    is_loaded = true;
    if (standalone) {
        title_id = metadata.GetTitleID();
        try {
            const auto registered = system.GetFileSystemController().RegisterProcess(
                process.GetProcessId(), {},
                std::make_shared<FileSys::RomFSFactory>(*this, system.GetContentProvider(),
                                                        system.GetFileSystemController()));
            if (metadata.IsSwitch2() && registered.IsError()) {
                is_loaded = false;
                return {ResultStatus::ErrorLoadingNSO, {}};
            }
        } catch (const std::bad_alloc&) {
            if (!metadata.IsSwitch2()) throw; // Preserve the legacy NS1 path.
            is_loaded = false;
            return {ResultStatus::ErrorLoadingNSO, {}};
        }
    }
    if (metadata.IsSwitch2()) {
        // Handoff is now fully committed: no incomplete standalone RomFS
        // registration can install an active process/SVC trace.
        Eden::NS2::SvcTrace::Register(process.GetProcessId());
        LOG_INFO(Loader, "NS2 KProcess ExeFS handoff pid={} entry={:#x} modules={}",
                 process.GetProcessId(), base_address, modules.size());
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
