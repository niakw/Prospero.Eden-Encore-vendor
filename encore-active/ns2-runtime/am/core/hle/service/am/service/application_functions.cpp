// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

// SPDX-FileCopyrightText: Copyright 2024 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <array>
#include <cstdio>
#include <mutex>
#include <optional>

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
#include "core/hle/service/am/am_results.h"
#include "core/hle/service/am/applet.h"
#include "core/hle/service/am/service/application_functions.h"
#include "core/hle/service/am/service/storage.h"
#include "core/hle/service/cmif_serialization.h"
#include "core/hle/service/filesystem/filesystem.h"
#include "core/hle/service/filesystem/save_data_controller.h"
#include "core/hle/service/glue/glue_manager.h"
#include "core/hle/service/ns/application_manager_interface.h"
#include "core/hle/service/ns/service_getter_interface.h"
#include "core/hle/service/sm/sm.h"

namespace Service::AM {

namespace {

// Diagnostic only: no changes to the language result or guest IPC layout.
// Bound release stderr traffic and collapse consecutive identical replies,
// including across repeated queries to the same AM instance.
struct GuestLanguageReply {
    u64 program_id;
    u32 supported_languages;
    int configured_index;
    u64 language_code;
    bool has_metadata;
    bool operator==(const GuestLanguageReply&) const = default;
};

bool ShouldReportGuestLanguage(const GuestLanguageReply& reply) {
    static std::mutex mutex;
    static std::optional<GuestLanguageReply> last;
    static unsigned emitted = 0;
    std::scoped_lock lock{mutex};
    if (emitted >= 32 || (last && *last == reply)) return false;
    last = reply;
    ++emitted;
    return true;
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
    : ServiceFramework{system_, "IApplicationFunctions"}, m_applet{std::move(applet)} {
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
        {36, D<&IApplicationFunctions::GetLimitedApplicationLicense>, "GetLimitedApplicationLicense"},
        {37, D<&IApplicationFunctions::GetLimitedApplicationLicenseUpgradableEvent>, "GetLimitedApplicationLicenseUpgradableEvent"},
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
        {180, &IApplicationFunctions::GetLaunchRequiredVersion, "GetLaunchRequiredVersion"},
        {181, &IApplicationFunctions::UpgradeLaunchRequiredVersion, "UpgradeLaunchRequiredVersion"},
        {190, &IApplicationFunctions::SendServerMaintenanceOverlayNotification, "SendServerMaintenanceOverlayNotification"},
        {200, D<&IApplicationFunctions::GetLastApplicationExitReason>, "GetLastApplicationExitReason"},
        {210, D<&IApplicationFunctions::GetUnknownEvent210>, "Unknown210"},
        {220, nullptr, "Unknown220"}, // [20.0.0+]
        {300, &IApplicationFunctions::RejectUnavailableMovieCommand, "CreateMovieWriter"}, // [19.0.0+]
        {310, nullptr, "Unknown310"}, // [20.0.0+]
        {320, D<&IApplicationFunctions::GetScreenShotAndContinuousRecordingInitiallyEnabled>,
         "GetScreenShotAndContinuousRecordingInitiallyEnabled"}, // NS2 SDK 20.0+
        // SDKs that request 321 expect a full one-byte response, not a
        // successful IPC reply with stale data left in the output word.
        {321, D<&IApplicationFunctions::Unknown321>, "Unknown321"}, // Switch 2
        {330, D<&IApplicationFunctions::Unknown330>, "Unknown330"}, // [20.0.0+]
        {500, D<&IApplicationFunctions::RejectDebugOnlyCommand>, "StartContinuousRecordingFlushForDebug"},
        {1000, &IApplicationFunctions::RejectUnavailableMovieCommand, "CreateMovieMaker"},
        {1001, D<&IApplicationFunctions::PrepareForJit>, "PrepareForJit"},
    };
    // clang-format on

    RegisterHandlers(functions);
}

IApplicationFunctions::~IApplicationFunctions() = default;

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
    const GuestLanguageReply reply{
        m_applet->program_id, supported_languages,
        static_cast<int>(Settings::values.language_index.GetValue()),
        *out_language_code, res.first != nullptr};
    if (ShouldReportGuestLanguage(reply)) {
        char code[9]{};
        for (unsigned i = 0; i < 8; ++i)
            code[i] = static_cast<char>((reply.language_code >> (8u * i)) & 0xffu);
        std::fprintf(stderr,
                     "EDEN_GUEST_LANGUAGE_REPLY title=%016llX metadata=%d supported_languages=%08X configured_index=%d reply_code=%016llX reply_text=%s\n",
                     static_cast<unsigned long long>(reply.program_id),
                     static_cast<int>(reply.has_metadata),
                     static_cast<unsigned>(reply.supported_languages),
                     reply.configured_index,
                     static_cast<unsigned long long>(reply.language_code), code);
    }
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
                                             u128 user_id, u64 normal_size,
                                             u64 journal_size) {
    LOG_DEBUG(Service_AM, "called with type={} user_id={:016X}{:016X} normal={:#x} journal={:#x}",
              static_cast<u8>(type), user_id[1], user_id[0], normal_size, journal_size);

    system.GetFileSystemController().OpenSaveDataController()->WriteSaveDataSize(
        type, m_applet->program_id, user_id, {normal_size, journal_size});

    // The following value is used to indicate the amount of space remaining on failure
    // due to running out of space. Since we always succeed, this should be 0.
    *out_required_size = 0;

    R_SUCCEED();
}

Result IApplicationFunctions::GetSaveDataSize(Out<u64> out_normal_size, Out<u64> out_journal_size,
                                              FileSys::SaveDataType type, u128 user_id) {
    LOG_DEBUG(Service_AM, "called with type={} user_id={:016X}{:016X}",
              static_cast<u8>(type), user_id[1], user_id[0]);

    const auto size = system.GetFileSystemController().OpenSaveDataController()->ReadSaveDataSize(
        type, m_applet->program_id, user_id);

    *out_normal_size = size.normal;
    *out_journal_size = size.journal;
    R_SUCCEED();
}

Result IApplicationFunctions::CreateCacheStorage(Out<u32> out_target_media,
                                                 Out<u64> out_required_size, u16 index,
                                                 u64 normal_size, u64 journal_size) {
    LOG_WARNING(Service_AM, "(STUBBED) called with index={} size={:#x} journal_size={:#x}", index,
                normal_size, journal_size);

    *out_target_media = 1; // Nand
    *out_required_size = 0;

    R_SUCCEED();
}

Result IApplicationFunctions::GetSaveDataSizeMax(Out<u64> out_max_normal_size,
                                                 Out<u64> out_max_journal_size) {
    LOG_WARNING(Service_AM, "(STUBBED) called");

    *out_max_normal_size = 0xFFFFFFF;
    *out_max_journal_size = 0xFFFFFFF;

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
    LOG_WARNING(Service_AM, "(STUBBED) called");

    std::scoped_lock lk{m_applet->lock};
    m_applet->home_button_long_pressed_blocked = true;
    m_applet->home_button_short_pressed_blocked = true;

    R_SUCCEED();
}

Result IApplicationFunctions::EndBlockingHomeButtonShortAndLongPressed() {
    LOG_WARNING(Service_AM, "(STUBBED) called");

    std::scoped_lock lk{m_applet->lock};
    m_applet->home_button_long_pressed_blocked = false;
    m_applet->home_button_short_pressed_blocked = false;

    R_SUCCEED();
}

Result IApplicationFunctions::BeginBlockingHomeButton(s64 timeout_ns) {
    LOG_WARNING(Service_AM, "(STUBBED) called, timeout_ns={}", timeout_ns);

    std::scoped_lock lk{m_applet->lock};
    m_applet->home_button_long_pressed_blocked = true;
    m_applet->home_button_short_pressed_blocked = true;
    m_applet->home_button_double_click_enabled = true;

    R_SUCCEED();
}

Result IApplicationFunctions::EndBlockingHomeButton() {
    LOG_WARNING(Service_AM, "(STUBBED) called");

    std::scoped_lock lk{m_applet->lock};
    m_applet->home_button_long_pressed_blocked = false;
    m_applet->home_button_short_pressed_blocked = false;
    m_applet->home_button_double_click_enabled = false;

    R_SUCCEED();
}

Result IApplicationFunctions::NotifyRunning(Out<bool> out_became_running) {
    LOG_WARNING(Service_AM, "(STUBBED) called");
    *out_became_running = true;
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
    // The maintained UUID ABI consumes the first 16 octets of the verified
    // 20-byte SHA-1 digest when constructing the RFC 4122 v5 UUID.
    *out_pseudo_device_id = Common::UUID::MakeRFC4122V5(std::span<u8, 16>{hash, 16});
    R_SUCCEED();
}

Result IApplicationFunctions::IsGamePlayRecordingSupported(
    Out<bool> out_is_game_play_recording_supported) {
    LOG_WARNING(Service_AM, "(STUBBED) called");
    *out_is_game_play_recording_supported = m_applet->game_play_recording_supported;
    R_SUCCEED();
}

Result IApplicationFunctions::InitializeGamePlayRecording(
    u64 transfer_memory_size, InCopyHandle<Kernel::KTransferMemory> transfer_memory_handle) {
    LOG_WARNING(Service_AM, "(STUBBED) called");
    R_SUCCEED();
}

Result IApplicationFunctions::SetMediaPlaybackStateForApplication(bool enabled) {
    LOG_WARNING(Service_AM, "(stubbed) {}", enabled);
    std::scoped_lock lk{m_applet->lock};
    m_applet->media_playback_state = enabled;
    R_SUCCEED();
}

Result IApplicationFunctions::SetGamePlayRecordingState(
    GamePlayRecordingState game_play_recording_state) {
    LOG_WARNING(Service_AM, "(STUBBED) called");

    std::scoped_lock lk{m_applet->lock};
    m_applet->game_play_recording_state = game_play_recording_state;

    R_SUCCEED();
}

Result IApplicationFunctions::EnableApplicationCrashReport(bool enabled) {
    LOG_WARNING(Service_AM, "(STUBBED) called");

    std::scoped_lock lk{m_applet->lock};
    m_applet->application_crash_report_enabled = enabled;

    R_SUCCEED();
}

Result IApplicationFunctions::InitializeApplicationCopyrightFrameBuffer(
    s32 width, s32 height, u64 transfer_memory_size,
    InCopyHandle<Kernel::KTransferMemory> transfer_memory_handle) {
    LOG_WARNING(Service_AM, "(STUBBED) called");
    R_SUCCEED();
}

Result IApplicationFunctions::SetApplicationCopyrightImage(
    s32 x, s32 y, s32 width, s32 height, WindowOriginMode window_origin_mode,
    InBuffer<BufferAttr_HipcMapTransferAllowsNonSecure | BufferAttr_HipcMapAlias> image_data) {
    LOG_WARNING(Service_AM, "(STUBBED) called");
    R_SUCCEED();
}

Result IApplicationFunctions::SetApplicationCopyrightVisibility(bool visible) {
    LOG_WARNING(Service_AM, "(STUBBED) called, is_visible={}", visible);
    R_SUCCEED();
}

Result IApplicationFunctions::QueryApplicationPlayStatistics(
    Out<s32> out_entries,
    OutArray<ApplicationPlayStatistics, BufferAttr_HipcMapAlias> out_play_statistics,
    InArray<u64, BufferAttr_HipcMapAlias> application_ids) {
    LOG_WARNING(Service_AM, "(STUBBED) called");
    *out_entries = 0;
    R_SUCCEED();
}

Result IApplicationFunctions::QueryApplicationPlayStatisticsByUid(
    Out<s32> out_entries,
    OutArray<ApplicationPlayStatistics, BufferAttr_HipcMapAlias> out_play_statistics,
    Common::UUID user_id, InArray<u64, BufferAttr_HipcMapAlias> application_ids) {
    LOG_WARNING(Service_AM, "(STUBBED) called");
    *out_entries = 0;
    R_SUCCEED();
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
    LOG_WARNING(Service_AM, "(STUBBED) called");
    *out_previous_program_index = m_applet->previous_program_index;
    R_SUCCEED();
}

Result IApplicationFunctions::GetGpuErrorDetectedSystemEvent(
    OutCopyHandle<Kernel::KReadableEvent> out_event) {
    LOG_WARNING(Service_AM, "(STUBBED) called");
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

Result IApplicationFunctions::GetHealthWarningDisappearedSystemEvent(
    OutCopyHandle<Kernel::KReadableEvent> out_event) {
    LOG_DEBUG(Service_AM, "called");
    *out_event = m_applet->health_warning_disappeared_system_event.GetHandle();
    R_SUCCEED();
}

Result IApplicationFunctions::GetUnknownEvent210(
    OutCopyHandle<Kernel::KReadableEvent> out_event) {
    LOG_DEBUG(Service_AM, "called");
    *out_event = m_applet->unknown_event.GetHandle();
    R_SUCCEED();
}

// Experimental NS2 AM command 320; preserve maintained Encore metadata logic.
Result IApplicationFunctions::GetScreenShotAndContinuousRecordingInitiallyEnabled(
    Out<bool> out_screenshot_enabled, Out<bool> out_recording_enabled) {
    std::scoped_lock lk{m_applet->lock};
    *out_screenshot_enabled = m_applet->screenshot_permission != ScreenshotPermission::Disable;
    *out_recording_enabled = m_applet->game_play_recording_supported &&
                             m_applet->game_play_recording_state == GamePlayRecordingState::Enabled;
    LOG_DEBUG(Service_AM, "called, screenshot={}, recording={}", *out_screenshot_enabled,
              *out_recording_enabled);
    R_SUCCEED();
}

void IApplicationFunctions::RejectUnavailableMovieCommand(HLERequestContext& ctx) {
    LOG_WARNING(Service_AM, "Movie capture/encoding interface unavailable on this runtime");
    IPC::ResponseBuilder rb{ctx, 2};
    rb.Push(ResultUnknown);
}

Result IApplicationFunctions::RejectDebugOnlyCommand() {
    LOG_WARNING(Service_AM, "Rejected application-functions debug command on release runtime");
    R_RETURN(ResultUnknown);
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

Result IApplicationFunctions::GetLimitedApplicationLicense(Out<u8> out_license_type) {
    *out_license_type = 0;
    LOG_DEBUG(Service_AM, "called, no limited/trial application license");
    R_SUCCEED();
}

Result IApplicationFunctions::GetLimitedApplicationLicenseUpgradableEvent(
    OutCopyHandle<Kernel::KReadableEvent> out_event) {
    // GetLimitedApplicationLicense reports the normal/full-license path (0), so there is
    // no pending limited-license upgrade to signal. Reuse the applet's persistent,
    // currently-unsignaled event rather than manufacturing a successful upgrade.
    LOG_DEBUG(Service_AM, "called, no limited application license upgrade pending");
    *out_event = m_applet->unknown_event.GetHandle();
    R_SUCCEED();
}

Result IApplicationFunctions::SetDelayTimeToAbortOnGpuError(u64 delay_ns) {
    LOG_DEBUG(Service_AM, "called, delay_ns={} (no guest GPU-abort timer)", delay_ns);
    R_SUCCEED();
}

void IApplicationFunctions::SendServerMaintenanceOverlayNotification(HLERequestContext& ctx) {
    IPC::RequestParser rp{ctx};
    [[maybe_unused]] const auto notification = rp.PopRaw<std::array<u8, 0x10>>();
    LOG_DEBUG(Service_AM, "called (maintenance overlay unavailable; payload consumed)");
    IPC::ResponseBuilder rb{ctx, 2};
    rb.Push(ResultSuccess);
}

void IApplicationFunctions::GetLaunchRequiredVersion(HLERequestContext& ctx) {
    IPC::RequestParser rp{ctx};
    [[maybe_unused]] const auto request = rp.PopRaw<std::array<u8, 0x10>>();
    LOG_WARNING(Service_AM, "GetLaunchRequiredVersion HOS23 payload is not decoded; rejecting");
    IPC::ResponseBuilder rb{ctx, 2};
    rb.Push(ResultUnknown);
}

void IApplicationFunctions::UpgradeLaunchRequiredVersion(HLERequestContext& ctx) {
    IPC::RequestParser rp{ctx};
    [[maybe_unused]] const auto request = rp.PopRaw<std::array<u8, 0x50>>();
    LOG_WARNING(Service_AM, "UpgradeLaunchRequiredVersion HOS23 payload is not decoded; rejecting");
    IPC::ResponseBuilder rb{ctx, 2};
    rb.Push(ResultUnknown);
}

Result IApplicationFunctions::GetLastApplicationExitReason(
    Out<s32> out_last_application_exit_reason) {
    std::scoped_lock lk{m_applet->lock};
    *out_last_application_exit_reason = m_applet->last_application_exit_reason;
    LOG_DEBUG(Service_AM, "called, reason={}", *out_last_application_exit_reason);
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

Result IApplicationFunctions::EnableApplicationAllThreadDumpOnCrash(bool enabled) {
    LOG_DEBUG(Service_AM, "called, enabled={}", enabled);
    std::scoped_lock lk{m_applet->lock};
    m_applet->all_thread_dump_on_crash_enabled = enabled;
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

Result IApplicationFunctions::Unknown321(Out<u8> out) {
    // Upstream Lichii/Eglantine NS2 initialization requires one data byte.
    // Meaning is unknown; report a deterministic zero without auto-stubbing.
    LOG_WARNING(Service_AM, "(STUBBED) called");
    *out = 0;
    R_SUCCEED();
}

Result IApplicationFunctions::Unknown330(Out<u8> out) {
    LOG_DEBUG(Service_AM, "called");
    *out = 0;
    R_SUCCEED();
}

Result IApplicationFunctions::PrepareForJit() {
    LOG_WARNING(Service_AM, "(STUBBED) called");

    std::scoped_lock lk{m_applet->lock};
    m_applet->jit_service_launched = true;

    R_SUCCEED();
}

} // namespace Service::AM
