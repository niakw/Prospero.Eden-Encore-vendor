// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

// SPDX-FileCopyrightText: Copyright 2024 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <openssl/evp.h>

#include "common/hex_util.h"
#include "common/settings.h"
#include "common/uuid.h"
#include "core/core.h"
#include "core/file_sys/common_funcs.h"
#include "core/file_sys/control_metadata.h"
#include "core/file_sys/patch_manager.h"
#include "core/file_sys/registered_cache.h"
#include "core/file_sys/savedata_factory.h"
#include "core/hle/kernel/k_transfer_memory.h"
#include "core/hle/result.h"
#include "core/launch_timestamp_cache.h"
#include "frontend_common/play_time_manager.h"
#include "core/hle/service/am/am_results.h"
#include "core/hle/service/am/applet.h"
#include "core/hle/service/am/applet_manager.h"
#include "core/hle/service/am/service/application_functions.h"
#include "core/hle/service/am/service/storage.h"
#include "core/hle/service/am/window_system.h"
#include "core/hle/service/cmif_serialization.h"
#include "core/hle/service/filesystem/filesystem.h"
#include "core/hle/service/filesystem/save_data_controller.h"
#include "core/hle/service/glue/glue_manager.h"
#include "core/hle/service/ns/application_manager_interface.h"
#include "core/hle/service/ns/service_getter_interface.h"
#include "core/hle/service/sm/sm.h"

