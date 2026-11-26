// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

// SPDX-FileCopyrightText: Copyright 2024 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "core/core.h"
#include "core/frontend/applets/error.h"
#include "core/reporter.h"
#include "core/core_timing.h"
#include "core/file_sys/common_funcs.h"
#include "core/file_sys/control_metadata.h"
#include "core/file_sys/patch_manager.h"
#include "core/file_sys/registered_cache.h"
#include "core/hle/service/acc/profile_manager.h"
#include "core/hle/service/am/applet_data_broker.h"
#include "core/hle/service/am/am_results.h"
#include "core/hle/service/am/applet_manager.h"
#include "core/hle/service/am/window_system.h"
#include "core/hle/service/am/frontend/applets.h"
#include "core/hle/service/am/service/library_applet_self_accessor.h"
#include "core/hle/service/am/service/storage.h"
#include "core/hle/service/cmif_serialization.h"
#include "core/hle/service/filesystem/filesystem.h"
#include "core/hle/service/glue/glue_manager.h"
#include "core/hle/service/ns/application_manager_interface.h"
#include "core/hle/service/ns/service_getter_interface.h"
#include "core/hle/service/sm/sm.h"

namespace Service::AM {

namespace {

std::optional<Result> DecodeVisibleErrorCode(ErrorCode error_code) {
    constexpr u32 ErrorCodeModuleBase = 2000;
    constexpr u32 MaxResultModule = 0x1FF;
    constexpr u32 MaxResultDescription = 0x1FFF;

    if (error_code.category < ErrorCodeModuleBase ||
        error_code.category > ErrorCodeModuleBase + MaxResultModule ||
        error_code.number > MaxResultDescription) {
        return std::nullopt;
    }

    return Result{static_cast<ErrorModule>(error_code.category - ErrorCodeModuleBase),
                  error_code.number};
}

AppletIdentityInfo GetCallerIdentity(Applet& applet) {
    if (const auto caller_applet = applet.caller_applet.lock(); caller_applet) {
        // TODO: is this actually the application ID?
        return {
            .applet_id = caller_applet->applet_id,
            .application_id = caller_applet->program_id,
        };
    } else {
        return {
            .applet_id = AppletId::QLaunch,
            .application_id = 0x0100000000001000ull,
        };
    }
}

std::shared_ptr<Applet> GetMainApplet(Core::System& system) {
    auto* const window_system = system.GetAppletManager().GetWindowSystem();
    if (window_system == nullptr) {
        return {};
    }
    return window_system->GetMainApplet();
}

} // namespace

ILibraryAppletSelfAccessor::ILibraryAppletSelfAccessor(Core::System& system_,
                                                       std::shared_ptr<Applet> applet)
    : ServiceFramework{system_, "ILibraryAppletSelfAccessor"}, m_applet{std::move(applet)},
      m_broker{m_applet->caller_applet_broker} {
    // clang-format off
    static const FunctionInfo functions[] = {
        {0, D<&ILibraryAppletSelfAccessor::PopInData>, "PopInData"},
        {1, D<&ILibraryAppletSelfAccessor::PushOutData>, "PushOutData"},
        {2, D<&ILibraryAppletSelfAccessor::PopInteractiveInData>, "PopInteractiveInData"},
        {3, D<&ILibraryAppletSelfAccessor::PushInteractiveOutData>, "PushInteractiveOutData"},
        {5, D<&ILibraryAppletSelfAccessor::GetPopInDataEvent>, "GetPopInDataEvent"},
        {6, D<&ILibraryAppletSelfAccessor::GetPopInteractiveInDataEvent>, "GetPopInteractiveInDataEvent"},
        {10, D<&ILibraryAppletSelfAccessor::ExitProcessAndReturn>, "ExitProcessAndReturn"},
        {11, D<&ILibraryAppletSelfAccessor::GetLibraryAppletInfo>, "GetLibraryAppletInfo"},
        {12, D<&ILibraryAppletSelfAccessor::GetMainAppletIdentityInfo>, "GetMainAppletIdentityInfo"},
        {13, D<&ILibraryAppletSelfAccessor::CanUseApplicationCore>, "CanUseApplicationCore"},
        {14, D<&ILibraryAppletSelfAccessor::GetCallerAppletIdentityInfo>, "GetCallerAppletIdentityInfo"},
        {15, D<&ILibraryAppletSelfAccessor::GetMainAppletApplicationControlProperty>, "GetMainAppletApplicationControlProperty"},
        {16, D<&ILibraryAppletSelfAccessor::GetMainAppletStorageId>, "GetMainAppletStorageId"},
        {17, D<&ILibraryAppletSelfAccessor::GetCallerAppletIdentityInfoStack>, "GetCallerAppletIdentityInfoStack"},
        {18, D<&ILibraryAppletSelfAccessor::GetNextReturnDestinationAppletIdentityInfo>,
         "GetNextReturnDestinationAppletIdentityInfo"},
        {19, D<&ILibraryAppletSelfAccessor::GetDesirableKeyboardLayout>, "GetDesirableKeyboardLayout"},
        {20, D<&ILibraryAppletSelfAccessor::PopExtraStorage>, "PopExtraStorage"},
        {25, D<&ILibraryAppletSelfAccessor::GetPopExtraStorageEvent>, "GetPopExtraStorageEvent"},
        {30, D<&ILibraryAppletSelfAccessor::UnpopInData>, "UnpopInData"},
        {31, D<&ILibraryAppletSelfAccessor::UnpopExtraStorage>, "UnpopExtraStorage"},
        {40, D<&ILibraryAppletSelfAccessor::GetIndirectLayerProducerHandle>, "GetIndirectLayerProducerHandle"},
        {50, D<&ILibraryAppletSelfAccessor::ReportVisibleError>, "ReportVisibleError"},
        {51, D<&ILibraryAppletSelfAccessor::ReportVisibleErrorWithErrorContext>, "ReportVisibleErrorWithErrorContext"},
        {60, D<&ILibraryAppletSelfAccessor::GetMainAppletApplicationDesiredLanguage>, "GetMainAppletApplicationDesiredLanguage"},
        {70, D<&ILibraryAppletSelfAccessor::GetCurrentApplicationId>, "GetCurrentApplicationId"},
        {80, D<&ILibraryAppletSelfAccessor::RequestExitToSelf>, "RequestExitToSelf"},
        {90, D<&ILibraryAppletSelfAccessor::CreateApplicationAndPushAndRequestToLaunch>,
         "CreateApplicationAndPushAndRequestToLaunch"},
        {100, D<&ILibraryAppletSelfAccessor::RejectUnavailableGameMovieTrimmer>,
         "CreateGameMovieTrimmer"},
        {101, D<&ILibraryAppletSelfAccessor::ReserveResourceForMovieOperation>, "ReserveResourceForMovieOperation"},
        {102, D<&ILibraryAppletSelfAccessor::UnreserveResourceForMovieOperation>, "UnreserveResourceForMovieOperation"},
        {110, D<&ILibraryAppletSelfAccessor::GetMainAppletAvailableUsers>, "GetMainAppletAvailableUsers"},
        {120, D<&ILibraryAppletSelfAccessor::RejectDebugOnlyCommand>, "GetLaunchStorageInfoForDebug"},
        {130, D<&ILibraryAppletSelfAccessor::GetGpuErrorDetectedSystemEvent>, "GetGpuErrorDetectedSystemEvent"},
        {140, D<&ILibraryAppletSelfAccessor::SetApplicationMemoryReservation>,
         "SetApplicationMemoryReservation"},
        {150, D<&ILibraryAppletSelfAccessor::ShouldSetGpuTimeSliceManually>, "ShouldSetGpuTimeSliceManually"},
        {160, D<&ILibraryAppletSelfAccessor::GetLibraryAppletInfoEx>, "GetLibraryAppletInfoEx"},
    };
    // clang-format on
    RegisterHandlers(functions);
}

ILibraryAppletSelfAccessor::~ILibraryAppletSelfAccessor() = default;

Result ILibraryAppletSelfAccessor::RejectUnavailableGameMovieTrimmer(
    u64 transfer_memory_size, InCopyHandle<Kernel::KTransferMemory> transfer_memory_handle) {
    LOG_WARNING(Service_AM,
                "GameMovieTrimmer unavailable: GRC OpenGameMovieTrimmer backend is not implemented, size={:#x}",
                transfer_memory_size);
    R_RETURN(ResultUnknown);
}

Result ILibraryAppletSelfAccessor::RejectDebugOnlyCommand() {
    LOG_WARNING(Service_AM, "Rejected library-applet debug command on release runtime");
    R_RETURN(ResultUnknown);
}

Result ILibraryAppletSelfAccessor::PopInData(Out<SharedPointer<IStorage>> out_storage) {
    LOG_INFO(Service_AM, "called");
    R_RETURN(m_broker->GetInData().Pop(system.Kernel(), out_storage));
}

Result ILibraryAppletSelfAccessor::PushOutData(SharedPointer<IStorage> storage) {
    LOG_INFO(Service_AM, "called");
    m_broker->GetOutData().Push(system.Kernel(), storage);
    R_SUCCEED();
}

Result ILibraryAppletSelfAccessor::PopInteractiveInData(Out<SharedPointer<IStorage>> out_storage) {
    LOG_INFO(Service_AM, "called");
    R_RETURN(m_broker->GetInteractiveInData().Pop(system.Kernel(), out_storage));
}

Result ILibraryAppletSelfAccessor::PushInteractiveOutData(SharedPointer<IStorage> storage) {
    LOG_INFO(Service_AM, "called");
    m_broker->GetInteractiveOutData().Push(system.Kernel(), storage);
    R_SUCCEED();
}

Result ILibraryAppletSelfAccessor::GetPopInDataEvent(
    OutCopyHandle<Kernel::KReadableEvent> out_event) {
    LOG_INFO(Service_AM, "called");
    *out_event = m_broker->GetInData().GetEvent();
    R_SUCCEED();
}

Result ILibraryAppletSelfAccessor::GetPopInteractiveInDataEvent(
    OutCopyHandle<Kernel::KReadableEvent> out_event) {
    LOG_INFO(Service_AM, "called");
    *out_event = m_broker->GetInteractiveInData().GetEvent();
    R_SUCCEED();
}

Result ILibraryAppletSelfAccessor::PopExtraStorage(Out<SharedPointer<IStorage>> out_storage) {
    LOG_INFO(Service_AM, "called");
    R_RETURN(m_broker->GetExtraStorage().Pop(system.Kernel(), out_storage));
}

Result ILibraryAppletSelfAccessor::GetPopExtraStorageEvent(
    OutCopyHandle<Kernel::KReadableEvent> out_event) {
    LOG_INFO(Service_AM, "called");
    *out_event = m_broker->GetExtraStorage().GetEvent();
    R_SUCCEED();
}

Result ILibraryAppletSelfAccessor::UnpopExtraStorage(SharedPointer<IStorage> storage) {
    LOG_INFO(Service_AM, "called");
    m_broker->GetExtraStorage().Unpop(system.Kernel(), storage);
    R_SUCCEED();
}

Result ILibraryAppletSelfAccessor::GetLibraryAppletInfo(
    Out<LibraryAppletInfo> out_library_applet_info) {
    LOG_INFO(Service_AM, "called");
    *out_library_applet_info = {
        .applet_id = m_applet->applet_id,
        .library_applet_mode = m_applet->library_applet_mode,
    };
    R_SUCCEED();
}

Result ILibraryAppletSelfAccessor::GetMainAppletIdentityInfo(
    Out<AppletIdentityInfo> out_identity_info) {
    LOG_DEBUG(Service_AM, "called");
    const auto main_applet = GetMainApplet(system);
    R_UNLESS(main_applet != nullptr, AM::ResultApplicationRecordNotFound);

    *out_identity_info = {
        .applet_id = main_applet->applet_id,
        .application_id = main_applet->program_id,
    };
    R_SUCCEED();
}

Result ILibraryAppletSelfAccessor::CanUseApplicationCore(Out<bool> out_can_use_application_core) {
    // TODO: This appears to read the NPDM from state and check the core mask of the applet.
    LOG_WARNING(Service_AM, "(STUBBED) called");
    *out_can_use_application_core = false;
    R_SUCCEED();
}

Result ILibraryAppletSelfAccessor::GetMainAppletApplicationControlProperty(
    OutLargeData<std::array<u8, 0x4000>, BufferAttr_HipcMapAlias> out_nacp) {
    LOG_DEBUG(Service_AM, "called");

    const auto main_applet = GetMainApplet(system);
    R_UNLESS(main_applet != nullptr, AM::ResultApplicationRecordNotFound);

    std::vector<u8> nacp;
    const auto result = system.GetARPManager().GetControlProperty(&nacp, main_applet->program_id);

    if (R_SUCCEEDED(result)) {
        std::memcpy(out_nacp->data(), nacp.data(), (std::min)(nacp.size(), out_nacp->size()));
    }

    R_RETURN(result);
}

Result ILibraryAppletSelfAccessor::GetMainAppletStorageId(Out<FileSys::StorageId> out_storage_id) {
    LOG_DEBUG(Service_AM, "called");
    const auto main_applet = GetMainApplet(system);
    R_UNLESS(main_applet != nullptr, AM::ResultApplicationRecordNotFound);

    const auto program_id = main_applet->program_id;
    auto& fsc = system.GetFileSystemController();
    const auto has_program = [program_id](const auto* cache) {
        return cache != nullptr &&
               cache->HasEntry(program_id, FileSys::ContentRecordType::Program);
    };

    if (has_program(fsc.GetGameCardContents())) {
        *out_storage_id = FileSys::StorageId::GameCard;
    } else if (has_program(fsc.GetSDMCContents())) {
        *out_storage_id = FileSys::StorageId::SdCard;
    } else if (has_program(fsc.GetUserNANDContents())) {
        *out_storage_id = FileSys::StorageId::NandUser;
    } else if (has_program(fsc.GetSystemNANDContents())) {
        *out_storage_id = FileSys::StorageId::NandSystem;
    } else {
        R_THROW(AM::ResultApplicationRecordNotFound);
    }

    R_SUCCEED();
}

Result ILibraryAppletSelfAccessor::ExitProcessAndReturn() {
    LOG_INFO(Service_AM, "called");

    if (const auto caller_applet = m_applet->caller_applet.lock(); caller_applet) {
        m_applet->process->Terminate();
    } else {
        system.GetUserChannel() = m_applet->user_channel_launch_parameter;
        system.ExecuteProgram(0);
    }

    R_SUCCEED();
}

Result ILibraryAppletSelfAccessor::GetCallerAppletIdentityInfo(
    Out<AppletIdentityInfo> out_identity_info) {
    LOG_INFO(Service_AM, "called");
    *out_identity_info = GetCallerIdentity(*m_applet);
    R_SUCCEED();
}

Result ILibraryAppletSelfAccessor::GetCallerAppletIdentityInfoStack(
    Out<s32> out_count, OutArray<AppletIdentityInfo, BufferAttr_HipcMapAlias> out_identity_info) {
    LOG_INFO(Service_AM, "called");

    std::shared_ptr<Applet> applet = m_applet;
    *out_count = 0;

    do {
        if (*out_count >= static_cast<s32>(out_identity_info.size())) {
            break;
        }
        out_identity_info[(*out_count)++] = GetCallerIdentity(*applet);
    } while ((applet = applet->caller_applet.lock()));

    R_SUCCEED();
}

Result ILibraryAppletSelfAccessor::GetNextReturnDestinationAppletIdentityInfo(
    Out<AppletIdentityInfo> out_identity_info) {
    LOG_DEBUG(Service_AM, "called");
    *out_identity_info = GetCallerIdentity(*m_applet);
    R_SUCCEED();
}

Result ILibraryAppletSelfAccessor::GetIndirectLayerProducerHandle(Out<u64> out_handle) {
    LOG_WARNING(Service_AM,
                "Indirect layer producer requested without a VI indirect-layer backend");
    *out_handle = 0;
    R_THROW(ResultUnknown);
}

Result ILibraryAppletSelfAccessor::GetDesirableKeyboardLayout(Out<u32> out_desirable_layout) {
    LOG_DEBUG(Service_AM, "called");

    const auto main_applet = GetMainApplet(system);
    R_UNLESS(main_applet != nullptr, AM::ResultApplicationRecordNotFound);

    std::scoped_lock lk{main_applet->lock};
    *out_desirable_layout = main_applet->desirable_keyboard_layout;
    R_SUCCEED();
}

Result ILibraryAppletSelfAccessor::ReportVisibleError(ErrorCode error_code) {
    const auto result = DecodeVisibleErrorCode(error_code);
    R_UNLESS(result.has_value(), ResultUnknown);

    const auto& frontend = system.GetFrontendAppletHolder().GetFrontendAppletSet().error;
    R_UNLESS(frontend != nullptr, ResultUnknown);

    LOG_WARNING(Service_AM, "displaying visible error {}-{} (raw={:08X})",
                error_code.category, error_code.number, result->raw);
    system.GetReporter().SaveErrorReport(system.GetApplicationProcessProgramID(), *result);
    frontend->ShowError(*result, [] {});
    R_SUCCEED();
}

Result ILibraryAppletSelfAccessor::ReportVisibleErrorWithErrorContext(
    ErrorCode error_code, InLargeData<ErrorContext, BufferAttr_HipcMapAlias> error_context) {
    const auto result = DecodeVisibleErrorCode(error_code);
    R_UNLESS(result.has_value(), ResultUnknown);

    const auto& frontend = system.GetFrontendAppletHolder().GetFrontendAppletSet().error;
    R_UNLESS(frontend != nullptr, ResultUnknown);

    LOG_WARNING(Service_AM,
                "displaying visible error {}-{} with ErrorContext type={} context_result={:08X}",
                error_code.category, error_code.number, error_context->type,
                error_context->result.raw);
    system.GetReporter().SaveErrorReport(system.GetApplicationProcessProgramID(), *result);
    frontend->ShowError(*result, [] {});
    R_SUCCEED();
}

Result ILibraryAppletSelfAccessor::UnpopInData(SharedPointer<IStorage> storage) {
    LOG_INFO(Service_AM, "called");
    m_broker->GetInData().Unpop(system.Kernel(), storage);
    R_SUCCEED();
}

Result ILibraryAppletSelfAccessor::GetMainAppletApplicationDesiredLanguage(
    Out<u64> out_desired_language) {
    // FIXME: this is copied from IApplicationFunctions::GetDesiredLanguage
    // FIXME: all of this stuff belongs to ns
    auto identity = GetCallerIdentity(*m_applet);

    // TODO(bunnei): This should be configurable
    LOG_DEBUG(Service_AM, "called");

    // Get supported languages from NACP, if possible
    // Default to 0 (all languages supported)
    u32 supported_languages = 0;

    const auto res = FileSys::PatchManager::GetMetadataFromBaseOrUpdate(system, identity.application_id);

    if (res.first != nullptr) {
        supported_languages = res.first->GetSupportedLanguages();
    }

    // Call IApplicationManagerInterface implementation.
    auto& service_manager = system.ServiceManager();
    auto ns_am2 = service_manager.GetService<NS::IServiceGetterInterface>("ns:am2");

    std::shared_ptr<NS::IApplicationManagerInterface> app_man;
    R_TRY(ns_am2->GetApplicationManagerInterface(&app_man));

    // Get desired application language
    NS::ApplicationLanguage desired_language{};
    R_TRY(app_man->GetApplicationDesiredLanguage(&desired_language, supported_languages));

    // Convert to settings language code.
    u64 language_code{};
    R_TRY(app_man->ConvertApplicationLanguageToLanguageCode(&language_code, desired_language));

    LOG_DEBUG(Service_AM, "got desired_language={:016X}", language_code);

    *out_desired_language = language_code;
    R_SUCCEED();
}

Result ILibraryAppletSelfAccessor::GetCurrentApplicationId(Out<u64> out_application_id) {
    LOG_DEBUG(Service_AM, "called");
    const auto main_applet = GetMainApplet(system);
    R_UNLESS(main_applet != nullptr, AM::ResultApplicationRecordNotFound);

    *out_application_id = main_applet->program_id;
    R_SUCCEED();
}

Result ILibraryAppletSelfAccessor::RequestExitToSelf() {
    LOG_DEBUG(Service_AM, "called");

    {
        std::scoped_lock lk{m_applet->lock};
        m_applet->lifecycle_manager.RequestExit(system.Kernel());
    }
    if (m_applet->frontend) {
        m_applet->frontend->RequestExit();
    }
    R_SUCCEED();
}

Result ILibraryAppletSelfAccessor::CreateApplicationAndPushAndRequestToLaunch(
    u64 application_id, SharedPointer<IStorage> storage) {
    R_UNLESS(storage != nullptr, ResultUnknown);

    const auto main_applet = GetMainApplet(system);
    R_UNLESS(main_applet != nullptr, AM::ResultApplicationRecordNotFound);

    u64 current_program_id{};
    {
        std::scoped_lock lk{main_applet->lock};
        current_program_id = main_applet->program_id;
    }

    const u64 base_application_id = FileSys::GetBaseTitleID(current_program_id);
    R_UNLESS(application_id == 0 ||
                 FileSys::GetBaseTitleID(application_id) == base_application_id,
             ResultUnknown);

    const std::size_t program_index =
        application_id == 0 ? 0 : static_cast<std::size_t>(application_id - base_application_id);

    auto next_user_channel = m_applet->user_channel_launch_parameter;
    next_user_channel.push_back(storage->GetData());
    system.GetUserChannel() = std::move(next_user_channel);

    LOG_INFO(Service_AM,
             "requesting application launch, application_id={:016X}, base_id={:016X}, "
             "program_index={}, user_channel_entries={}",
             application_id, base_application_id, program_index, system.GetUserChannel().size());

    system.ExecuteProgram(program_index);
    R_SUCCEED();
}

Result ILibraryAppletSelfAccessor::ReserveResourceForMovieOperation() {
    LOG_WARNING(Service_AM,
                "Movie resource reservation unavailable: GameMovieTrimmer backend is not implemented");
    R_RETURN(ResultUnknown);
}

Result ILibraryAppletSelfAccessor::UnreserveResourceForMovieOperation() {
    LOG_WARNING(Service_AM,
                "Movie resource release unavailable: GameMovieTrimmer backend is not implemented");
    R_RETURN(ResultUnknown);
}

Result ILibraryAppletSelfAccessor::GetMainAppletAvailableUsers(
    Out<bool> out_can_select_any_user, Out<s32> out_users_count,
    OutArray<Common::UUID, BufferAttr_HipcMapAlias> out_users) {
    const Service::Account::ProfileManager manager{};

    *out_can_select_any_user = false;
    *out_users_count = -1;

    LOG_INFO(Service_AM, "called");

    if (manager.GetUserCount() > 0) {
        *out_can_select_any_user = true;
        *out_users_count = static_cast<s32>(manager.GetUserCount());

        const auto users = manager.GetAllUsers();
        for (size_t i = 0; i < users.size() && i < out_users.size(); i++) {
            out_users[i] = users[i];
        }
    }

    R_SUCCEED();
}

Result ILibraryAppletSelfAccessor::GetGpuErrorDetectedSystemEvent(
    OutCopyHandle<Kernel::KReadableEvent> out_event) {
    LOG_DEBUG(Service_AM, "called");
    *out_event = m_applet->gpu_error_detected_event.GetHandle();
    R_SUCCEED();
}

Result ILibraryAppletSelfAccessor::SetApplicationMemoryReservation(u64 reservation) {
    constexpr u64 ReservationAlignment = 0x1000;
    R_UNLESS((reservation & (ReservationAlignment - 1)) == 0, ResultUnknown);

    const auto main_applet = GetMainApplet(system);
    R_UNLESS(main_applet != nullptr, AM::ResultApplicationRecordNotFound);
    R_UNLESS(main_applet->process != nullptr && !main_applet->process->IsTerminated(),
             ResultUnknown);

    {
        std::scoped_lock lk{main_applet->lock};
        main_applet->application_memory_reservation = reservation;
    }

    LOG_INFO(Service_AM,
             "recorded application memory reservation={:#x}; host allocator enforcement unavailable",
             reservation);
    R_SUCCEED();
}

Result ILibraryAppletSelfAccessor::ShouldSetGpuTimeSliceManually(
    Out<bool> out_should_set_gpu_time_slice_manually) {
    LOG_INFO(Service_AM, "(STUBBED) called");
    *out_should_set_gpu_time_slice_manually = false;
    R_SUCCEED();
}

Result ILibraryAppletSelfAccessor::GetLibraryAppletInfoEx(
    Out<LibraryAppletInfo> out_library_applet_info) {
    LOG_INFO(Service_AM, "called");
    *out_library_applet_info = {
        .applet_id = m_applet->applet_id,
        .library_applet_mode = m_applet->library_applet_mode,
    };
    R_SUCCEED();
}

} // namespace Service::AM