namespace Service::AM {

namespace {

constexpr Result ResultCacheStorageAlreadyExists{ErrorModule::FS, 3003};
constexpr Result ResultCacheStorageSizeTooLarge{ErrorModule::FS, 6069};
constexpr Result ResultCacheStorageIndexTooLarge{ErrorModule::FS, 6070};
constexpr Result ResultInvalidCopyrightParameters{ErrorModule::AM, 506};

std::string GetCacheStorageDirectoryName(u64 program_id, u16 index) {
    if (index == 0) {
        return fmt::format("{:016X}", program_id);
    }
    return fmt::format("{:016X}_{:04X}", program_id, index);
}

std::string GetCacheStorageRelativePath(u64 program_id, u16 index) {
    return fmt::format("save/cache/{}", GetCacheStorageDirectoryName(program_id, index));
}

FileSys::PatchManager::Metadata GetApplicationMetadata(Core::System& system, u64 program_id) {
    auto metadata = FileSys::PatchManager::GetMetadataFromBaseOrUpdate(system, program_id);
    if (metadata.first != nullptr) {
        return metadata;
    }

    const auto application_id = FileSys::GetBaseTitleID(program_id);
    if (application_id == program_id) {
        return metadata;
    }

    LOG_DEBUG(Service_AM,
              "no metadata for program_id={:016X}, falling back to application_id={:016X}",
              program_id, application_id);
    return FileSys::PatchManager::GetMetadataFromBaseOrUpdate(system, application_id);
}

} // Anonymous namespace

IApplicationFunctions::IApplicationFunctions(Core::System& system_, std::shared_ptr<Applet> applet)
    : ServiceFramework{system_, "IApplicationFunctions"}, m_applet{std::move(applet)},
      m_play_time_manager{std::make_unique<PlayTime::PlayTimeManager>()} {
    // clang-format off
    static const FunctionInfo functions[] = {
        {1, D<&IApplicationFunctions::PopLaunchParameter>, "PopLaunchParameter"},
        {10, D<&IApplicationFunctions::CreateApplicationAndPushAndRequestToStart>, "CreateApplicationAndPushAndRequestToStart"},
        {11, D<&IApplicationFunctions::CreateApplicationAndPushAndRequestToStartForQuest>, "CreateApplicationAndPushAndRequestToStartForQuest"},
        {12, D<&IApplicationFunctions::CreateApplicationAndRequestToStart>, "CreateApplicationAndRequestToStart"},
        {13, D<&IApplicationFunctions::CreateApplicationAndRequestToStartForQuest>, "CreateApplicationAndRequestToStartForQuest"},
        {14, D<&IApplicationFunctions::CreateApplicationWithAttributeAndPushAndRequestToStartForQuest>, "CreateApplicationWithAttributeAndPushAndRequestToStartForQuest"},
        {15, D<&IApplicationFunctions::CreateApplicationWithAttributeAndRequestToStartForQuest>, "CreateApplicationWithAttributeAndRequestToStartForQuest"},
        {20, D<&IApplicationFunctions::EnsureSaveData>, "EnsureSaveData"},
        {21, D<&IApplicationFunctions::GetDesiredLanguage>, "GetDesiredLanguage"},
        {22, D<&IApplicationFunctions::SetTerminateResult>, "SetTerminateResult"},
        {23, D<&IApplicationFunctions::GetDisplayVersion>, "GetDisplayVersion"},
        {24, D<&IApplicationFunctions::RejectDebugOnlyCommand>, "GetLaunchStorageInfoForDebug"},
        {25, D<&IApplicationFunctions::ExtendSaveData>, "ExtendSaveData"},
        {26, D<&IApplicationFunctions::GetSaveDataSize>, "GetSaveDataSize"},
        {27, D<&IApplicationFunctions::CreateCacheStorage>, "CreateCacheStorage"},
        {28, D<&IApplicationFunctions::GetSaveDataSizeMax>, "GetSaveDataSizeMax"},
        {29, D<&IApplicationFunctions::GetCacheStorageMax>, "GetCacheStorageMax"},
        {30, D<&IApplicationFunctions::BeginBlockingHomeButtonShortAndLongPressed>, "BeginBlockingHomeButtonShortAndLongPressed"},
        {31, D<&IApplicationFunctions::EndBlockingHomeButtonShortAndLongPressed>, "EndBlockingHomeButtonShortAndLongPressed"},
        {32, D<&IApplicationFunctions::BeginBlockingHomeButton>, "BeginBlockingHomeButton"},
        {33, D<&IApplicationFunctions::EndBlockingHomeButton>, "EndBlockingHomeButton"},
        {34, nullptr, "SelectApplicationLicense"},
        {35, D<&IApplicationFunctions::GetDeviceSaveDataSizeMax>, "GetDeviceSaveDataSizeMax"},
        {36, nullptr, "GetLimitedApplicationLicense"},
        {37, nullptr, "GetLimitedApplicationLicenseUpgradableEvent"},
        {40, D<&IApplicationFunctions::NotifyRunning>, "NotifyRunning"},
        {50, D<&IApplicationFunctions::GetPseudoDeviceId>, "GetPseudoDeviceId"},
        {60, D<&IApplicationFunctions::SetMediaPlaybackStateForApplication>, "SetMediaPlaybackStateForApplication"},
        {65, D<&IApplicationFunctions::IsGamePlayRecordingSupported>, "IsGamePlayRecordingSupported"},
        {66, D<&IApplicationFunctions::InitializeGamePlayRecording>, "InitializeGamePlayRecording"},
        {67, D<&IApplicationFunctions::SetGamePlayRecordingState>, "SetGamePlayRecordingState"},
        {68, D<&IApplicationFunctions::RejectDebugOnlyCommand>, "RequestFlushGamePlayingMovieForDebug"},
        {70, D<&IApplicationFunctions::RequestToShutdown>, "RequestToShutdown"},
        {71, D<&IApplicationFunctions::RequestToReboot>, "RequestToReboot"},
        {72, D<&IApplicationFunctions::RequestToSleep>, "RequestToSleep"},
        {80, D<&IApplicationFunctions::ExitAndRequestToShowThanksMessage>, "ExitAndRequestToShowThanksMessage"},
        {90, D<&IApplicationFunctions::EnableApplicationCrashReport>, "EnableApplicationCrashReport"},
        {100, D<&IApplicationFunctions::InitializeApplicationCopyrightFrameBuffer>, "InitializeApplicationCopyrightFrameBuffer"},
        {101, D<&IApplicationFunctions::SetApplicationCopyrightImage>, "SetApplicationCopyrightImage"},
        {102, D<&IApplicationFunctions::SetApplicationCopyrightVisibility>, "SetApplicationCopyrightVisibility"},
        {110, D<&IApplicationFunctions::QueryApplicationPlayStatistics>, "QueryApplicationPlayStatistics"},
        {111, D<&IApplicationFunctions::QueryApplicationPlayStatisticsByUid>, "QueryApplicationPlayStatisticsByUid"},
        {120, D<&IApplicationFunctions::ExecuteProgram>, "ExecuteProgram"},
        {121, D<&IApplicationFunctions::ClearUserChannel>, "ClearUserChannel"},
        {122, D<&IApplicationFunctions::UnpopToUserChannel>, "UnpopToUserChannel"},
        {123, D<&IApplicationFunctions::GetPreviousProgramIndex>, "GetPreviousProgramIndex"},
        {124, D<&IApplicationFunctions::EnableApplicationAllThreadDumpOnCrash>, "EnableApplicationAllThreadDumpOnCrash"},
        {130, D<&IApplicationFunctions::GetGpuErrorDetectedSystemEvent>, "GetGpuErrorDetectedSystemEvent"},
        {131, D<&IApplicationFunctions::SetDelayTimeToAbortOnGpuError>, "SetDelayTimeToAbortOnGpuError"},
        {140, D<&IApplicationFunctions::GetFriendInvitationStorageChannelEvent>, "GetFriendInvitationStorageChannelEvent"},
        {141, D<&IApplicationFunctions::TryPopFromFriendInvitationStorageChannel>, "TryPopFromFriendInvitationStorageChannel"},
        {150, D<&IApplicationFunctions::GetNotificationStorageChannelEvent>, "GetNotificationStorageChannelEvent"},
        {151, D<&IApplicationFunctions::TryPopFromNotificationStorageChannel>, "TryPopFromNotificationStorageChannel"},
        {160, D<&IApplicationFunctions::GetHealthWarningDisappearedSystemEvent>, "GetHealthWarningDisappearedSystemEvent"},
        {170, D<&IApplicationFunctions::SetHdcpAuthenticationActivated>, "SetHdcpAuthenticationActivated"},
        {180, D<&IApplicationFunctions::GetLaunchRequiredVersion>, "GetLaunchRequiredVersion"},
        {181, D<&IApplicationFunctions::UpgradeLaunchRequiredVersion>, "UpgradeLaunchRequiredVersion"},
        {190, D<&IApplicationFunctions::SendServerMaintenanceOverlayNotification>, "SendServerMaintenanceOverlayNotification"},
        {200, D<&IApplicationFunctions::GetLastApplicationExitReason>, "GetLastApplicationExitReason"},
        {210, D<&IApplicationFunctions::GetUnknownEvent210>, "Unknown210"},
        {220, nullptr, "Unknown220"}, // [20.0.0+]
        {300, &IApplicationFunctions::RejectUnavailableMovieCommand, "CreateMovieWriter"}, // [19.0.0+]
        {310, nullptr, "Unknown310"}, // [20.0.0+]
        {320, nullptr, "Unknown320"}, // [20.0.0+]
        {330, D<&IApplicationFunctions::Unknown330>, "Unknown330"}, // [20.0.0+]
        {500, D<&IApplicationFunctions::RejectDebugOnlyCommand>, "StartContinuousRecordingFlushForDebug"},
        {1000, &IApplicationFunctions::RejectUnavailableMovieCommand, "CreateMovieMaker"},
        {1001, D<&IApplicationFunctions::PrepareForJit>, "PrepareForJit"},
    };
    // clang-format on

    RegisterHandlers(functions);
}

IApplicationFunctions::~IApplicationFunctions() = default;

void IApplicationFunctions::RejectUnavailableMovieCommand(HLERequestContext& ctx) {
    LOG_WARNING(Service_AM, "Movie capture/encoding interface unavailable on this runtime");
    IPC::ResponseBuilder rb{ctx, 2};
    rb.Push(ResultUnknown);
}

Result IApplicationFunctions::RejectDebugOnlyCommand() {
    LOG_WARNING(Service_AM, "Rejected application-functions debug command on release runtime");
    R_RETURN(ResultUnknown);
}

Result IApplicationFunctions::PopLaunchParameter(Out<SharedPointer<IStorage>> out_storage,
                                                 LaunchParameterKind launch_parameter_kind) {
    LOG_INFO(Service_AM, "called, kind={}", launch_parameter_kind);

    std::scoped_lock lk{m_applet->lock};

    auto& channel = launch_parameter_kind == LaunchParameterKind::UserChannel
                        ? m_applet->user_channel_launch_parameter
                        : m_applet->preselected_user_launch_parameter;

    if (channel.empty()) {
        LOG_WARNING(Service_AM, "Attempted to pop parameter {} but none was found!",
                    launch_parameter_kind);
        R_THROW(AM::ResultNoDataInChannel);
    }

    auto data = channel.back();
    channel.pop_back();

    *out_storage = std::make_shared<IStorage>(system, std::move(data));
    R_SUCCEED();
}

Result IApplicationFunctions::EnsureSaveData(Out<u64> out_size, Common::UUID user_id) {
    LOG_INFO(Service_AM, "called, uid={}", user_id.FormattedString());

    FileSys::SaveDataAttribute attribute{};
    attribute.program_id = m_applet->program_id;
    attribute.user_id = user_id.AsU128();
    attribute.type = FileSys::SaveDataType::Account;

    FileSys::VirtualDir save_data{};
    R_TRY(system.GetFileSystemController().OpenSaveDataController()->CreateSaveData(
        &save_data, FileSys::SaveDataSpaceId::User, attribute));

    *out_size = 0;
    R_SUCCEED();
}

Result IApplicationFunctions::GetDesiredLanguage(Out<u64> out_language_code) {
    // FIXME: all of this stuff belongs to ns
    // TODO(bunnei): This should be configurable
    LOG_DEBUG(Service_AM, "called");

    // Get supported languages from NACP, if possible
    // Default to 0 (all languages supported)
    u32 supported_languages = 0;

    const auto res = GetApplicationMetadata(system, m_applet->program_id);
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
    R_TRY(app_man->ConvertApplicationLanguageToLanguageCode(out_language_code, desired_language));

    LOG_DEBUG(Service_AM, "got desired_language={:016X}", *out_language_code);
    R_SUCCEED();
}

Result IApplicationFunctions::SetTerminateResult(Result terminate_result) {
    LOG_INFO(Service_AM, "(STUBBED) called, result={:#x} ({:04}-{:04})",
             terminate_result.GetInnerValue(),
             static_cast<u32>(terminate_result.GetModule()) + 2000,
             terminate_result.GetDescription());

    std::scoped_lock lk{m_applet->lock};
    m_applet->terminate_result = terminate_result;

    R_SUCCEED();
}

Result IApplicationFunctions::GetDisplayVersion(Out<DisplayVersion> out_display_version) {
    LOG_DEBUG(Service_AM, "called");

    const auto res = GetApplicationMetadata(system, m_applet->program_id);
    if (res.first != nullptr) {
        const auto& version = res.first->GetVersionString();
        std::memcpy(out_display_version->string.data(), version.data(),
                    (std::min)(version.size(), out_display_version->string.size()));
    } else {
        static constexpr char default_version[]{"1.0.0"};
        std::memcpy(out_display_version->string.data(), default_version, sizeof(default_version));
    }

    out_display_version->string[out_display_version->string.size() - 1] = '\0';
#ifdef EDEN_GUEST_TITLE_VERSION_DIAG
    // Exactly one record per title: the guest's own version metadata and
    // whether the emulator had to substitute a fabricated fallback version.
    static std::atomic<u64> last_reported_title{0};
    const auto title_id = m_applet->program_id;
    if (title_id != 0 &&
        last_reported_title.exchange(title_id, std::memory_order_relaxed) != title_id) {
        LOG_WARNING(Service_AM,
                    "EDEN_GUEST_TITLE_VERSION title={:016X} metadata={} version={}",
                    title_id, res.first != nullptr,
                    res.first != nullptr ? std::string_view(out_display_version->string.data())
                                         : std::string_view{"unknown"});
    }
#endif
    R_SUCCEED();
}

Result IApplicationFunctions::ExtendSaveData(Out<u64> out_required_size, FileSys::SaveDataType type,
                                             Common::UUID user_id, u64 normal_size,
                                             u64 journal_size) {
    LOG_DEBUG(Service_AM, "called with type={} user_id={} normal={:#x} journal={:#x}",
              static_cast<u8>(type), user_id.FormattedString(), normal_size, journal_size);

    system.GetFileSystemController().OpenSaveDataController()->WriteSaveDataSize(
        type, m_applet->program_id, user_id.AsU128(), {normal_size, journal_size});

    // The following value is used to indicate the amount of space remaining on failure
    // due to running out of space. Since we always succeed, this should be 0.
    *out_required_size = 0;

    R_SUCCEED();
}

Result IApplicationFunctions::GetSaveDataSize(Out<u64> out_normal_size, Out<u64> out_journal_size,
                                              FileSys::SaveDataType type, Common::UUID user_id) {
    LOG_DEBUG(Service_AM, "called with type={} user_id={}", type, user_id.FormattedString());

    const auto size = system.GetFileSystemController().OpenSaveDataController()->ReadSaveDataSize(
        type, m_applet->program_id, user_id.AsU128());

    *out_normal_size = size.normal;
    *out_journal_size = size.journal;
    R_SUCCEED();
}

Result IApplicationFunctions::CreateCacheStorage(Out<u32> out_target_media,
                                                 Out<u64> out_required_size, u16 index,
                                                 u64 normal_size, u64 journal_size) {
    LOG_DEBUG(Service_AM, "called with index={} size={:#x} journal_size={:#x}", index, normal_size,
              journal_size);

    *out_target_media = 0;
    *out_required_size = 0;

    std::vector<u8> nacp;
    R_TRY(system.GetARPManager().GetControlProperty(&nacp, m_applet->program_id));

    auto raw_nacp = std::make_unique<FileSys::RawNACP>();
    std::memcpy(raw_nacp.get(), nacp.data(), (std::min)(sizeof(*raw_nacp), nacp.size()));

    R_UNLESS(index <= static_cast<u16>(raw_nacp->cache_storage_max_index),
             ResultCacheStorageIndexTooLarge);

    const auto max_size =
        static_cast<u64>(raw_nacp->cache_storage_data_and_journal_max_size);
    R_UNLESS(normal_size <= max_size && journal_size <= max_size - normal_size,
             ResultCacheStorageSizeTooLarge);

    auto controller = system.GetFileSystemController().OpenSaveDataController();
    FileSys::VirtualDir save_root{};
    R_TRY(controller->OpenSaveDataSpace(&save_root, FileSys::SaveDataSpaceId::User));

    const auto relative_path = GetCacheStorageRelativePath(m_applet->program_id, index);
    R_UNLESS(save_root->GetDirectoryRelative(relative_path) == nullptr,
             ResultCacheStorageAlreadyExists);

    const auto attribute = FileSys::SaveDataAttribute::Make(
        m_applet->program_id, FileSys::SaveDataType::Cache, {}, 0, index);
    FileSys::VirtualDir cache_dir{};
    R_TRY(controller->CreateSaveData(&cache_dir, FileSys::SaveDataSpaceId::User, attribute));
    R_UNLESS(cache_dir != nullptr, ResultUnknown);

    const FileSys::SaveDataSize size{normal_size, journal_size};
    const auto size_file = cache_dir->CreateFile(FileSys::GetSaveDataSizeFileName());
    if (size_file == nullptr || !size_file->Resize(sizeof(size)) ||
        size_file->WriteObject(size) != sizeof(size)) {
        if (const auto cache_root = save_root->GetDirectoryRelative("save/cache");
            cache_root != nullptr) {
            cache_root->DeleteSubdirectoryRecursive(
                GetCacheStorageDirectoryName(m_applet->program_id, index));
        }
        R_RETURN(ResultUnknown);
    }

    // Encore has one persistent save-data backing on PS5; report the medium
    // actually used instead of pretending that a distinct SD save partition exists.
    *out_target_media = 1; // Nand
    R_SUCCEED();
}

Result IApplicationFunctions::GetSaveDataSizeMax(Out<u64> out_max_normal_size,
                                                 Out<u64> out_max_journal_size) {
    std::vector<u8> nacp;
    R_TRY(system.GetARPManager().GetControlProperty(&nacp, m_applet->program_id));

    auto raw_nacp = std::make_unique<FileSys::RawNACP>();
    std::memcpy(raw_nacp.get(), nacp.data(), (std::min)(sizeof(*raw_nacp), nacp.size()));

    *out_max_normal_size = static_cast<u64>(raw_nacp->user_account_save_data_max_size);
    *out_max_journal_size =
        static_cast<u64>(raw_nacp->user_account_save_data_max_journal_size);

    LOG_DEBUG(Service_AM, "GetSaveDataSizeMax normal={:#x}, journal={:#x}",
              *out_max_normal_size, *out_max_journal_size);
    R_SUCCEED();
}

Result IApplicationFunctions::GetDeviceSaveDataSizeMax(Out<u64> out_max_normal_size,
                                                       Out<u64> out_max_journal_size) {
    std::vector<u8> nacp;
    R_TRY(system.GetARPManager().GetControlProperty(&nacp, m_applet->program_id));

    auto raw_nacp = std::make_unique<FileSys::RawNACP>();
    std::memcpy(raw_nacp.get(), nacp.data(), (std::min)(sizeof(*raw_nacp), nacp.size()));

    *out_max_normal_size = static_cast<u64>(raw_nacp->device_save_data_max_size);
    *out_max_journal_size = static_cast<u64>(raw_nacp->device_save_data_max_journal_size);

    LOG_DEBUG(Service_AM, "GetDeviceSaveDataSizeMax normal={:#x}, journal={:#x}",
              *out_max_normal_size, *out_max_journal_size);
    R_SUCCEED();
}

Result IApplicationFunctions::GetCacheStorageMax(Out<u32> out_cache_storage_index_max,
                                                 Out<u64> out_max_journal_size) {
    LOG_DEBUG(Service_AM, "called");

    std::vector<u8> nacp;
    R_TRY(system.GetARPManager().GetControlProperty(&nacp, m_applet->program_id));

    auto raw_nacp = std::make_unique<FileSys::RawNACP>();
    std::memcpy(raw_nacp.get(), nacp.data(), (std::min)(sizeof(*raw_nacp), nacp.size()));

    *out_cache_storage_index_max = static_cast<u32>(raw_nacp->cache_storage_max_index);
    *out_max_journal_size = static_cast<u64>(raw_nacp->cache_storage_data_and_journal_max_size);

    R_SUCCEED();
}

Result IApplicationFunctions::BeginBlockingHomeButtonShortAndLongPressed(s64 unused) {
    LOG_DEBUG(Service_AM, "called");

    std::scoped_lock lk{m_applet->lock};
    m_applet->home_button_long_pressed_blocked = true;
    m_applet->home_button_short_pressed_blocked = true;

    R_SUCCEED();
}

Result IApplicationFunctions::EndBlockingHomeButtonShortAndLongPressed() {
    LOG_DEBUG(Service_AM, "called");

    std::scoped_lock lk{m_applet->lock};
    m_applet->home_button_long_pressed_blocked = false;
    m_applet->home_button_short_pressed_blocked = false;

    R_SUCCEED();
}

Result IApplicationFunctions::BeginBlockingHomeButton(s64 timeout_ns) {
    LOG_DEBUG(Service_AM, "called, timeout_ns={} (automatic timeout not modeled)", timeout_ns);

    std::scoped_lock lk{m_applet->lock};
    m_applet->home_button_long_pressed_blocked = true;
    m_applet->home_button_short_pressed_blocked = true;
    m_applet->home_button_double_click_enabled = true;

    R_SUCCEED();
}

Result IApplicationFunctions::EndBlockingHomeButton() {
    LOG_DEBUG(Service_AM, "called");

    std::scoped_lock lk{m_applet->lock};
    m_applet->home_button_long_pressed_blocked = false;
    m_applet->home_button_short_pressed_blocked = false;
    m_applet->home_button_double_click_enabled = false;

    R_SUCCEED();
}

Result IApplicationFunctions::NotifyRunning(Out<bool> out_became_running) {
    LOG_DEBUG(Service_AM, "called");

    bool became_running{};
    {
        std::scoped_lock lk{m_applet->lock};
        became_running = !m_applet->is_process_running;
        m_applet->is_process_running = true;
    }

    *out_became_running = became_running;

    if (auto* window_system = system.GetAppletManager().GetWindowSystem()) {
        window_system->RequestUpdate();
    }

    R_SUCCEED();
}

Result IApplicationFunctions::GetPseudoDeviceId(Out<Common::UUID> out_pseudo_device_id) {
    LOG_WARNING(Service_AM, "(stubbed)");
    R_UNLESS(out_pseudo_device_id, ResultUnknown);

    // This should be hashed with the device specific hash
    // for now this will do
    const auto res = FileSys::PatchManager::GetMetadataFromBaseOrUpdate(system, m_applet->program_id);
    u8 hash[EVP_MAX_MD_SIZE]{};
    unsigned int hash_len = 0;
    const auto seed = res.first != nullptr ? res.first->raw.seed_for_pseudo_device_id : u64_le{0};
    EVP_MD_CTX* ctx = EVP_MD_CTX_new();
    R_UNLESS(ctx != nullptr, ResultUnknown);
    const bool ok = EVP_DigestInit_ex(ctx, EVP_sha1(), nullptr) == 1 &&
                    EVP_DigestUpdate(ctx, &seed, sizeof(seed)) == 1 &&
                    EVP_DigestFinal_ex(ctx, hash, &hash_len) == 1;
    EVP_MD_CTX_free(ctx);
    // SHA-1 must be exactly 20 bytes. Passing EVP_MAX_MD_SIZE (64) to
    // std::span<u8, 20> violates the fixed-extent constructor contract.
    R_UNLESS(ok && hash_len == 20, ResultUnknown);
    *out_pseudo_device_id = Common::UUID::MakeRFC4122V5(std::span<u8, 16>{hash, 16});
    R_SUCCEED();
}

Result IApplicationFunctions::IsGamePlayRecordingSupported(
    Out<bool> out_is_game_play_recording_supported) {
    LOG_DEBUG(Service_AM, "called");
    *out_is_game_play_recording_supported = m_applet->game_play_recording_supported;
    R_SUCCEED();
}

Result IApplicationFunctions::InitializeGamePlayRecording(
    u64 transfer_memory_size, InCopyHandle<Kernel::KTransferMemory> transfer_memory_handle) {
    LOG_WARNING(Service_AM, "(STUBBED) called");
    R_SUCCEED();
}

Result IApplicationFunctions::SetMediaPlaybackStateForApplication(bool enabled) {
    LOG_DEBUG(Service_AM, "called, enabled={}", enabled);
    std::scoped_lock lk{m_applet->lock};
    m_applet->media_playback_state = enabled;
    R_SUCCEED();
}

Result IApplicationFunctions::SetGamePlayRecordingState(
    GamePlayRecordingState game_play_recording_state) {
    LOG_DEBUG(Service_AM, "called");

    std::scoped_lock lk{m_applet->lock};
    m_applet->game_play_recording_state = game_play_recording_state;

    R_SUCCEED();
}

Result IApplicationFunctions::SetDelayTimeToAbortOnGpuError(u64 delay_ns) {
    LOG_DEBUG(Service_AM, "called, delay_ns={} (no guest GPU-abort timer)", delay_ns);
    R_SUCCEED();
}

Result IApplicationFunctions::SendServerMaintenanceOverlayNotification() {
    LOG_DEBUG(Service_AM, "called (no maintenance-overlay backend)");
    R_SUCCEED();
}

Result IApplicationFunctions::GetLastApplicationExitReason(
    Out<s32> out_last_application_exit_reason) {
    std::scoped_lock lk{m_applet->lock};
    *out_last_application_exit_reason = m_applet->last_application_exit_reason;
    LOG_DEBUG(Service_AM, "called, reason={}", *out_last_application_exit_reason);
    R_SUCCEED();
}

Result IApplicationFunctions::GetLaunchRequiredVersion(Out<u32> out_version) {
    std::scoped_lock lk{m_applet->lock};
    *out_version = m_applet->launch_required_version;
    LOG_DEBUG(Service_AM, "called, version={}", *out_version);
    R_SUCCEED();
}

Result IApplicationFunctions::UpgradeLaunchRequiredVersion(u32 version) {
    std::scoped_lock lk{m_applet->lock};
    if (version > m_applet->launch_required_version) {
        m_applet->launch_required_version = version;
    }
    LOG_DEBUG(Service_AM, "called, requested={}, effective={}", version,
              m_applet->launch_required_version);
    R_SUCCEED();
}

Result IApplicationFunctions::RequestToShutdown() {
    LOG_INFO(Service_AM, "called");
    system.Exit();
    R_SUCCEED();
}

Result IApplicationFunctions::RequestToReboot() {
    LOG_INFO(Service_AM, "called");
    // The existing global AM controller represents guest reboot with this same exit transition.
    // It never requests a reboot of the PS5 host.
    system.Exit();
    R_SUCCEED();
}

Result IApplicationFunctions::RequestToSleep() {
    LOG_DEBUG(Service_AM, "called (guest sleep has no PS5-host side effect)");
    R_SUCCEED();
}

Result IApplicationFunctions::ExitAndRequestToShowThanksMessage() {
    LOG_DEBUG(Service_AM, "called (no Horizon Thanks system-applet backend)");
    R_SUCCEED();
}

Result IApplicationFunctions::EnableApplicationCrashReport(bool enabled) {
    LOG_DEBUG(Service_AM, "called");

    std::scoped_lock lk{m_applet->lock};
    m_applet->application_crash_report_enabled = enabled;

    R_SUCCEED();
}

Result IApplicationFunctions::InitializeApplicationCopyrightFrameBuffer(
    s32 width, s32 height, u64 transfer_memory_size,
    InCopyHandle<Kernel::KTransferMemory> transfer_memory_handle) {
    constexpr s32 MaxCopyrightWidth = 1280;
    constexpr s32 MaxCopyrightHeight = 720;
    constexpr u64 TransferMemoryAlignment = 0x40000;

    const bool dimensions_valid =
        width >= 1 && width <= MaxCopyrightWidth && height >= 1 && height <= MaxCopyrightHeight;
    const u64 minimum_size =
        dimensions_valid ? static_cast<u64>(width) * static_cast<u64>(height) * 4 : 0;
    R_UNLESS(dimensions_valid && transfer_memory_size >= minimum_size &&
                 (transfer_memory_size % TransferMemoryAlignment) == 0,
             ResultInvalidCopyrightParameters);

    // The client keeps ownership of the transfer-memory lifecycle after this call. Encore
    // records the validated framebuffer contract, while screenshot compositing remains separate.
    (void)transfer_memory_handle;

    std::scoped_lock lk{m_copyright_mutex};
    m_copyright_framebuffer_initialized = true;
    m_copyright_framebuffer_width = width;
    m_copyright_framebuffer_height = height;
    m_copyright_transfer_memory_size = transfer_memory_size;
    m_copyright_visible = true;
    m_copyright_image = {};

    LOG_DEBUG(Service_AM, "called, width={}, height={}, transfer_memory_size={:#x}", width, height,
              transfer_memory_size);
    R_SUCCEED();
}

Result IApplicationFunctions::SetApplicationCopyrightImage(
    s32 x, s32 y, s32 width, s32 height, WindowOriginMode window_origin_mode,
    InBuffer<BufferAttr_HipcMapTransferAllowsNonSecure | BufferAttr_HipcMapAlias> image_data) {
    constexpr s32 MaxCopyrightWidth = 1280;
    constexpr s32 MaxCopyrightHeight = 720;

    R_UNLESS(x >= 0 && y >= 0 && width >= 1 && height >= 1 &&
                 static_cast<s32>(window_origin_mode) >= 1,
             ResultInvalidCopyrightParameters);

    const s64 right = static_cast<s64>(x) + static_cast<s64>(width);
    const s64 bottom = static_cast<s64>(y) + static_cast<s64>(height);
    const u64 required_bytes = static_cast<u64>(width) * static_cast<u64>(height) * 4;
    R_UNLESS(right <= MaxCopyrightWidth && bottom <= MaxCopyrightHeight &&
                 image_data.size() >= required_bytes,
             ResultInvalidCopyrightParameters);

    std::scoped_lock lk{m_copyright_mutex};
    if (m_copyright_framebuffer_initialized) {
        R_UNLESS(right <= m_copyright_framebuffer_width &&
                     bottom <= m_copyright_framebuffer_height,
                 ResultInvalidCopyrightParameters);
    }

    m_copyright_image.x = x;
    m_copyright_image.y = y;
    m_copyright_image.width = width;
    m_copyright_image.height = height;
    m_copyright_image.origin_mode = window_origin_mode;
    m_copyright_image.data.assign(image_data.begin(),
                                  image_data.begin() + static_cast<size_t>(required_bytes));

    LOG_DEBUG(Service_AM,
              "called, x={}, y={}, width={}, height={}, origin={}, image_bytes={:#x}", x, y, width,
              height, static_cast<s32>(window_origin_mode), required_bytes);
    R_SUCCEED();
}

Result IApplicationFunctions::SetApplicationCopyrightVisibility(bool visible) {
    std::scoped_lock lk{m_copyright_mutex};
    m_copyright_visible = visible;
    LOG_DEBUG(Service_AM, "called, visible={}", visible);
    R_SUCCEED();
}

Result IApplicationFunctions::QueryApplicationPlayStatistics(
    Out<s32> out_entries,
    OutArray<ApplicationPlayStatistics, BufferAttr_HipcMapAlias> out_play_statistics,
    InArray<u64, BufferAttr_HipcMapAlias> application_ids) {
    const size_t count = (std::min)(out_play_statistics.size(), application_ids.size());
    for (size_t i = 0; i < count; ++i) {
        const u64 application_id = application_ids[i];
        out_play_statistics[i] = {
            .application_id = application_id,
            .play_time_ns = m_play_time_manager->GetPlayTime(application_id) * 1'000'000'000ULL,
            .launch_count = Core::LaunchTimestampCache::GetLaunchCount(application_id),
        };
    }
    *out_entries = static_cast<s32>(count);
    LOG_DEBUG(Service_AM, "called, entries={}", count);
    R_SUCCEED();
}

Result IApplicationFunctions::QueryApplicationPlayStatisticsByUid(
    Out<s32> out_entries,
    OutArray<ApplicationPlayStatistics, BufferAttr_HipcMapAlias> out_play_statistics,
    Common::UUID user_id, InArray<u64, BufferAttr_HipcMapAlias> application_ids) {
    // Encore currently persists one play-time database for the active emulator profile.
    // Return that real data rather than fabricating per-user counters.
    LOG_DEBUG(Service_AM, "called, user_id={}", user_id.FormattedString());
    R_RETURN(QueryApplicationPlayStatistics(out_entries, out_play_statistics, application_ids));
}

Result IApplicationFunctions::ExecuteProgram(ProgramSpecifyKind kind, u64 value) {
    LOG_WARNING(Service_AM, "(STUBBED) called, kind={}, value={}", kind, value);
    ASSERT(kind == ProgramSpecifyKind::ExecuteProgram ||
           kind == ProgramSpecifyKind::RestartProgram);

    // Copy user channel ownership into the system so that it will be preserved
    system.GetUserChannel() = m_applet->user_channel_launch_parameter;
    system.ExecuteProgram(value);
    R_SUCCEED();
}

Result IApplicationFunctions::CreateApplicationAndPushAndRequestToStart(
    u64 application_id, SharedPointer<IStorage> storage) {
    LOG_INFO(Service_AM, "called, application_id={:016X}", application_id);

    m_applet->user_channel_launch_parameter.push_back(storage->GetData());
    const Result result = CreateApplicationAndRequestToStart(application_id);
    if (R_FAILED(result)) {
        m_applet->user_channel_launch_parameter.pop_back();
    }
    R_RETURN(result);
}

Result IApplicationFunctions::CreateApplicationAndPushAndRequestToStartForQuest(
    u32 attribute0, u32 attribute1, u64 application_id, SharedPointer<IStorage> storage) {
    LOG_INFO(Service_AM,
             "called, application_id={:016X}, quest_attr0={:#x}, quest_attr1={:#x}",
             application_id, attribute0, attribute1);

    // The Quest attributes tune kiosk launch policy. Encore can preserve the real user-channel
    // storage and application transition today, while those two policy values are not modeled.
    R_RETURN(CreateApplicationAndPushAndRequestToStart(application_id, std::move(storage)));
}

Result IApplicationFunctions::CreateApplicationAndRequestToStartForQuest(
    u32 attribute0, u32 attribute1, u64 application_id) {
    LOG_INFO(Service_AM,
             "called, application_id={:016X}, quest_attr0={:#x}, quest_attr1={:#x}",
             application_id, attribute0, attribute1);
    R_RETURN(CreateApplicationAndRequestToStart(application_id));
}

Result IApplicationFunctions::CreateApplicationWithAttributeAndPushAndRequestToStartForQuest(
    u64 application_id, InBuffer<BufferAttr_HipcMapAlias> attribute,
    SharedPointer<IStorage> storage) {
    LOG_INFO(Service_AM, "called, application_id={:016X}, attribute_size={:#x}",
             application_id, attribute.size());
    R_RETURN(CreateApplicationAndPushAndRequestToStart(application_id, std::move(storage)));
}

Result IApplicationFunctions::CreateApplicationWithAttributeAndRequestToStartForQuest(
    u64 application_id, InBuffer<BufferAttr_HipcMapAlias> attribute) {
    LOG_INFO(Service_AM, "called, application_id={:016X}, attribute_size={:#x}",
             application_id, attribute.size());
    R_RETURN(CreateApplicationAndRequestToStart(application_id));
}

// https://switchbrew.org/wiki/Applet_Manager_services#CreateApplicationAndRequestToStart
Result IApplicationFunctions::CreateApplicationAndRequestToStart(u64 application_id) {
    LOG_INFO(Service_AM, "called, application_id={:016X} current_program_id={:016X}", application_id, m_applet->program_id);

    if (application_id == 0 ||
        FileSys::GetBaseTitleID(application_id) == FileSys::GetBaseTitleID(m_applet->program_id)) {
        const auto program_index = application_id == 0
                                       ? 0
                                       : application_id - FileSys::GetBaseTitleID(application_id);

        system.GetUserChannel() = m_applet->user_channel_launch_parameter;
        system.ExecuteProgram(program_index);
        R_SUCCEED();
    }

    LOG_ERROR(Service_AM, "Launching a different application ({:016X}) is not implemented!", application_id);
    R_THROW(ResultUnknown);
}

Result IApplicationFunctions::ClearUserChannel() {
    LOG_DEBUG(Service_AM, "called");
    m_applet->user_channel_launch_parameter.clear();
    R_SUCCEED();
}

Result IApplicationFunctions::UnpopToUserChannel(SharedPointer<IStorage> storage) {
    LOG_DEBUG(Service_AM, "called");
    m_applet->user_channel_launch_parameter.push_back(storage->GetData());
    R_SUCCEED();
}

Result IApplicationFunctions::GetPreviousProgramIndex(Out<s32> out_previous_program_index) {
    LOG_DEBUG(Service_AM, "called");
    *out_previous_program_index = m_applet->previous_program_index;
    R_SUCCEED();
}

Result IApplicationFunctions::EnableApplicationAllThreadDumpOnCrash(bool enabled) {
    LOG_DEBUG(Service_AM, "called, enabled={}", enabled);
    std::scoped_lock lk{m_applet->lock};
    m_applet->all_thread_dump_on_crash_enabled = enabled;
    R_SUCCEED();
}

Result IApplicationFunctions::GetGpuErrorDetectedSystemEvent(
    OutCopyHandle<Kernel::KReadableEvent> out_event) {
    LOG_DEBUG(Service_AM, "called");
    *out_event = m_applet->gpu_error_detected_event.GetHandle();
    R_SUCCEED();
}

Result IApplicationFunctions::GetFriendInvitationStorageChannelEvent(
    OutCopyHandle<Kernel::KReadableEvent> out_event) {
    LOG_DEBUG(Service_AM, "called");
    *out_event = m_applet->friend_invitation_storage_channel_event.GetHandle();
    R_SUCCEED();
}

Result IApplicationFunctions::TryPopFromFriendInvitationStorageChannel(
    Out<SharedPointer<IStorage>> out_storage) {
    LOG_DEBUG(Service_AM, "called");

    std::scoped_lock lk{m_applet->lock};

    auto& channel = m_applet->friend_invitation_storage_channel;

    if (channel.empty()) {
        return AM::ResultNoDataInChannel;
    }

    auto data = channel.back();
    channel.pop_back();

    *out_storage = std::make_shared<IStorage>(system, std::move(data));
    R_SUCCEED();
}

Result IApplicationFunctions::GetNotificationStorageChannelEvent(
    OutCopyHandle<Kernel::KReadableEvent> out_event) {
    LOG_DEBUG(Service_AM, "called");
    *out_event = m_applet->notification_storage_channel_event.GetHandle();
    R_SUCCEED();
}

Result IApplicationFunctions::TryPopFromNotificationStorageChannel(
    Out<SharedPointer<IStorage>> out_storage) {
    LOG_DEBUG(Service_AM, "called");

    std::scoped_lock lk{m_applet->lock};
    auto& channel = m_applet->notification_storage_channel;
    if (channel.empty()) {
        R_THROW(AM::ResultNoDataInChannel);
    }

    auto data = std::move(channel.back());
    channel.pop_back();
    *out_storage = std::make_shared<IStorage>(system, std::move(data));
    R_SUCCEED();
}

Result IApplicationFunctions::GetHealthWarningDisappearedSystemEvent(
    OutCopyHandle<Kernel::KReadableEvent> out_event) {
    LOG_DEBUG(Service_AM, "called");
    *out_event = m_applet->health_warning_disappeared_system_event.GetHandle();
    R_SUCCEED();
}

Result IApplicationFunctions::SetHdcpAuthenticationActivated(bool activated) {
    LOG_DEBUG(Service_AM, "called, activated={}", activated);

    bool changed{};
    {
        std::scoped_lock lk{m_applet->lock};
        changed = m_applet->hdcp_authentication_activated != activated;
        m_applet->hdcp_authentication_activated = activated;
    }
    if (changed) {
        m_applet->lifecycle_manager.GetHDCPStateChangedEvent().Signal(system.Kernel());
    }

    R_SUCCEED();
}

Result IApplicationFunctions::GetUnknownEvent210(
    OutCopyHandle<Kernel::KReadableEvent> out_event) {
    LOG_DEBUG(Service_AM, "called");
    *out_event = m_applet->unknown_event.GetHandle();
    R_SUCCEED();
}

Result IApplicationFunctions::Unknown330(Out<u8> out) {
    LOG_DEBUG(Service_AM, "called");
    *out = 0;
    R_SUCCEED();
}

Result IApplicationFunctions::PrepareForJit() {
    LOG_DEBUG(Service_AM, "called");

    std::scoped_lock lk{m_applet->lock};
    m_applet->jit_service_launched = true;

    R_SUCCEED();
}

} // namespace Service::AM
