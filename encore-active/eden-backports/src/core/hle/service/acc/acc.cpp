// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

// SPDX-FileCopyrightText: Copyright 2018 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <algorithm>
#include <array>

#include "common/common_types.h"
#include "common/fs/file.h"
#include "common/fs/path_util.h"
#include "common/logging.h"
#include <ranges>
#include "common/stb.h"
#include "common/string_util.h"
#include "common/swap.h"
#include "core/constants.h"
#include "core/core.h"
#include "core/core_timing.h"
#include "core/file_sys/control_metadata.h"
#include "core/file_sys/patch_manager.h"
#include "core/hle/service/acc/acc.h"
#include "core/hle/service/acc/async_context.h"
#include "core/hle/service/acc/errors.h"
#include "core/hle/service/acc/profile_manager.h"
#include "core/hle/service/cmif_serialization.h"
#include "core/hle/service/glue/glue_manager.h"
#include "core/hle/service/server_manager.h"
#include "core/loader/loader.h"

namespace Service::Account {

// Thumbnails are hard coded to be at least this size
constexpr std::size_t THUMBNAIL_SIZE = 0x24000;

static std::filesystem::path GetImagePath(const Common::UUID& uuid) {
    return Common::FS::GetEdenPath(Common::FS::EdenPath::NANDDir) /
           fmt::format("system/save/8000000000000010/su/avators/{}.jpg", uuid.FormattedString());
}

static std::filesystem::path GetSaveDataThumbnailPath(const Common::UUID& uuid, u64 tid) {
    return Common::FS::GetEdenPath(Common::FS::EdenPath::NANDDir) /
           fmt::format("system/save/8000000000000010/su/thumbnails/{}/{:016X}.bin",
                       uuid.FormattedString(), tid);
}

static void JPGToMemory(void* context, void* data, int len) {
    std::vector<u8>* jpg_image = static_cast<std::vector<u8>*>(context);
    unsigned char* jpg = static_cast<unsigned char*>(data);
    jpg_image->insert(jpg_image->end(), jpg, jpg + len);
}

static void SanitizeJPEGImageSize(std::vector<u8>& image) {
    constexpr std::size_t max_jpeg_image_size = 0x20000;
    constexpr int profile_dimensions = 256;
    int original_width, original_height, color_channels;

    const auto plain_image =
        stbi_load_from_memory(image.data(), static_cast<int>(image.size()), &original_width,
                              &original_height, &color_channels, STBI_rgb);

    // Resize image to match 256*256
    if (original_width != profile_dimensions || original_height != profile_dimensions) {
        // Use vector instead of array to avoid overflowing the stack
        std::vector<u8> out_image(profile_dimensions * profile_dimensions * STBI_rgb);
        stbir_resize_uint8_srgb(plain_image, original_width, original_height, 0, out_image.data(),
                                profile_dimensions, profile_dimensions, 0, STBI_rgb, 0,
                                STBIR_FILTER_BOX);
        image.clear();
        if (!stbi_write_jpg_to_func(JPGToMemory, &image, profile_dimensions, profile_dimensions,
                                    STBI_rgb, out_image.data(), 0)) {
            LOG_ERROR(Service_ACC, "Failed to resize the user provided image.");
        }
    }

    image.resize((std::min)(image.size(), max_jpeg_image_size));
}

class IManagerForSystemService final : public ServiceFramework<IManagerForSystemService> {
public:
    explicit IManagerForSystemService(Core::System& system_, Common::UUID uuid)
        : ServiceFramework{system_, "IManagerForSystemService"}, account_id{uuid} {
        // clang-format off
        static const FunctionInfo functions[] = {
            {0, D<&IManagerForSystemService::CheckAvailability>, "CheckAvailability"},
            {1, D<&IManagerForSystemService::GetAccountId>, "GetAccountId"},
            {2, &IManagerForSystemService::RejectUnavailableNintendoAccount, "EnsureIdTokenCacheAsync"},
            {3, D<&IManagerForSystemService::LoadIdTokenCacheDeprecated>, "LoadIdTokenCacheDeprecated"}, // 19.0.0+
            {4, D<&IManagerForSystemService::LoadIdTokenCache>, "LoadIdTokenCache"}, // 19.0.0+
            {100, &IManagerForSystemService::RejectUnavailableNintendoAccount, "SetSystemProgramIdentification"},
            {101, &IManagerForSystemService::RejectUnavailableNintendoAccount, "RefreshNotificationTokenAsync"}, // 7.0.0+
            {110, &IManagerForSystemService::RejectUnavailableNintendoAccount, "GetServiceEntryRequirementCache"}, // 4.0.0+
            {111, &IManagerForSystemService::RejectUnavailableNintendoAccount, "InvalidateServiceEntryRequirementCache"}, // 4.0.0+
            {112, &IManagerForSystemService::RejectUnavailableNintendoAccount, "InvalidateTokenCache"}, // 4.0.0 - 6.2.0
            {113, &IManagerForSystemService::RejectUnavailableNintendoAccount, "GetServiceEntryRequirementCacheForOnlinePlay"}, // 6.1.0+
            {120, &IManagerForSystemService::RejectUnavailableNintendoAccount, "GetNintendoAccountId"},
            {121, &IManagerForSystemService::RejectUnavailableNintendoAccount, "CalculateNintendoAccountAuthenticationFingerprint"}, // 9.0.0+
            {130, &IManagerForSystemService::RejectUnavailableNintendoAccount, "GetNintendoAccountUserResourceCache"},
            {131, &IManagerForSystemService::RejectUnavailableNintendoAccount, "RefreshNintendoAccountUserResourceCacheAsync"},
            {132, &IManagerForSystemService::RejectUnavailableNintendoAccount, "RefreshNintendoAccountUserResourceCacheAsyncIfSecondsElapsed"},
            {133, &IManagerForSystemService::RejectUnavailableNintendoAccount, "GetNintendoAccountVerificationUrlCache"}, // 9.0.0+
            {134, &IManagerForSystemService::RejectUnavailableNintendoAccount, "RefreshNintendoAccountVerificationUrlCache"}, // 9.0.0+
            {135, &IManagerForSystemService::RejectUnavailableNintendoAccount, "RefreshNintendoAccountVerificationUrlCacheAsyncIfSecondsElapsed"}, // 9.0.0+
            {136, &IManagerForSystemService::RejectUnavailableNintendoAccount, "GetNintendoAccountUserResourceCache"}, // 19.0.0+
            {140, &IManagerForSystemService::RejectUnavailableNintendoAccount, "GetNetworkServiceLicenseCache"}, // 5.0.0+
            {141, &IManagerForSystemService::RejectUnavailableNintendoAccount, "RefreshNetworkServiceLicenseCacheAsync"}, // 5.0.0+
            {142, &IManagerForSystemService::RejectUnavailableNintendoAccount, "RefreshNetworkServiceLicenseCacheAsyncIfSecondsElapsed"}, // 5.0.0+
            {143, D<&IManagerForSystemService::GetNetworkServiceLicenseCacheEx>, "GetNetworkServiceLicenseCacheEx"}, // 15.0.0+
            {150, &IManagerForSystemService::RejectUnavailableNintendoAccount, "CreateAuthorizationRequest"},
            {160, &IManagerForSystemService::RejectUnavailableNintendoAccount, "RequiresUpdateNetworkServiceAccountIdTokenCache"},
            {161, &IManagerForSystemService::RejectUnavailableNintendoAccount, "RequireReauthenticationOfNetworkServiceAccount"},
            {170, &IManagerForSystemService::RejectUnavailableNintendoAccount, "CreateDeviceHistoryRequest"}, // 17.0.0+
            {180, &IManagerForSystemService::RejectUnavailableNintendoAccount, "GetRequestForNintendoAccountReauthentication"} // 18.0.0+
        };
        // clang-format on

        RegisterHandlers(functions);
    }

private:
    Result CheckAvailability() {
        LOG_WARNING(Service_ACC, "(STUBBED) called");
        R_SUCCEED();
    }

    Result GetAccountId(Out<u64> out_account_id) {
        LOG_WARNING(Service_ACC, "(STUBBED) called");
        *out_account_id = account_id.Hash();
        R_SUCCEED();
    }

    Result LoadIdTokenCacheDeprecated() {
        LOG_WARNING(Service_ACC, "(STUBBED) called");
        R_SUCCEED();
    }

    Result LoadIdTokenCache() {
        LOG_WARNING(Service_ACC, "(STUBBED) called");
        R_SUCCEED();
    }

    Result GetNetworkServiceLicenseCacheEx(Out<u32> out_license, Out<s64> out_expiration) {
        LOG_DEBUG(Service_ACC, "(STUBBED) called.");

        *out_license = 0;
        *out_expiration = 0;

        R_SUCCEED();
    }

    Common::UUID account_id;

    void RejectUnavailableNintendoAccount(HLERequestContext& ctx) {
        LOG_WARNING(Service_ACC, "Rejected IManagerForSystemService Nintendo Account/network command: backend unavailable");
        IPC::ResponseBuilder rb{ctx, 2};
        rb.Push(ResultUnknown);
    }

};

// 3.0.0+
class IFloatingRegistrationRequest final : public ServiceFramework<IFloatingRegistrationRequest> {
public:
    explicit IFloatingRegistrationRequest(Core::System& system_, Common::UUID)
        : ServiceFramework{system_, "IFloatingRegistrationRequest"} {
        // clang-format off
        static const FunctionInfo functions[] = {
            {0, &IFloatingRegistrationRequest::RejectUnavailableNintendoAccount, "GetSessionId"},
            {12, &IFloatingRegistrationRequest::RejectUnavailableNintendoAccount, "GetAccountId"},
            {13, &IFloatingRegistrationRequest::RejectUnavailableNintendoAccount, "GetLinkedNintendoAccountId"},
            {14, &IFloatingRegistrationRequest::RejectUnavailableNintendoAccount, "GetNickname"},
            {15, &IFloatingRegistrationRequest::RejectUnavailableNintendoAccount, "GetProfileImage"},
            {16, &IFloatingRegistrationRequest::RejectUnavailableNintendoAccount, "GetProfileLargeImage"}, // 18.0.0+
            {21, &IFloatingRegistrationRequest::RejectUnavailableNintendoAccount, "LoadIdTokenCache"},
            {100, &IFloatingRegistrationRequest::RejectUnavailableNintendoAccount, "RegisterUser"}, // [1.0.0-3.0.2] RegisterAsync
            {101, &IFloatingRegistrationRequest::RejectUnavailableNintendoAccount, "RegisterUserWithUid"}, // [1.0.0-3.0.2] RegisterWithUidAsync
            {102, &IFloatingRegistrationRequest::RejectUnavailableNintendoAccount, "RegisterNetworkServiceAccountAsync"}, // 4.0.0+
            {103, &IFloatingRegistrationRequest::RejectUnavailableNintendoAccount, "RegisterNetworkServiceAccountWithUidAsync"}, // 4.0.0+
            {110, &IFloatingRegistrationRequest::RejectUnavailableNintendoAccount, "SetSystemProgramIdentification"},
            {111, &IFloatingRegistrationRequest::RejectUnavailableNintendoAccount, "EnsureIdTokenCacheAsync"},
        };
        // clang-format on

        RegisterHandlers(functions);
    }

    void RejectUnavailableNintendoAccount(HLERequestContext& ctx) {
        LOG_WARNING(Service_ACC, "Rejected IFloatingRegistrationRequest Nintendo Account/network command: backend unavailable");
        IPC::ResponseBuilder rb{ctx, 2};
        rb.Push(ResultUnknown);
    }

};

class IAdministrator final : public ServiceFramework<IAdministrator> {
public:
    explicit IAdministrator(Core::System& system_, Common::UUID)
        : ServiceFramework{system_, "IAdministrator"} {
        // clang-format off
        static const FunctionInfo functions[] = {
            {0, &IAdministrator::RejectUnavailableNintendoAccount, "CheckAvailability"},
            {1, &IAdministrator::RejectUnavailableNintendoAccount, "GetAccountId"},
            {2, &IAdministrator::RejectUnavailableNintendoAccount, "EnsureIdTokenCacheAsync"},
            {3, &IAdministrator::RejectUnavailableNintendoAccount, "LoadIdTokenCache"},
            {100, &IAdministrator::RejectUnavailableNintendoAccount, "SetSystemProgramIdentification"},
            {101, &IAdministrator::RejectUnavailableNintendoAccount, "RefreshNotificationTokenAsync"}, // 7.0.0+
            {110, &IAdministrator::RejectUnavailableNintendoAccount, "GetServiceEntryRequirementCache"}, // 4.0.0+
            {111, &IAdministrator::RejectUnavailableNintendoAccount, "InvalidateServiceEntryRequirementCache"}, // 4.0.0+
            {112, &IAdministrator::RejectUnavailableNintendoAccount, "InvalidateTokenCache"}, // 4.0.0 - 6.2.0
            {113, &IAdministrator::RejectUnavailableNintendoAccount, "GetServiceEntryRequirementCacheForOnlinePlay"}, // 6.1.0+
            {120, &IAdministrator::RejectUnavailableNintendoAccount, "GetNintendoAccountId"},
            {121, &IAdministrator::RejectUnavailableNintendoAccount, "CalculateNintendoAccountAuthenticationFingerprint"}, // 9.0.0+
            {130, &IAdministrator::RejectUnavailableNintendoAccount, "GetNintendoAccountUserResourceCache"},
            {131, &IAdministrator::RejectUnavailableNintendoAccount, "RefreshNintendoAccountUserResourceCacheAsync"},
            {132, &IAdministrator::RejectUnavailableNintendoAccount, "RefreshNintendoAccountUserResourceCacheAsyncIfSecondsElapsed"},
            {133, &IAdministrator::RejectUnavailableNintendoAccount, "GetNintendoAccountVerificationUrlCache"}, // 9.0.0+
            {134, &IAdministrator::RejectUnavailableNintendoAccount, "RefreshNintendoAccountVerificationUrlCacheAsync"}, // 9.0.0+
            {135, &IAdministrator::RejectUnavailableNintendoAccount, "RefreshNintendoAccountVerificationUrlCacheAsyncIfSecondsElapsed"}, // 9.0.0+
            {140, &IAdministrator::RejectUnavailableNintendoAccount, "GetNetworkServiceLicenseCache"}, // 5.0.0+
            {141, &IAdministrator::RejectUnavailableNintendoAccount, "RefreshNetworkServiceLicenseCacheAsync"}, // 5.0.0+
            {142, &IAdministrator::RejectUnavailableNintendoAccount, "RefreshNetworkServiceLicenseCacheAsyncIfSecondsElapsed"}, // 5.0.0+
            {143, &IAdministrator::RejectUnavailableNintendoAccount, "GetNetworkServiceLicenseCacheEx"},
            {150, &IAdministrator::RejectUnavailableNintendoAccount, "CreateAuthorizationRequest"},
            {160, &IAdministrator::RejectUnavailableNintendoAccount, "RequiresUpdateNetworkServiceAccountIdTokenCache"},
            {161, &IAdministrator::RejectUnavailableNintendoAccount, "RequireReauthenticationOfNetworkServiceAccount"},
            {170, &IAdministrator::RejectUnavailableNintendoAccount, "CreateDeviceHistoryRequest"}, // 17.0.0+
            {180, &IAdministrator::RejectUnavailableNintendoAccount, "GetRequestForNintendoAccountReauthentication"}, // 18.0.0+
            {200, &IAdministrator::RejectUnavailableNintendoAccount, "IsRegistered"},
            {201, &IAdministrator::RejectUnavailableNintendoAccount, "RegisterAsync"},
            {202, &IAdministrator::RejectUnavailableNintendoAccount, "UnregisterAsync"},
            {203, &IAdministrator::RejectUnavailableNintendoAccount, "DeleteRegistrationInfoLocally"},
            {220, &IAdministrator::RejectUnavailableNintendoAccount, "SynchronizeProfileAsync"},
            {221, &IAdministrator::RejectUnavailableNintendoAccount, "UploadProfileAsync"},
            {222, &IAdministrator::RejectUnavailableNintendoAccount, "SynchronizaProfileAsyncIfSecondsElapsed"},
            {250, &IAdministrator::IsLinkedWithNintendoAccount, "IsLinkedWithNintendoAccount"},
            {251, &IAdministrator::RejectUnavailableNintendoAccount, "CreateProcedureToLinkWithNintendoAccount"},
            {252, &IAdministrator::RejectUnavailableNintendoAccount, "ResumeProcedureToLinkWithNintendoAccount"},
            {255, &IAdministrator::RejectUnavailableNintendoAccount, "CreateProcedureToUpdateLinkageStateOfNintendoAccount"},
            {256, &IAdministrator::RejectUnavailableNintendoAccount, "ResumeProcedureToUpdateLinkageStateOfNintendoAccount"},
            {260, &IAdministrator::RejectUnavailableNintendoAccount, "CreateProcedureToLinkNnidWithNintendoAccount"}, // 3.0.0+
            {261, &IAdministrator::RejectUnavailableNintendoAccount, "ResumeProcedureToLinkNnidWithNintendoAccount"}, // 3.0.0+
            {280, &IAdministrator::RejectUnavailableNintendoAccount, "ProxyProcedureToAcquireApplicationAuthorizationForNintendoAccount"},
            {290, &IAdministrator::RejectUnavailableNintendoAccount, "GetRequestForNintendoAccountUserResourceView"}, // 8.0.0+
            {300, &IAdministrator::RejectUnavailableNintendoAccount, "TryRecoverNintendoAccountUserStateAsync"}, // 6.0.0+
            {400, &IAdministrator::RejectUnavailableNintendoAccount, "IsServiceEntryRequirementCacheRefreshRequiredForOnlinePlay"}, // 6.1.0+
            {401, &IAdministrator::RejectUnavailableNintendoAccount, "RefreshServiceEntryRequirementCacheForOnlinePlayAsync"}, // 6.1.0+
            {900, &IAdministrator::RejectUnavailableNintendoAccount, "GetAuthenticationInfoForWin"}, // 9.0.0+
            {901, &IAdministrator::RejectUnavailableNintendoAccount, "ImportAsyncForWin"}, // 9.0.0+
            {997, &IAdministrator::RejectDebugOnlyCommand, "DebugUnlinkNintendoAccountAsync"},
            {998, &IAdministrator::RejectDebugOnlyCommand, "DebugSetAvailabilityErrorDetail"},
        };
        // clang-format on

        RegisterHandlers(functions);
    }

private:
    void RejectDebugOnlyCommand(HLERequestContext& ctx) {
        LOG_WARNING(Service_ACC, "Rejected unsupported Nintendo-account debug-only command");
        IPC::ResponseBuilder rb{ctx, 2};
        rb.Push(ResultUnknown);
    }

    void IsLinkedWithNintendoAccount(HLERequestContext& ctx) {
        IPC::ResponseBuilder rb{ctx, 3};
        rb.Push(ResultSuccess);
        rb.Push(false);
    }

    void RejectUnavailableNintendoAccount(HLERequestContext& ctx) {
        LOG_WARNING(Service_ACC, "Rejected IAdministrator Nintendo Account/network command: backend unavailable");
        IPC::ResponseBuilder rb{ctx, 2};
        rb.Push(ResultUnknown);
    }

};

class IAuthorizationRequest final : public ServiceFramework<IAuthorizationRequest> {
public:
    explicit IAuthorizationRequest(Core::System& system_, Common::UUID)
        : ServiceFramework{system_, "IAuthorizationRequest"} {
        // clang-format off
        static const FunctionInfo functions[] = {
            {0, &IAuthorizationRequest::RejectUnavailableNintendoAccount, "GetSessionId"},
            {10, &IAuthorizationRequest::RejectUnavailableNintendoAccount, "InvokeWithoutInteractionAsync"},
            {19, &IAuthorizationRequest::RejectUnavailableNintendoAccount, "IsAuthorized"},
            {20, &IAuthorizationRequest::RejectUnavailableNintendoAccount, "GetAuthorizationCode"},
            {21, &IAuthorizationRequest::RejectUnavailableNintendoAccount, "GetIdToken"},
            {22, &IAuthorizationRequest::RejectUnavailableNintendoAccount, "GetState"},
        };
        // clang-format on

        RegisterHandlers(functions);
    }

    void RejectUnavailableNintendoAccount(HLERequestContext& ctx) {
        LOG_WARNING(Service_ACC, "Rejected IAuthorizationRequest Nintendo Account/network command: backend unavailable");
        IPC::ResponseBuilder rb{ctx, 2};
        rb.Push(ResultUnknown);
    }

};

class IOAuthProcedure final : public ServiceFramework<IOAuthProcedure> {
public:
    explicit IOAuthProcedure(Core::System& system_, Common::UUID)
        : ServiceFramework{system_, "IOAuthProcedure"} {
        // clang-format off
        static const FunctionInfo functions[] = {
            {0, &IOAuthProcedure::RejectUnavailableNintendoAccount, "PrepareAsync"},
            {1, &IOAuthProcedure::RejectUnavailableNintendoAccount, "GetRequest"},
            {2, &IOAuthProcedure::RejectUnavailableNintendoAccount, "ApplyResponse"},
            {3, &IOAuthProcedure::RejectUnavailableNintendoAccount, "ApplyResponseAsync"},
            {10, &IOAuthProcedure::RejectUnavailableNintendoAccount, "Suspend"},
        };
        // clang-format on

        RegisterHandlers(functions);
    }

    void RejectUnavailableNintendoAccount(HLERequestContext& ctx) {
        LOG_WARNING(Service_ACC, "Rejected IOAuthProcedure Nintendo Account/network command: backend unavailable");
        IPC::ResponseBuilder rb{ctx, 2};
        rb.Push(ResultUnknown);
    }

};

// 3.0.0+
class IOAuthProcedureForExternalNsa final : public ServiceFramework<IOAuthProcedureForExternalNsa> {
public:
    explicit IOAuthProcedureForExternalNsa(Core::System& system_, Common::UUID)
        : ServiceFramework{system_, "IOAuthProcedureForExternalNsa"} {
        // clang-format off
        static const FunctionInfo functions[] = {
            {0, &IOAuthProcedureForExternalNsa::RejectUnavailableNintendoAccount, "PrepareAsync"},
            {1, &IOAuthProcedureForExternalNsa::RejectUnavailableNintendoAccount, "GetRequest"},
            {2, &IOAuthProcedureForExternalNsa::RejectUnavailableNintendoAccount, "ApplyResponse"},
            {3, &IOAuthProcedureForExternalNsa::RejectUnavailableNintendoAccount, "ApplyResponseAsync"},
            {10, &IOAuthProcedureForExternalNsa::RejectUnavailableNintendoAccount, "Suspend"},
            {100, &IOAuthProcedureForExternalNsa::RejectUnavailableNintendoAccount, "GetAccountId"},
            {101, &IOAuthProcedureForExternalNsa::RejectUnavailableNintendoAccount, "GetLinkedNintendoAccountId"},
            {102, &IOAuthProcedureForExternalNsa::RejectUnavailableNintendoAccount, "GetNickname"},
            {103, &IOAuthProcedureForExternalNsa::RejectUnavailableNintendoAccount, "GetProfileImage"},
            {104, &IOAuthProcedureForExternalNsa::RejectUnavailableNintendoAccount, "GetProfileLargeImage"} // 18.0.0+
        };
        // clang-format on

        RegisterHandlers(functions);
    }

    void RejectUnavailableNintendoAccount(HLERequestContext& ctx) {
        LOG_WARNING(Service_ACC, "Rejected IOAuthProcedureForExternalNsa Nintendo Account/network command: backend unavailable");
        IPC::ResponseBuilder rb{ctx, 2};
        rb.Push(ResultUnknown);
    }

};

class IOAuthProcedureForNintendoAccountLinkage final
    : public ServiceFramework<IOAuthProcedureForNintendoAccountLinkage> {
public:
    explicit IOAuthProcedureForNintendoAccountLinkage(Core::System& system_, Common::UUID)
        : ServiceFramework{system_, "IOAuthProcedureForNintendoAccountLinkage"} {
        // clang-format off
        static const FunctionInfo functions[] = {
            {0, &IOAuthProcedureForNintendoAccountLinkage::RejectUnavailableNintendoAccount, "PrepareAsync"},
            {1, &IOAuthProcedureForNintendoAccountLinkage::RejectUnavailableNintendoAccount, "GetRequest"},
            {2, &IOAuthProcedureForNintendoAccountLinkage::RejectUnavailableNintendoAccount, "ApplyResponse"},
            {3, &IOAuthProcedureForNintendoAccountLinkage::RejectUnavailableNintendoAccount, "ApplyResponseAsync"},
            {10, &IOAuthProcedureForNintendoAccountLinkage::RejectUnavailableNintendoAccount, "Suspend"},
            {100, &IOAuthProcedureForNintendoAccountLinkage::RejectUnavailableNintendoAccount, "GetRequestWithTheme"},
            {101, &IOAuthProcedureForNintendoAccountLinkage::RejectUnavailableNintendoAccount, "IsNetworkServiceAccountReplaced"},
            {199, &IOAuthProcedureForNintendoAccountLinkage::RejectUnavailableNintendoAccount, "GetUrlForIntroductionOfExtraMembership"}, // 2.0.0 - 5.1.0
            {200, &IOAuthProcedureForNintendoAccountLinkage::RejectUnavailableNintendoAccount, "ApplyAsyncWithAuthorizedToken"},
            {210, &IOAuthProcedureForNintendoAccountLinkage::RejectUnavailableNintendoAccount, "IsProfileAvailable"}, // 17.0.0+
            {220, &IOAuthProcedureForNintendoAccountLinkage::RejectUnavailableNintendoAccount, "RegisterUserAsyncWithoutProfile"}, // 17.0.0+
            {221, &IOAuthProcedureForNintendoAccountLinkage::RejectUnavailableNintendoAccount, "RegisterUserWithProfileAsync"}, // 17.0.0+
            {230, &IOAuthProcedureForNintendoAccountLinkage::RejectUnavailableNintendoAccount, "RegisterUserWithLargeImageProfileAsync"}, // 18.0.0+
        };
        // clang-format on

        RegisterHandlers(functions);
    }

    void RejectUnavailableNintendoAccount(HLERequestContext& ctx) {
        LOG_WARNING(Service_ACC, "Rejected IOAuthProcedureForNintendoAccountLinkage Nintendo Account/network command: backend unavailable");
        IPC::ResponseBuilder rb{ctx, 2};
        rb.Push(ResultUnknown);
    }

};

class INotifier final : public ServiceFramework<INotifier> {
public:
    explicit INotifier(Core::System& system_, Common::UUID)
        : ServiceFramework{system_, "INotifier"} {
        // clang-format off
        static const FunctionInfo functions[] = {
            {0, &INotifier::RejectUnsupportedRemainingAccountCommand, "GetSystemEvent"},
        };
        // clang-format on

        RegisterHandlers(functions);
    }
private:
    void RejectUnsupportedRemainingAccountCommand(HLERequestContext& ctx) {
        // Local profile/user commands already implemented above remain untouched.
        // Remaining entries require Nintendo Account, notification, PIN policy,
        // debug, thumbnail or firmware services not modeled by Encore.
        IPC::ResponseBuilder rb{ctx, 2};
        rb.Push(IPC::ResultNotSupported);
    }

};

class IProfileCommon : public ServiceFramework<IProfileCommon> {
public:
    explicit IProfileCommon(Core::System& system_, const char* name, bool editor_commands,
                            Common::UUID user_id_, ProfileManager& profile_manager_)
        : ServiceFramework{system_, name}, profile_manager{profile_manager_}, user_id{user_id_} {
        // clang-format off
        static const FunctionInfo functions[] = {
            {0, &IProfileCommon::Get, "Get"},
            {1, &IProfileCommon::GetBase, "GetBase"},
            {10, &IProfileCommon::GetImageSize, "GetImageSize"},
            {11, &IProfileCommon::LoadImage, "LoadImage"},
            {20, &IProfileCommon::Unknown20, "Unknown20"},
            {21, &IProfileCommon::Unknown21, "Unknown21"},
            {30, &IProfileCommon::Unknown30, "Unknown30"}
        };
        // clang-format on
        RegisterHandlers(functions);

        if (editor_commands) {
            // clang-format off
            static const FunctionInfo editor_functions[] = {
                {100, &IProfileCommon::Store, "Store"},
                {101, &IProfileCommon::StoreWithImage, "StoreWithImage"},
                {110, &IProfileCommon::Unknown110, "Unknown110"}
            };
            // clang-format on

            RegisterHandlers(editor_functions);
        }
    }

protected:
    void Unknown20(HLERequestContext& ctx) {
        LOG_DEBUG(Service_ACC, "(STUBBED) called.");

        // TODO (jarrodnorwell)
        // inbytes: 0x0, outbytes: 0x4

        IPC::ResponseBuilder rb{ctx, 2};
        rb.Push(ResultSuccess);
    }

    void Unknown21(HLERequestContext& ctx) {
        LOG_DEBUG(Service_ACC, "(STUBBED) called.");

        // TODO (jarrodnorwell)
        // buffers: [0x6], inbytes: 0x0, outbytes: 0x4

        IPC::ResponseBuilder rb{ctx, 2};
        rb.Push(ResultSuccess);
    }

    void Unknown30(HLERequestContext& ctx) {
        LOG_DEBUG(Service_ACC, "(STUBBED) called.");

        // TODO (jarrodnorwell)
        // inbytes: 0x0, outbytes: 0x10

        IPC::ResponseBuilder rb{ctx, 2};
        rb.Push(ResultSuccess);
    }

    void Unknown110(HLERequestContext& ctx) {
        LOG_DEBUG(Service_ACC, "(STUBBED) called.");

        // TODO (jarrodnorwell)
        // buffer_entry_sizes: [0x80, 0x0], buffers: [0x19, 0x5], inbytes: 0x38, outbytes: 0x0

        IPC::ResponseBuilder rb{ctx, 2};
        rb.Push(ResultSuccess);
    }

    void Get(HLERequestContext& ctx) {
        LOG_DEBUG(Service_ACC, "called user_id=0x{}", user_id.RawString());
        ProfileBase profile_base{};
        UserData data{};
        if (profile_manager.GetProfileBaseAndData(user_id, profile_base, data)) {
            ctx.WriteBuffer(data);
            IPC::ResponseBuilder rb{ctx, 16};
            rb.Push(ResultSuccess);
            rb.PushRaw(profile_base);
        } else {
            LOG_ERROR(Service_ACC, "Failed to get profile base and data for user=0x{}",
                      user_id.RawString());
            IPC::ResponseBuilder rb{ctx, 2};
            rb.Push(ResultUnknown); // TODO(ogniK): Get actual error code
        }
    }

    void GetBase(HLERequestContext& ctx) {
        LOG_DEBUG(Service_ACC, "called user_id=0x{}", user_id.RawString());
        ProfileBase profile_base{};
        if (profile_manager.GetProfileBase(user_id, profile_base)) {
            IPC::ResponseBuilder rb{ctx, 16};
            rb.Push(ResultSuccess);
            rb.PushRaw(profile_base);
        } else {
            LOG_ERROR(Service_ACC, "Failed to get profile base for user=0x{}", user_id.RawString());
            IPC::ResponseBuilder rb{ctx, 2};
            rb.Push(ResultUnknown); // TODO(ogniK): Get actual error code
        }
    }

    void LoadImage(HLERequestContext& ctx) {
        LOG_DEBUG(Service_ACC, "called");

        IPC::ResponseBuilder rb{ctx, 3};
        rb.Push(ResultSuccess);

        const Common::FS::IOFile image(GetImagePath(user_id), Common::FS::FileAccessMode::Read,
                                       Common::FS::FileType::BinaryFile);
        if (!image.IsOpen()) {
            LOG_WARNING(Service_ACC,
                        "Failed to load user provided image! Falling back to built-in backup...");
            ctx.WriteBuffer(Core::Constants::ACCOUNT_BACKUP_JPEG);
            rb.Push(static_cast<u32>(Core::Constants::ACCOUNT_BACKUP_JPEG.size()));
            return;
        }

        std::vector<u8> buffer(image.GetSize());

        if (image.Read(buffer) != buffer.size()) {
            LOG_ERROR(Service_ACC, "Failed to read all the bytes in the user provided image.");
        }

        SanitizeJPEGImageSize(buffer);

        ctx.WriteBuffer(buffer);
        rb.Push(static_cast<u32>(buffer.size()));
    }

    void GetImageSize(HLERequestContext& ctx) {
        LOG_DEBUG(Service_ACC, "called");
        IPC::ResponseBuilder rb{ctx, 3};
        rb.Push(ResultSuccess);

        const Common::FS::IOFile image(GetImagePath(user_id), Common::FS::FileAccessMode::Read,
                                       Common::FS::FileType::BinaryFile);

        if (!image.IsOpen()) {
            LOG_WARNING(Service_ACC,
                        "Failed to load user provided image! Falling back to built-in backup...");
            rb.Push(static_cast<u32>(Core::Constants::ACCOUNT_BACKUP_JPEG.size()));
            return;
        }

        std::vector<u8> buffer(image.GetSize());

        if (image.Read(buffer) != buffer.size()) {
            LOG_ERROR(Service_ACC, "Failed to read all the bytes in the user provided image.");
        }

        SanitizeJPEGImageSize(buffer);
        rb.Push(static_cast<u32>(buffer.size()));
    }

    void LoadIdTokenCache(HLERequestContext& ctx) {
        LOG_WARNING(Service_ACC, "(STUBBED) called");

        std::vector<u8> token_data(0x100);
        std::fill(token_data.begin(), token_data.end(), u8(0));

        (void)ctx.WriteBuffer(token_data);

        IPC::ResponseBuilder rb{ctx, 3};
        rb.Push(ResultSuccess);
        rb.Push(static_cast<u32>(token_data.size()));
    }

    void GetNintendoAccountUserResourceCacheForApplication(HLERequestContext& ctx) {
        LOG_WARNING(Service_ACC, "(STUBBED) called");

        std::vector<u8> nas_user_base_for_application(0x68);
        (void)ctx.WriteBuffer(nas_user_base_for_application);

        if (ctx.CanWriteBuffer(1)) {
            std::vector<u8> unknown_out_buffer(ctx.GetWriteBufferSize(1));
            (void)ctx.WriteBuffer(unknown_out_buffer, 1);
        }

        IPC::ResponseBuilder rb{ctx, 4};
        rb.Push(ResultSuccess);
        rb.PushRaw<u64>(profile_manager.GetLastOpenedUser().Hash());
    }

    void Store(HLERequestContext& ctx) {
        IPC::RequestParser rp{ctx};
        const auto base = rp.PopRaw<ProfileBase>();

        const auto user_data = ctx.ReadBuffer();

        LOG_DEBUG(Service_ACC, "called, username='{}', timestamp={:016X}, uuid=0x{}",
                  Common::StringFromFixedZeroTerminatedBuffer(
                      reinterpret_cast<const char*>(base.username.data()), base.username.size()),
                  base.timestamp, base.user_uuid.RawString());

        if (user_data.size() < sizeof(UserData)) {
            LOG_ERROR(Service_ACC, "UserData buffer too small!");
            IPC::ResponseBuilder rb{ctx, 2};
            rb.Push(Account::ResultInvalidArrayLength);
            return;
        }

        UserData data;
        std::memcpy(&data, user_data.data(), sizeof(UserData));

        if (!profile_manager.SetProfileBaseAndData(user_id, base, data)) {
            LOG_ERROR(Service_ACC, "Failed to update user data and base!");
            IPC::ResponseBuilder rb{ctx, 2};
            rb.Push(Account::ResultAccountUpdateFailed);
            return;
        }

        IPC::ResponseBuilder rb{ctx, 2};
        rb.Push(ResultSuccess);
    }

    void StoreWithImage(HLERequestContext& ctx) {
        IPC::RequestParser rp{ctx};
        const auto base = rp.PopRaw<ProfileBase>();

        const auto image_data = ctx.ReadBufferA(0);
        const auto user_data = ctx.ReadBufferX(0);

        LOG_INFO(Service_ACC, "called, username='{}', timestamp={:016X}, uuid=0x{}",
                 Common::StringFromFixedZeroTerminatedBuffer(
                     reinterpret_cast<const char*>(base.username.data()), base.username.size()),
                 base.timestamp, base.user_uuid.RawString());

        if (user_data.size() < sizeof(UserData)) {
            LOG_ERROR(Service_ACC, "UserData buffer too small!");
            IPC::ResponseBuilder rb{ctx, 2};
            rb.Push(Account::ResultInvalidArrayLength);
            return;
        }

        UserData data;
        std::memcpy(&data, user_data.data(), sizeof(UserData));

        Common::FS::IOFile image(GetImagePath(user_id), Common::FS::FileAccessMode::Write,
                                 Common::FS::FileType::BinaryFile);

        if (!image.IsOpen() || !image.SetSize(image_data.size()) ||
            image.Write(image_data) != image_data.size() ||
            !profile_manager.SetProfileBaseAndData(user_id, base, data)) {
            LOG_ERROR(Service_ACC, "Failed to update profile data, base, and image!");
            IPC::ResponseBuilder rb{ctx, 2};
            rb.Push(Account::ResultAccountUpdateFailed);
            return;
        }

        IPC::ResponseBuilder rb{ctx, 2};
        rb.Push(ResultSuccess);
    }

    ProfileManager& profile_manager;
    Common::UUID user_id{}; ///< The user id this profile refers to.
};

class IProfile final : public IProfileCommon {
public:
    explicit IProfile(Core::System& system_, Common::UUID user_id_,
                      ProfileManager& profile_manager_)
        : IProfileCommon{system_, "IProfile", false, user_id_, profile_manager_} {}
};

class IProfileEditor final : public IProfileCommon {
public:
    explicit IProfileEditor(Core::System& system_, Common::UUID user_id_,
                            ProfileManager& profile_manager_)
        : IProfileCommon{system_, "IProfileEditor", true, user_id_, profile_manager_} {}
};

class ISessionObject final : public ServiceFramework<ISessionObject> {
public:
    explicit ISessionObject(Core::System& system_, Common::UUID)
        : ServiceFramework{system_, "ISessionObject"} {
        // clang-format off
        static const FunctionInfo functions[] = {
            {999, &ISessionObject::AcceptDummy, "Dummy"},
        };
        // clang-format on

        RegisterHandlers(functions);
    }
private:
    void AcceptDummy(HLERequestContext& ctx) {
        IPC::ResponseBuilder rb{ctx, 2};
        rb.Push(ResultSuccess);
    }

};

class IGuestLoginRequest final : public ServiceFramework<IGuestLoginRequest> {
public:
    explicit IGuestLoginRequest(Core::System& system_, Common::UUID)
        : ServiceFramework{system_, "IGuestLoginRequest"} {
        // clang-format off
        static const FunctionInfo functions[] = {
            {0, &IGuestLoginRequest::RejectUnavailableNintendoAccount, "GetSessionId"},
            {11, &IGuestLoginRequest::RejectUnavailableNintendoAccount, "Unknown"}, // 1.0.0 - 2.3.0 (the name is blank on Switchbrew)
            {12, &IGuestLoginRequest::RejectUnavailableNintendoAccount, "GetAccountId"},
            {13, &IGuestLoginRequest::RejectUnavailableNintendoAccount, "GetLinkedNintendoAccountId"},
            {14, &IGuestLoginRequest::RejectUnavailableNintendoAccount, "GetNickname"},
            {15, &IGuestLoginRequest::RejectUnavailableNintendoAccount, "GetProfileImage"},
            {16, &IGuestLoginRequest::RejectUnavailableNintendoAccount, "GetProfileLargeImage"}, // 18.0.0+
            {21, &IGuestLoginRequest::RejectUnavailableNintendoAccount, "LoadIdTokenCache"}, // 3.0.0+
        };
        // clang-format on

        RegisterHandlers(functions);
    }

    void RejectUnavailableNintendoAccount(HLERequestContext& ctx) {
        LOG_WARNING(Service_ACC, "Rejected IGuestLoginRequest Nintendo Account/network command: backend unavailable");
        IPC::ResponseBuilder rb{ctx, 2};
        rb.Push(ResultUnknown);
    }

};

class EnsureTokenIdCacheAsyncInterface final : public IAsyncContext {
public:
    explicit EnsureTokenIdCacheAsyncInterface(Core::System& system_) : IAsyncContext{system_} {
        MarkComplete();
    }
    ~EnsureTokenIdCacheAsyncInterface() = default;

    void LoadIdTokenCache(HLERequestContext& ctx) {
        LOG_WARNING(Service_ACC, "(STUBBED) called");

        IPC::ResponseBuilder rb{ctx, 3};
        rb.Push(ResultSuccess);
        rb.Push(0);
    }

protected:
    bool IsComplete() const override {
        return true;
    }

    void Cancel() override {}

    Result GetResult() const override {
        return ResultSuccess;
    }
};

class IManagerForApplication final : public ServiceFramework<IManagerForApplication> {
public:
    explicit IManagerForApplication(Core::System& system_,
                                    const std::shared_ptr<ProfileManager>& profile_manager_)
        : ServiceFramework{system_, "IManagerForApplication"},
          ensure_token_id{std::make_shared<EnsureTokenIdCacheAsyncInterface>(system)},
          profile_manager{profile_manager_} {
        // clang-format off
        static const FunctionInfo functions[] = {
            {0, &IManagerForApplication::CheckAvailability, "CheckAvailability"},
            {1, &IManagerForApplication::GetAccountId, "GetAccountId"},
            {2, &IManagerForApplication::EnsureIdTokenCacheAsync, "EnsureIdTokenCacheAsync"},
            {3, &IManagerForApplication::LoadIdTokenCacheDeprecated, "LoadIdTokenCacheDeprecated"},
            {4, &IManagerForApplication::LoadIdTokenCache, "LoadIdTokenCache"},
            {130, &IManagerForApplication::GetNintendoAccountUserResourceCacheForApplication, "GetNintendoAccountUserResourceCacheForApplication"},
            {136, &IManagerForApplication::GetNintendoAccountUserResourceCacheForApplication, "GetNintendoAccountUserResourceCache"}, // 19.0.0+
            {150, &IManagerForApplication::RejectUnsupportedRemainingAccountCommand, "CreateAuthorizationRequest"},
            {160, &IManagerForApplication::StoreOpenContext, "StoreOpenContext"},
            {170, &IManagerForApplication::RejectUnsupportedRemainingAccountCommand, "LoadNetworkServiceLicenseKindAsync"},
        };
        // clang-format on

        RegisterHandlers(functions);
    }

private:
    void CheckAvailability(HLERequestContext& ctx) {
        LOG_DEBUG(Service_ACC, "(STUBBED) called");
        IPC::ResponseBuilder rb{ctx, 2};
        rb.Push(ResultSuccess);
    }

    void GetAccountId(HLERequestContext& ctx) {
        LOG_DEBUG(Service_ACC, "called");

        IPC::ResponseBuilder rb{ctx, 4};
        rb.Push(ResultSuccess);
        rb.PushRaw<u64>(profile_manager->GetLastOpenedUser().Hash());
    }

    void EnsureIdTokenCacheAsync(HLERequestContext& ctx) {
        LOG_WARNING(Service_ACC, "(STUBBED) called");

        IPC::ResponseBuilder rb{ctx, 2, 0, 1};
        rb.Push(ResultSuccess);
        rb.PushIpcInterface(ctx, ensure_token_id);
    }

    void LoadIdTokenCacheDeprecated(HLERequestContext& ctx) {
        LOG_WARNING(Service_ACC, "(STUBBED) called");

        ensure_token_id->LoadIdTokenCache(ctx);
    }

    void LoadIdTokenCache(HLERequestContext& ctx) {
        LOG_WARNING(Service_ACC, "(STUBBED) called");

        std::vector<u8> token_data(0x100);
        std::fill(token_data.begin(), token_data.end(), u8(0));

        ctx.WriteBuffer(token_data);

        IPC::ResponseBuilder rb{ctx, 3};
        rb.Push(ResultSuccess);
        rb.Push(static_cast<u32>(token_data.size()));
    }

    void GetNintendoAccountUserResourceCacheForApplication(HLERequestContext& ctx) {
        LOG_WARNING(Service_ACC, "(STUBBED) called");

        std::vector<u8> nas_user_base_for_application(0x68);
        ctx.WriteBuffer(nas_user_base_for_application);

        if (ctx.CanWriteBuffer(1)) {
            std::vector<u8> unknown_out_buffer(ctx.GetWriteBufferSize(1));
            ctx.WriteBuffer(unknown_out_buffer, 1);
        }

        IPC::ResponseBuilder rb{ctx, 4};
        rb.Push(ResultSuccess);
        rb.PushRaw<u64>(profile_manager->GetLastOpenedUser().Hash());
    }

    void StoreOpenContext(HLERequestContext& ctx) {
        LOG_DEBUG(Service_ACC, "called");

        profile_manager->StoreOpenedUsers();

        IPC::ResponseBuilder rb{ctx, 2};
        rb.Push(ResultSuccess);
    }

    std::shared_ptr<EnsureTokenIdCacheAsyncInterface> ensure_token_id{};
    std::shared_ptr<ProfileManager> profile_manager;
private:
    void RejectUnsupportedRemainingAccountCommand(HLERequestContext& ctx) {
        // Local profile/user commands already implemented above remain untouched.
        // Remaining entries require Nintendo Account, notification, PIN policy,
        // debug, thumbnail or firmware services not modeled by Encore.
        IPC::ResponseBuilder rb{ctx, 2};
        rb.Push(IPC::ResultNotSupported);
    }

};

// 6.0.0+
class IAsyncNetworkServiceLicenseKindContext final
    : public ServiceFramework<IAsyncNetworkServiceLicenseKindContext> {
public:
    explicit IAsyncNetworkServiceLicenseKindContext(Core::System& system_, Common::UUID)
        : ServiceFramework{system_, "IAsyncNetworkServiceLicenseKindContext"} {
        // clang-format off
        static const FunctionInfo functions[] = {
            {0, &IAsyncNetworkServiceLicenseKindContext::RejectUnavailableNintendoAccount, "GetSystemEvent"},
            {1, &IAsyncNetworkServiceLicenseKindContext::RejectUnavailableNintendoAccount, "Cancel"},
            {2, &IAsyncNetworkServiceLicenseKindContext::RejectUnavailableNintendoAccount, "HasDone"},
            {3, &IAsyncNetworkServiceLicenseKindContext::RejectUnavailableNintendoAccount, "GetResult"},
            {4, &IAsyncNetworkServiceLicenseKindContext::RejectUnavailableNintendoAccount, "GetNetworkServiceLicenseKind"},
        };
        // clang-format on

        RegisterHandlers(functions);
    }

    void RejectUnavailableNintendoAccount(HLERequestContext& ctx) {
        LOG_WARNING(Service_ACC, "Rejected IAsyncNetworkServiceLicenseKindContext Nintendo Account/network command: backend unavailable");
        IPC::ResponseBuilder rb{ctx, 2};
        rb.Push(ResultUnknown);
    }

};

// 8.0.0+
class IOAuthProcedureForUserRegistration final
    : public ServiceFramework<IOAuthProcedureForUserRegistration> {
public:
    explicit IOAuthProcedureForUserRegistration(Core::System& system_, Common::UUID)
        : ServiceFramework{system_, "IOAuthProcedureForUserRegistration"} {
        // clang-format off
        static const FunctionInfo functions[] = {
            {0, &IOAuthProcedureForUserRegistration::RejectUnavailableNintendoAccount, "PrepareAsync"},
            {1, &IOAuthProcedureForUserRegistration::RejectUnavailableNintendoAccount, "GetRequest"},
            {2, &IOAuthProcedureForUserRegistration::RejectUnavailableNintendoAccount, "ApplyResponse"},
            {3, &IOAuthProcedureForUserRegistration::RejectUnavailableNintendoAccount, "ApplyResponseAsync"},
            {10, &IOAuthProcedureForUserRegistration::RejectUnavailableNintendoAccount, "Suspend"},
            {100, &IOAuthProcedureForUserRegistration::RejectUnavailableNintendoAccount, "GetAccountId"},
            {101, &IOAuthProcedureForUserRegistration::RejectUnavailableNintendoAccount, "GetLinkedNintendoAccountId"},
            {102, &IOAuthProcedureForUserRegistration::RejectUnavailableNintendoAccount, "GetNickname"},
            {103, &IOAuthProcedureForUserRegistration::RejectUnavailableNintendoAccount, "GetProfileImage"},
            {104, &IOAuthProcedureForUserRegistration::RejectUnavailableNintendoAccount, "GetProfileLargeImage"}, // 18.0.0+
            {110, &IOAuthProcedureForUserRegistration::RejectUnavailableNintendoAccount, "RegisterUserAsync"},
            {111, &IOAuthProcedureForUserRegistration::RejectUnavailableNintendoAccount, "GetUid"},
            {200, &IOAuthProcedureForUserRegistration::RejectUnavailableNintendoAccount, "ApplyResponseForUserCreationAsync"}, // 17.0.0+
            {205, &IOAuthProcedureForUserRegistration::RejectUnavailableNintendoAccount, "SuspendAfterApplyResponse"}, // 17.0.0+
            {210, &IOAuthProcedureForUserRegistration::RejectUnavailableNintendoAccount, "IsProfileAvailable"}, // 17.0.0+
            {220, &IOAuthProcedureForUserRegistration::RejectUnavailableNintendoAccount, "RegisterUserAsyncWithoutProfile"}, // 17.0.0+
            {221, &IOAuthProcedureForUserRegistration::RejectUnavailableNintendoAccount, "RegisterUserWithProfileAsync"}, // 17.0.0+
            {230, &IOAuthProcedureForUserRegistration::RejectUnavailableNintendoAccount, "RegisterUserWithLargeImageProfileAsync"} // 18.0.0+
        };
        // clang-format on

        RegisterHandlers(functions);
    }

    void RejectUnavailableNintendoAccount(HLERequestContext& ctx) {
        LOG_WARNING(Service_ACC, "Rejected IOAuthProcedureForUserRegistration Nintendo Account/network command: backend unavailable");
        IPC::ResponseBuilder rb{ctx, 2};
        rb.Push(ResultUnknown);
    }

};

class DAUTH_O final : public ServiceFramework<DAUTH_O> {
public:
    explicit DAUTH_O(Core::System& system_, Common::UUID) : ServiceFramework{system_, "dauth:o"} {
        // clang-format off
        static const FunctionInfo functions[] = {
            {0, &DAUTH_O::RejectUnavailableNintendoAccount, "EnsureAuthenticationTokenCacheAsync"},
            {1, &DAUTH_O::RejectUnavailableNintendoAccount, "LoadAuthenticationTokenCache"},
            {2, &DAUTH_O::RejectUnavailableNintendoAccount, "InvalidateAuthenticationTokenCache"},
            {3, &DAUTH_O::RejectUnavailableNintendoAccount, "IsDeviceAuthenticationTokenCacheAvailable"},
            {10, &DAUTH_O::RejectUnavailableNintendoAccount, "EnsureEdgeTokenCacheAsync"},
            {11, &DAUTH_O::RejectUnavailableNintendoAccount, "LoadEdgeTokenCache"},
            {12, &DAUTH_O::RejectUnavailableNintendoAccount, "InvalidateEdgeTokenCache"},
            {13, &DAUTH_O::RejectUnavailableNintendoAccount, "IsEdgeTokenCacheAvailable"},
            {20, &DAUTH_O::RejectUnavailableNintendoAccount, "EnsureApplicationAuthenticationCacheAsync"},
            {21, &DAUTH_O::RejectUnavailableNintendoAccount, "LoadApplicationAuthenticationTokenCache"},
            {22, &DAUTH_O::RejectUnavailableNintendoAccount, "LoadApplicationNetworkServiceClientConfigCache"},
            {23, &DAUTH_O::RejectUnavailableNintendoAccount, "IsApplicationAuthenticationCacheAvailable"},
            {24, &DAUTH_O::RejectUnavailableNintendoAccount, "InvalidateApplicationAuthenticationCache"},
        };
        // clang-format on

        RegisterHandlers(functions);
    }

    void RejectUnavailableNintendoAccount(HLERequestContext& ctx) {
        LOG_WARNING(Service_ACC, "Rejected DAUTH_O Nintendo Account/network command: backend unavailable");
        IPC::ResponseBuilder rb{ctx, 2};
        rb.Push(ResultUnknown);
    }

};

// 6.0.0+
class IAsyncResult final : public ServiceFramework<IAsyncResult> {
public:
    explicit IAsyncResult(Core::System& system_, Common::UUID)
        : ServiceFramework{system_, "IAsyncResult"} {
        // clang-format off
        static const FunctionInfo functions[] = {
            {0, &IAsyncResult::RejectUnavailableNintendoAccount, "GetResult"},
            {1, &IAsyncResult::RejectUnavailableNintendoAccount, "Cancel"},
            {2, &IAsyncResult::RejectUnavailableNintendoAccount, "IsAvailable"},
            {3, &IAsyncResult::RejectUnavailableNintendoAccount, "GetSystemEvent"},
        };
        // clang-format on

        RegisterHandlers(functions);
    }

    void RejectUnavailableNintendoAccount(HLERequestContext& ctx) {
        LOG_WARNING(Service_ACC, "Rejected IAsyncResult Nintendo Account/network command: backend unavailable");
        IPC::ResponseBuilder rb{ctx, 2};
        rb.Push(ResultUnknown);
    }

};

void Module::Interface::GetUserCount(HLERequestContext& ctx) {
    LOG_DEBUG(Service_ACC, "called");
    IPC::ResponseBuilder rb{ctx, 3};
    rb.Push(ResultSuccess);
    rb.Push<u32>(static_cast<u32>(profile_manager->GetUserCount()));
}

void Module::Interface::GetUserExistence(HLERequestContext& ctx) {
    IPC::RequestParser rp{ctx};
    Common::UUID user_id = rp.PopRaw<Common::UUID>();
    LOG_DEBUG(Service_ACC, "called user_id=0x{}", user_id.RawString());

    IPC::ResponseBuilder rb{ctx, 3};
    rb.Push(ResultSuccess);
    rb.Push(profile_manager->UserExists(user_id));
}

void Module::Interface::ListAllUsers(HLERequestContext& ctx) {
    LOG_DEBUG(Service_ACC, "called");
    ctx.WriteBuffer(profile_manager->GetAllUsers());
    IPC::ResponseBuilder rb{ctx, 2};
    rb.Push(ResultSuccess);
}

void Module::Interface::ListOpenUsers(HLERequestContext& ctx) {
    LOG_DEBUG(Service_ACC, "called");
    ctx.WriteBuffer(profile_manager->GetOpenUsers());
    IPC::ResponseBuilder rb{ctx, 2};
    rb.Push(ResultSuccess);
}

void Module::Interface::GetLastOpenedUser(HLERequestContext& ctx) {
    LOG_DEBUG(Service_ACC, "called");
    IPC::ResponseBuilder rb{ctx, 6};
    rb.Push(ResultSuccess);
    rb.PushRaw<Common::UUID>(profile_manager->GetLastOpenedUser());
}

void Module::Interface::GetProfile(HLERequestContext& ctx) {
    IPC::RequestParser rp{ctx};
    Common::UUID user_id = rp.PopRaw<Common::UUID>();
    LOG_DEBUG(Service_ACC, "called user_id=0x{}", user_id.RawString());

    IPC::ResponseBuilder rb{ctx, 2, 0, 1};
    rb.Push(ResultSuccess);
    rb.PushIpcInterface<IProfile>(ctx, system, user_id, *profile_manager);
}

void Module::Interface::IsUserRegistrationRequestPermitted(HLERequestContext& ctx) {
    LOG_WARNING(Service_ACC, "(STUBBED) called");
    IPC::ResponseBuilder rb{ctx, 3};
    rb.Push(ResultSuccess);
    rb.Push(profile_manager->CanSystemRegisterUser());
}

void Module::Interface::InitializeApplicationInfo(HLERequestContext& ctx) {
    LOG_DEBUG(Service_ACC, "called");
    IPC::ResponseBuilder rb{ctx, 2};
    rb.Push(InitializeApplicationInfoBase());
}

void Module::Interface::InitializeApplicationInfoRestricted(HLERequestContext& ctx) {
    LOG_WARNING(Service_ACC, "(Partial implementation) called");

    // TODO(ogniK): We require checking if the user actually owns the title and what not. As of
    // currently, we assume the user owns the title. InitializeApplicationInfoBase SHOULD be called
    // first then we do extra checks if the game is a digital copy.

    IPC::ResponseBuilder rb{ctx, 2};
    rb.Push(InitializeApplicationInfoBase());
}

Result Module::Interface::InitializeApplicationInfoBase() {
    if (application_info) {
        LOG_ERROR(Service_ACC, "Application already initialized");
        return Account::ResultApplicationInfoAlreadyInitialized;
    }

    // TODO(ogniK): This should be changed to reflect the target process for when we have multiple
    // processes emulated. As we don't actually have pid support we should assume we're just using
    // our own process
    Glue::ApplicationLaunchProperty launch_property{};
    const auto result = system.GetARPManager().GetLaunchProperty(
        &launch_property, system.GetApplicationProcessProgramID());

    if (result != ResultSuccess) {
        LOG_ERROR(Service_ACC, "Failed to get launch property");
        return Account::ResultInvalidApplication;
    }

    switch (launch_property.base_game_storage_id) {
    case FileSys::StorageId::GameCard:
        application_info.application_type = ApplicationType::GameCard;
        break;
    case FileSys::StorageId::Host:
    case FileSys::StorageId::NandSystem:
    case FileSys::StorageId::NandUser:
    case FileSys::StorageId::SdCard:
    case FileSys::StorageId::None: // Yuzu specific, differs from hardware
        application_info.application_type = ApplicationType::Digital;
        break;
    default:
        LOG_ERROR(Service_ACC, "Invalid game storage ID! storage_id={}",
                  launch_property.base_game_storage_id);
        return Account::ResultInvalidApplication;
    }

    LOG_WARNING(Service_ACC, "ApplicationInfo init required");
    // TODO(ogniK): Actual initialization here

    return ResultSuccess;
}

void Module::Interface::GetBaasAccountManagerForApplication(HLERequestContext& ctx) {
    LOG_DEBUG(Service_ACC, "called");
    IPC::ResponseBuilder rb{ctx, 2, 0, 1};
    rb.Push(ResultSuccess);
    rb.PushIpcInterface<IManagerForApplication>(ctx, system, profile_manager);
}

void Module::Interface::IsUserAccountSwitchLocked(HLERequestContext& ctx) {
    LOG_DEBUG(Service_ACC, "called");
    FileSys::NACP nacp;
    const auto res = system.GetAppLoader().ReadControlData(nacp);

    bool is_locked = false;

    if (res != Loader::ResultStatus::Success) {
        const FileSys::PatchManager pm{system.GetApplicationProcessProgramID(),
                                       system.GetFileSystemController(),
                                       system.GetContentProvider()};
        const auto nacp_unique = pm.GetControlMetadata().first;

        if (nacp_unique != nullptr) {
            is_locked = nacp_unique->GetUserAccountSwitchLock();
        } else {
            LOG_ERROR(Service_ACC, "nacp_unique is null!");
        }
    } else {
        is_locked = nacp.GetUserAccountSwitchLock();
    }

    IPC::ResponseBuilder rb{ctx, 3};
    rb.Push(ResultSuccess);
    rb.Push(is_locked);
}

void Module::Interface::InitializeApplicationInfoV2(HLERequestContext& ctx) {
    LOG_WARNING(Service_ACC, "(STUBBED) called");

    IPC::ResponseBuilder rb{ctx, 2};
    rb.Push(ResultSuccess);
}

void Module::Interface::BeginUserRegistration(HLERequestContext& ctx) {
    const auto user_id = Common::UUID::MakeRandom();
    profile_manager->CreateNewUser(user_id, "Eden");
    pending_registration_user = user_id;

    LOG_INFO(Service_ACC, "called, uuid={}", user_id.FormattedString());

    IPC::ResponseBuilder rb{ctx, 6};
    rb.Push(ResultSuccess);
    rb.PushRaw(user_id);
}

void Module::Interface::CompleteUserRegistration(HLERequestContext& ctx) {
    IPC::RequestParser rp{ctx};
    Common::UUID user_id = rp.PopRaw<Common::UUID>();

    LOG_INFO(Service_ACC, "called, uuid={}", user_id.FormattedString());

    profile_manager->WriteUserSaveFile();
    if (pending_registration_user && *pending_registration_user == user_id) {
        pending_registration_user.reset();
    }

    IPC::ResponseBuilder rb{ctx, 2};
    rb.Push(ResultSuccess);
}

void Module::Interface::CancelUserRegistration(HLERequestContext& ctx) {
    IPC::RequestParser rp{ctx};
    const Common::UUID user_id = rp.PopRaw<Common::UUID>();

    LOG_INFO(Service_ACC, "called, uuid={}", user_id.FormattedString());

    // BeginUserRegistration creates the candidate profile in memory before it is persisted.
    // Only remove that exact pending profile: an arbitrary UUID must never delete a committed user.
    if (pending_registration_user && *pending_registration_user == user_id) {
        profile_manager->RemoveUser(user_id);
        pending_registration_user.reset();
    }

    IPC::ResponseBuilder rb{ctx, 2};
    rb.Push(ResultSuccess);
}

void Module::Interface::CompleteUserRegistrationForcibly(HLERequestContext& ctx) {
    // Encore does not model the policy checks bypassed by the "forcibly" variant, so completion is
    // identical to the normal path while preserving the documented 0x10 UUID ABI.
    CompleteUserRegistration(ctx);
}

void Module::Interface::DeleteUser(HLERequestContext& ctx) {
    IPC::RequestParser rp{ctx};
    Common::UUID user_id = rp.PopRaw<Common::UUID>();
    LOG_INFO(Service_ACC, "called, uuid={}", user_id.FormattedString());
    if (!profile_manager->RemoveUser(user_id)) {
        LOG_ERROR(Service_ACC, "Failed to delete user with uuid={}", user_id.RawString());
        IPC::ResponseBuilder rb{ctx, 2};
        rb.Push(1U);
        return;
    }
    IPC::ResponseBuilder rb{ctx, 2};
    rb.Push(ResultSuccess);
}

void Module::Interface::SetUserPosition(HLERequestContext& ctx) {
    IPC::RequestParser rp{ctx};

    u64 position = rp.Pop<u64>();
    Common::UUID user_id = rp.PopRaw<Common::UUID>();

    LOG_DEBUG(Service_ACC, "called, position={} user_id={}", position, user_id.FormattedString());

    profile_manager->SetUserPosition(position, user_id);

    IPC::ResponseBuilder rb{ctx, 2};
    rb.Push(ResultSuccess);
}

void Module::Interface::GetProfileEditor(HLERequestContext& ctx) {
    IPC::RequestParser rp{ctx};
    Common::UUID user_id = rp.PopRaw<Common::UUID>();

    LOG_DEBUG(Service_ACC, "called, user_id=0x{}", user_id.RawString());

    IPC::ResponseBuilder rb{ctx, 2, 0, 1};
    rb.Push(ResultSuccess);
    rb.PushIpcInterface<IProfileEditor>(ctx, system, user_id, *profile_manager);
}

void Module::Interface::GetBaasAccountAdministrator(HLERequestContext &ctx) {
    IPC::RequestParser rp{ctx};
    const auto uuid = rp.PopRaw<Common::UUID>();

    LOG_INFO(Service_ACC, "called, uuid=0x{}", uuid.RawString());

    IPC::ResponseBuilder rb{ctx, 2, 0, 1};
    rb.Push(ResultSuccess);
    rb.PushIpcInterface<IAdministrator>(ctx, system, uuid);
}

void Module::Interface::ListQualifiedUsers(HLERequestContext& ctx) {
    LOG_DEBUG(Service_ACC, "called");

    // All users should be qualified. We don't actually have parental control or anything to do with
    // nintendo online currently. We're just going to assume the user running the game has access to
    // the game regardless of parental control settings.
    ctx.WriteBuffer(profile_manager->GetAllUsers());
    IPC::ResponseBuilder rb{ctx, 2};
    rb.Push(ResultSuccess);
}

void Module::Interface::ListOpenContextStoredUsers(HLERequestContext& ctx) {
    LOG_DEBUG(Service_ACC, "called");

    ctx.WriteBuffer(profile_manager->GetStoredOpenedUsers());
    IPC::ResponseBuilder rb{ctx, 2};
    rb.Push(ResultSuccess);
}

// Restore only existing profiles from the StoreOpenContext snapshot. Do not invent
// a new user, select one arbitrarily, or lose currently opened profiles on resume.
void Module::Interface::LoadOpenContext(HLERequestContext& ctx) {
    const auto stored = profile_manager->GetStoredOpenedUsers();
    for (const auto& user_id : stored) {
        if (user_id.IsValid() && profile_manager->UserExists(user_id)) {
            profile_manager->OpenUser(user_id);
        }
    }
    LOG_DEBUG(Service_ACC, "LoadOpenContext restored stored account state");
    IPC::ResponseBuilder rb{ctx, 2};
    rb.Push(ResultSuccess);
}

void Module::Interface::StoreSaveDataThumbnailApplication(HLERequestContext& ctx) {
    IPC::RequestParser rp{ctx};
    const auto uuid = rp.PopRaw<Common::UUID>();

    LOG_WARNING(Service_ACC, "(STUBBED) called, uuid=0x{}", uuid.RawString());

    // TODO(ogniK): Check if application ID is zero on acc initialize. As we don't have a reliable
    // way of confirming things like the TID, we're going to assume a non zero value for the time
    // being.
    constexpr u64 tid{1};
    StoreSaveDataThumbnail(ctx, uuid, tid);
}

void Module::Interface::GetBaasAccountManagerForSystemService(HLERequestContext& ctx) {
    IPC::RequestParser rp{ctx};
    const auto uuid = rp.PopRaw<Common::UUID>();

    LOG_INFO(Service_ACC, "called, uuid=0x{}", uuid.RawString());

    IPC::ResponseBuilder rb{ctx, 2, 0, 1};
    rb.Push(ResultSuccess);
    rb.PushIpcInterface<IManagerForSystemService>(ctx, system, uuid);
}

void Module::Interface::StoreSaveDataThumbnailSystem(HLERequestContext& ctx) {
    IPC::RequestParser rp{ctx};
    const auto uuid = rp.PopRaw<Common::UUID>();
    const auto tid = rp.Pop<u64_le>();

    LOG_WARNING(Service_ACC, "(STUBBED) called, uuid=0x{}, tid={:016X}", uuid.RawString(), tid);
    StoreSaveDataThumbnail(ctx, uuid, tid);
}

void Module::Interface::GetPinCodeLength(HLERequestContext& ctx) {
    LOG_WARNING(Service_ACC, "(STUBBED) called");

    IPC::ResponseBuilder rb{ctx, 3};
    rb.Push(ResultSuccess);
    rb.Push<u32>(0);
}

void Module::Interface::StoreSaveDataThumbnail(HLERequestContext& ctx, const Common::UUID& uuid,
                                               const u64 tid) {
    IPC::ResponseBuilder rb{ctx, 2};

    if (tid == 0) {
        LOG_ERROR(Service_ACC, "TitleID is not valid!");
        rb.Push(Account::ResultInvalidApplication);
        return;
    }

    if (uuid.IsInvalid() || !profile_manager->UserExists(uuid)) {
        LOG_ERROR(Service_ACC, "User ID is not valid!");
        rb.Push(Account::ResultInvalidUserId);
        return;
    }

    const auto thumbnail_data = ctx.ReadBuffer();
    if (thumbnail_data.size() != THUMBNAIL_SIZE) {
        LOG_ERROR(Service_ACC, "Thumbnail buffer size={:X}, expected {:X}", thumbnail_data.size(),
                  THUMBNAIL_SIZE);
        rb.Push(Account::ResultInvalidArrayLength);
        return;
    }

    const auto path = GetSaveDataThumbnailPath(uuid, tid);
    if (!Common::FS::CreateDirs(path.parent_path())) {
        LOG_ERROR(Service_ACC, "Failed to create save-data thumbnail directory");
        rb.Push(Account::ResultAccountUpdateFailed);
        return;
    }

    Common::FS::IOFile thumbnail(path, Common::FS::FileAccessMode::Write,
                                 Common::FS::FileType::BinaryFile);
    if (!thumbnail.IsOpen() || !thumbnail.SetSize(thumbnail_data.size()) ||
        thumbnail.Write(thumbnail_data) != thumbnail_data.size()) {
        LOG_ERROR(Service_ACC, "Failed to persist save-data thumbnail");
        rb.Push(Account::ResultAccountUpdateFailed);
        return;
    }

    rb.Push(ResultSuccess);
}

void Module::Interface::ClearSaveDataThumbnail(HLERequestContext& ctx) {
    IPC::RequestParser rp{ctx};
    const auto uuid = rp.PopRaw<Common::UUID>();
    const auto tid = rp.Pop<u64_le>();

    IPC::ResponseBuilder rb{ctx, 2};
    if (tid == 0) {
        rb.Push(Account::ResultInvalidApplication);
        return;
    }
    if (uuid.IsInvalid() || !profile_manager->UserExists(uuid)) {
        rb.Push(Account::ResultInvalidUserId);
        return;
    }

    const auto path = GetSaveDataThumbnailPath(uuid, tid);
    if (Common::FS::Exists(path) && !Common::FS::RemoveFile(path)) {
        LOG_ERROR(Service_ACC, "Failed to clear save-data thumbnail");
        rb.Push(Account::ResultAccountUpdateFailed);
        return;
    }
    rb.Push(ResultSuccess);
}

void Module::Interface::LoadSaveDataThumbnail(HLERequestContext& ctx) {
    IPC::RequestParser rp{ctx};
    const auto uuid = rp.PopRaw<Common::UUID>();
    const auto tid = rp.Pop<u64_le>();

    IPC::ResponseBuilder rb{ctx, 3};
    if (tid == 0) {
        rb.Push(Account::ResultInvalidApplication);
        return;
    }
    if (uuid.IsInvalid() || !profile_manager->UserExists(uuid)) {
        rb.Push(Account::ResultInvalidUserId);
        return;
    }

    const auto path = GetSaveDataThumbnailPath(uuid, tid);
    if (!Common::FS::Exists(path)) {
        rb.Push(ResultSuccess);
        rb.Push<u32>(0);
        return;
    }

    Common::FS::IOFile thumbnail(path, Common::FS::FileAccessMode::Read,
                                 Common::FS::FileType::BinaryFile);
    if (!thumbnail.IsOpen() || thumbnail.GetSize() != THUMBNAIL_SIZE ||
        ctx.GetWriteBufferSize() < THUMBNAIL_SIZE) {
        rb.Push(Account::ResultInvalidArrayLength);
        return;
    }

    std::vector<u8> data(THUMBNAIL_SIZE);
    if (thumbnail.Read(data) != data.size()) {
        rb.Push(Account::ResultAccountUpdateFailed);
        return;
    }

    ctx.WriteBuffer(data);
    rb.Push(ResultSuccess);
    rb.Push(static_cast<u32>(data.size()));
}

void Module::Interface::GetSaveDataThumbnailExistence(HLERequestContext& ctx) {
    IPC::RequestParser rp{ctx};
    const auto uuid = rp.PopRaw<Common::UUID>();
    const auto tid = rp.Pop<u64_le>();

    IPC::ResponseBuilder rb{ctx, 3};
    if (tid == 0) {
        rb.Push(Account::ResultInvalidApplication);
        return;
    }
    if (uuid.IsInvalid() || !profile_manager->UserExists(uuid)) {
        rb.Push(Account::ResultInvalidUserId);
        return;
    }

    rb.Push(ResultSuccess);
    rb.Push(Common::FS::Exists(GetSaveDataThumbnailPath(uuid, tid)));
}

void Module::Interface::TrySelectUserWithoutInteractionDeprecated(HLERequestContext& ctx) {
    LOG_DEBUG(Service_ACC, "called");
    // A u8 is passed into this function which we can safely ignore. It's to determine if we have
    // access to use the network or not by the looks of it
    IPC::ResponseBuilder rb{ctx, 6};
    if (profile_manager->GetUserCount() != 1) {
        rb.Push(ResultSuccess);
        rb.PushRaw(Common::InvalidUUID);
        return;
    }

    const auto user_list = profile_manager->GetAllUsers();
    if (std::ranges::all_of(user_list, [](const auto& user) { return user.IsInvalid(); })) {
        rb.Push(ResultUnknown); // TODO(ogniK): Find the correct error code
        rb.PushRaw(Common::InvalidUUID);
        return;
    }

    // Select the first user we have
    rb.Push(ResultSuccess);
    rb.PushRaw(profile_manager->GetUser(0)->uuid);
}

void Module::Interface::TrySelectUserWithoutInteraction(HLERequestContext& ctx) {
    LOG_DEBUG(Service_ACC, "called");
    // A u8 is passed into this function which we can safely ignore. It's to determine if we have
    // access to use the network or not by the looks of it
    IPC::ResponseBuilder rb{ctx, 6};
    if (profile_manager->GetUserCount() != 1) {
        rb.Push(ResultSuccess);
        rb.PushRaw(Common::InvalidUUID);
        return;
    }

    const auto user_list = profile_manager->GetAllUsers();
    if (std::ranges::all_of(user_list, [](const auto& user) { return user.IsInvalid(); })) {
        rb.Push(ResultUnknown); // TODO(ogniK): Find the correct error code
        rb.PushRaw(Common::InvalidUUID);
        return;
    }

    // Select the first user we have
    rb.Push(ResultSuccess);
    rb.PushRaw(profile_manager->GetUser(0)->uuid);
}

Module::Interface::Interface(std::shared_ptr<Module> module_,
                             std::shared_ptr<ProfileManager> profile_manager_,
                             Core::System& system_, const char* name)
    : ServiceFramework{system_, name}, module{std::move(module_)},
      profile_manager{std::move(profile_manager_)} {}

Module::Interface::~Interface() = default;

class ACC_AA final : public Module::Interface {
public:
    explicit ACC_AA(std::shared_ptr<Module> module_, std::shared_ptr<ProfileManager> profile_manager_, Core::System& system_)
        : Interface(std::move(module_), std::move(profile_manager_), system_, "acc:aa") {
        // clang-format off
        static const FunctionInfo functions[] = {
            {0, &ACC_AA::RejectUnavailableNintendoAccount, "EnsureCacheAsync"},
            {1, &ACC_AA::RejectUnavailableNintendoAccount, "LoadCache"},
            {2, &ACC_AA::RejectUnavailableNintendoAccount, "GetDeviceAccountId"},
            {50, &ACC_AA::RejectUnavailableNintendoAccount, "RegisterNotificationTokenAsync"},   // 1.0.0 - 6.2.0
            {51, &ACC_AA::RejectUnavailableNintendoAccount, "UnregisterNotificationTokenAsync"}, // 1.0.0 - 6.2.0
        };
        // clang-format on
        RegisterHandlers(functions);
    }
    ~ACC_AA() override = default;

    void RejectUnavailableNintendoAccount(HLERequestContext& ctx) {
        LOG_WARNING(Service_ACC, "Rejected ACC_AA Nintendo Account/network command: backend unavailable");
        IPC::ResponseBuilder rb{ctx, 2};
        rb.Push(ResultUnknown);
    }

};

class ACC_SU final : public Module::Interface {
public:
    explicit ACC_SU(std::shared_ptr<Module> module_, std::shared_ptr<ProfileManager> profile_manager_, Core::System& system_)
        : Interface(std::move(module_), std::move(profile_manager_), system_, "acc:su") {
        // clang-format off
        static const FunctionInfo functions[] = {
            {0, &ACC_SU::GetUserCount, "GetUserCount"},
            {1, &ACC_SU::GetUserExistence, "GetUserExistence"},
            {2, &ACC_SU::ListAllUsers, "ListAllUsers"},
            {3, &ACC_SU::ListOpenUsers, "ListOpenUsers"},
            {4, &ACC_SU::GetLastOpenedUser, "GetLastOpenedUser"},
            {5, &ACC_SU::GetProfile, "GetProfile"},
            {6, &ACC_SU::RejectUnsupportedRemainingAccountCommand, "GetProfileDigest"},
            {50, &ACC_SU::IsUserRegistrationRequestPermitted, "IsUserRegistrationRequestPermitted"},
            {51, &ACC_SU::TrySelectUserWithoutInteractionDeprecated, "TrySelectUserWithoutInteractionDeprecated"},
            {52, &ACC_SU::TrySelectUserWithoutInteraction, "TrySelectUserWithoutInteraction"}, // 19.0.0+
            {60, &ACC_SU::ListOpenContextStoredUsers, "ListOpenContextStoredUsers"},
            {99, &ACC_SU::RejectDebugOnlyCommand, "DebugActivateOpenContextRetention"},
            {100, &ACC_SU::RejectUnsupportedRemainingAccountCommand, "GetUserRegistrationNotifier"},
            {101, &ACC_SU::RejectUnsupportedRemainingAccountCommand, "GetUserStateChangeNotifier"},
            {102, &ACC_SU::GetBaasAccountManagerForSystemService, "GetBaasAccountManagerForSystemService"},
            {103, &ACC_SU::RejectUnsupportedRemainingAccountCommand, "GetBaasUserAvailabilityChangeNotifier"},
            {104, &ACC_SU::RejectUnsupportedRemainingAccountCommand, "GetProfileUpdateNotifier"},
            {105, &ACC_SU::RejectUnavailableNintendoAccount, "CheckNetworkServiceAvailabilityAsync"},
            {106, &ACC_SU::RejectUnsupportedRemainingAccountCommand, "GetProfileSyncNotifier"},
            {110, &ACC_SU::StoreSaveDataThumbnailSystem, "StoreSaveDataThumbnail"},
            {111, &ACC_SU::ClearSaveDataThumbnail, "ClearSaveDataThumbnail"},
            {112, &ACC_SU::LoadSaveDataThumbnail, "LoadSaveDataThumbnail"},
            {113, &ACC_SU::GetSaveDataThumbnailExistence, "GetSaveDataThumbnailExistence"},
            {120, &ACC_SU::RejectUnsupportedRemainingAccountCommand, "ListOpenUsersInApplication"},
            {130, &ACC_SU::RejectUnsupportedRemainingAccountCommand, "ActivateOpenContextRetention"},
            {140, &ACC_SU::ListQualifiedUsers, "ListQualifiedUsers"},
            {150, &ACC_SU::RejectUnavailableNintendoAccount, "AuthenticateApplicationAsync"},
            {151, &ACC_SU::RejectUnavailableNintendoAccount, "EnsureSignedDeviceIdentifierCacheForNintendoAccountAsync"},
            {152, &ACC_SU::RejectUnavailableNintendoAccount, "LoadSignedDeviceIdentifierCacheForNintendoAccount"},
            {190, &ACC_SU::RejectUnsupportedRemainingAccountCommand, "GetUserLastOpenedApplication"},
            {191, &ACC_SU::RejectUnsupportedRemainingAccountCommand, "ActivateOpenContextHolder"},
            {200, &ACC_SU::BeginUserRegistration, "BeginUserRegistration"},
            {201, &ACC_SU::CompleteUserRegistration, "CompleteUserRegistration"},
            {202, &ACC_SU::CancelUserRegistration, "CancelUserRegistration"},
            {203, &ACC_SU::DeleteUser, "DeleteUser"},
            {204, &ACC_SU::SetUserPosition, "SetUserPosition"},
            {205, &ACC_SU::GetProfileEditor, "GetProfileEditor"},
            {206, &ACC_SU::CompleteUserRegistrationForcibly, "CompleteUserRegistrationForcibly"},
            {210, &ACC_SU::RejectUnavailableNintendoAccount, "CreateFloatingRegistrationRequest"},
            {211, &ACC_SU::RejectUnavailableNintendoAccount, "CreateProcedureToRegisterUserWithNintendoAccount"},
            {212, &ACC_SU::RejectUnavailableNintendoAccount, "ResumeProcedureToRegisterUserWithNintendoAccount"},
            {230, &ACC_SU::RejectUnavailableNintendoAccount, "AuthenticateServiceAsync"},
            {250, &ACC_SU::GetBaasAccountAdministrator, "GetBaasAccountAdministrator"},
            {290, &ACC_SU::RejectUnavailableNintendoAccount, "ProxyProcedureForGuestLoginWithNintendoAccount"},
            {291, &ACC_SU::RejectUnavailableNintendoAccount, "ProxyProcedureForFloatingRegistrationWithNintendoAccount"},
            {299, &ACC_SU::RejectUnavailableNintendoAccount, "SuspendBackgroundDaemon"},
            {400, &ACC_SU::RejectUnsupportedRemainingAccountCommand, "SetPinCode"}, // 18.0.0+
            {401, &ACC_SU::GetPinCodeLength, "GetPinCodeLength"}, // 18.0.0+
            {402, &ACC_SU::RejectUnsupportedRemainingAccountCommand, "GetPinCode"}, // 18.0.0+
            {403, &ACC_SU::RejectUnsupportedRemainingAccountCommand, "GetPinCodeParity"},
            {404, &ACC_SU::RejectUnsupportedRemainingAccountCommand, "VerifyPinCode"},
            {405, &ACC_SU::RejectUnsupportedRemainingAccountCommand, "IsPinCodeVerificationForbidden"},
            {410, &ACC_SU::RejectUnsupportedRemainingAccountCommand, "GetPinCodeErrorCount"}, // 18.0.0+
            {411, &ACC_SU::RejectUnsupportedRemainingAccountCommand, "ResetPinCodeErrorCount"}, // 18.0.0+
            {412, &ACC_SU::RejectUnsupportedRemainingAccountCommand, "IncrementPinCodeErrorCount"}, // 18.0.0+
            {900, &ACC_SU::RejectDebugOnlyCommand, "SetUserUnqualifiedForDebug"},
            {901, &ACC_SU::RejectDebugOnlyCommand, "UnsetUserUnqualifiedForDebug"},
            {902, &ACC_SU::RejectDebugOnlyCommand, "ListUsersUnqualifiedForDebug"},
            {910, &ACC_SU::RejectDebugOnlyCommand, "RefreshFirmwareSettingsForDebug"},
            {997, &ACC_SU::RejectDebugOnlyCommand, "DebugInvalidateTokenCacheForUser"},
            {998, &ACC_SU::RejectDebugOnlyCommand, "DebugSetUserStateClose"},
            {999, &ACC_SU::RejectDebugOnlyCommand, "DebugSetUserStateOpen"},
        };
        // clang-format on

        RegisterHandlers(functions);
    }
private:
    void RejectUnavailableNintendoAccount(HLERequestContext& ctx) {
        LOG_DEBUG(Service_ACC, "Rejected acc:su Nintendo Account/auth backend operation");
        IPC::ResponseBuilder rb{ctx, 2};
        rb.Push(ResultUnknown);
    }

    void RejectDebugOnlyCommand(HLERequestContext& ctx) {
        LOG_WARNING(Service_ACC, "Rejected unsupported acc:su debug-only command");
        IPC::ResponseBuilder rb{ctx, 2};
        rb.Push(ResultUnknown);
    }

public:
    ~ACC_SU() override = default;
private:
    void RejectUnsupportedRemainingAccountCommand(HLERequestContext& ctx) {
        // Local profile/user commands already implemented above remain untouched.
        // Remaining entries require Nintendo Account, notification, PIN policy,
        // debug, thumbnail or firmware services not modeled by Encore.
        IPC::ResponseBuilder rb{ctx, 2};
        rb.Push(IPC::ResultNotSupported);
    }

};

class ACC_U0 final : public Module::Interface {
public:
    ACC_U0(std::shared_ptr<Module> module_, std::shared_ptr<ProfileManager> profile_manager_, Core::System& system_)
        : Interface(std::move(module_), std::move(profile_manager_), system_, "acc:u0") {
        // clang-format off
        static const FunctionInfo functions[] = {
            {0, &ACC_U0::GetUserCount, "GetUserCount"},
            {1, &ACC_U0::GetUserExistence, "GetUserExistence"},
            {2, &ACC_U0::ListAllUsers, "ListAllUsers"},
            {3, &ACC_U0::ListOpenUsers, "ListOpenUsers"},
            {4, &ACC_U0::GetLastOpenedUser, "GetLastOpenedUser"},
            {5, &ACC_U0::GetProfile, "GetProfile"},
            {6, &ACC_U0::RejectUnsupportedRemainingAccountCommand, "GetProfileDigest"}, // 3.0.0+
            {50, &ACC_U0::IsUserRegistrationRequestPermitted, "IsUserRegistrationRequestPermitted"},
            {51, &ACC_U0::TrySelectUserWithoutInteractionDeprecated, "TrySelectUserWithoutInteractionDeprecated"},
            {52, &ACC_U0::TrySelectUserWithoutInteraction, "TrySelectUserWithoutInteraction"},
            {60, &ACC_U0::ListOpenContextStoredUsers, "ListOpenContextStoredUsers"}, // 5.0.0 - 5.1.0
            {99, &ACC_U0::RejectUnsupportedRemainingAccountCommand, "DebugActivateOpenContextRetention"}, // 6.0.0+
            {100, &ACC_U0::InitializeApplicationInfo, "InitializeApplicationInfo"},
            {101, &ACC_U0::GetBaasAccountManagerForApplication, "GetBaasAccountManagerForApplication"},
            {102, &ACC_U0::RejectUnsupportedRemainingAccountCommand, "AuthenticateApplicationAsync"},
            {103, &ACC_U0::RejectUnsupportedRemainingAccountCommand, "CheckNetworkServiceAvailabilityAsync"}, // 4.0.0+
            {110, &ACC_U0::StoreSaveDataThumbnailApplication, "StoreSaveDataThumbnail"},
            {111, &ACC_U0::RejectUnsupportedRemainingAccountCommand, "ClearSaveDataThumbnail"},
            {120, &ACC_U0::RejectUnsupportedRemainingAccountCommand, "CreateGuestLoginRequest"},
            {130, &ACC_U0::LoadOpenContext, "LoadOpenContext"}, // 5.0.0+
            {131, &ACC_U0::ListOpenContextStoredUsers, "ListOpenContextStoredUsers"}, // 6.0.0+
            {140, &ACC_U0::InitializeApplicationInfoRestricted, "InitializeApplicationInfoRestricted"}, // 6.0.0+
            {141, &ACC_U0::ListQualifiedUsers, "ListQualifiedUsers"}, // 6.0.0+
            {150, &ACC_U0::IsUserAccountSwitchLocked, "IsUserAccountSwitchLocked"}, // 6.0.0+
            {160, &ACC_U0::InitializeApplicationInfoV2, "InitializeApplicationInfoV2"},
        };
        // clang-format on

        RegisterHandlers(functions);
    }
    ~ACC_U0() override = default;
private:
    void RejectUnsupportedRemainingAccountCommand(HLERequestContext& ctx) {
        // Local profile/user commands already implemented above remain untouched.
        // Remaining entries require Nintendo Account, notification, PIN policy,
        // debug, thumbnail or firmware services not modeled by Encore.
        IPC::ResponseBuilder rb{ctx, 2};
        rb.Push(IPC::ResultNotSupported);
    }

};

class ACC_U1 final : public Module::Interface {
public:
    ACC_U1(std::shared_ptr<Module> module_, std::shared_ptr<ProfileManager> profile_manager_, Core::System& system_)
        : Interface(std::move(module_), std::move(profile_manager_), system_, "acc:u1") {
        // clang-format off
        static const FunctionInfo functions[] = {
            {0, &ACC_U1::GetUserCount, "GetUserCount"},
            {1, &ACC_U1::GetUserExistence, "GetUserExistence"},
            {2, &ACC_U1::ListAllUsers, "ListAllUsers"},
            {3, &ACC_U1::ListOpenUsers, "ListOpenUsers"},
            {4, &ACC_U1::GetLastOpenedUser, "GetLastOpenedUser"},
            {5, &ACC_U1::GetProfile, "GetProfile"},
            {6, &ACC_U1::RejectUnsupportedRemainingAccountCommand, "GetProfileDigest"},
            {50, &ACC_U1::IsUserRegistrationRequestPermitted, "IsUserRegistrationRequestPermitted"},
            {51, &ACC_U1::TrySelectUserWithoutInteractionDeprecated, "TrySelectUserWithoutInteractionDeprecated"},
            {52, &ACC_U1::TrySelectUserWithoutInteraction, "TrySelectUserWithoutInteraction"},
            {60, &ACC_U1::ListOpenContextStoredUsers, "ListOpenContextStoredUsers"},
            {99, &ACC_U1::RejectUnsupportedRemainingAccountCommand, "DebugActivateOpenContextRetention"},
            {100, &ACC_U1::RejectUnsupportedRemainingAccountCommand, "GetUserRegistrationNotifier"},
            {101, &ACC_U1::RejectUnsupportedRemainingAccountCommand, "GetUserStateChangeNotifier"},
            {102, &ACC_U1::GetBaasAccountManagerForSystemService, "GetBaasAccountManagerForSystemService"},
            {103, &ACC_U1::RejectUnsupportedRemainingAccountCommand, "GetBaasUserAvailabilityChangeNotifier"},
            {104, &ACC_U1::RejectUnsupportedRemainingAccountCommand, "GetProfileUpdateNotifier"},
            {105, &ACC_U1::RejectUnsupportedRemainingAccountCommand, "CheckNetworkServiceAvailabilityAsync"},
            {106, &ACC_U1::RejectUnsupportedRemainingAccountCommand, "GetProfileSyncNotifier"},
            {110, &ACC_U1::StoreSaveDataThumbnailApplication, "StoreSaveDataThumbnail"},
            {111, &ACC_U1::RejectUnsupportedRemainingAccountCommand, "ClearSaveDataThumbnail"},
            {112, &ACC_U1::RejectUnsupportedRemainingAccountCommand, "LoadSaveDataThumbnail"},
            {113, &ACC_U1::RejectUnsupportedRemainingAccountCommand, "GetSaveDataThumbnailExistence"},
            {120, &ACC_U1::RejectUnsupportedRemainingAccountCommand, "ListOpenUsersInApplication"},
            {130, &ACC_U1::RejectUnsupportedRemainingAccountCommand, "ActivateOpenContextRetention"},
            {140, &ACC_U1::ListQualifiedUsers, "ListQualifiedUsers"},
            {150, &ACC_U1::RejectUnsupportedRemainingAccountCommand, "AuthenticateApplicationAsync"},
            {151, &ACC_U1::RejectUnsupportedRemainingAccountCommand, "EnsureSignedDeviceIdentifierCacheForNintendoAccountAsync"},
            {152, &ACC_U1::RejectUnsupportedRemainingAccountCommand, "LoadSignedDeviceIdentifierCacheForNintendoAccount"},
            {190, &ACC_U1::RejectUnsupportedRemainingAccountCommand, "GetUserLastOpenedApplication"},
            {191, &ACC_U1::RejectUnsupportedRemainingAccountCommand, "ActivateOpenContextHolder"},
            {401, &ACC_U1::GetPinCodeLength, "GetPinCodeLength"}, // 18.0.0+
            {402, &ACC_U1::RejectUnsupportedRemainingAccountCommand, "GetPinCode"}, // 18.0.0+
            {997, &ACC_U1::RejectUnsupportedRemainingAccountCommand, "DebugInvalidateTokenCacheForUser"},
            {998, &ACC_U1::RejectUnsupportedRemainingAccountCommand, "DebugSetUserStateClose"},
            {999, &ACC_U1::RejectUnsupportedRemainingAccountCommand, "DebugSetUserStateOpen"},
        };
        // clang-format on

        RegisterHandlers(functions);
    }
    ~ACC_U1() override = default;
private:
    void RejectUnsupportedRemainingAccountCommand(HLERequestContext& ctx) {
        // Local profile/user commands already implemented above remain untouched.
        // Remaining entries require Nintendo Account, notification, PIN policy,
        // debug, thumbnail or firmware services not modeled by Encore.
        IPC::ResponseBuilder rb{ctx, 2};
        rb.Push(IPC::ResultNotSupported);
    }

};

class DAUTH_0 final : public ServiceFramework<DAUTH_0> {
public:
    explicit DAUTH_0(Core::System& system_) : ServiceFramework{system_, "dauth:0"} {
        // clang-format off
        static const FunctionInfo functions[] = {
            {0, &DAUTH_0::RejectUnavailableNintendoAccount, "EnsureAuthenticationTokenCacheAsync"},
            {1, &DAUTH_0::RejectUnavailableNintendoAccount, "LoadAuthenticationTokenCache"},
            {2, &DAUTH_0::RejectUnavailableNintendoAccount, "InvalidateAuthenticationTokenCache"},
            {3, &DAUTH_0::RejectUnavailableNintendoAccount, "IsDeviceAuthenticationTokenCacheAvailable"},
            {10, &DAUTH_0::RejectUnavailableNintendoAccount, "EnsureEdgeTokenCacheAsync"},
            {11, &DAUTH_0::RejectUnavailableNintendoAccount, "LoadEdgeTokenCache"},
            {12, &DAUTH_0::RejectUnavailableNintendoAccount, "InvalidateEdgeTokenCache"},
            {13, &DAUTH_0::RejectUnavailableNintendoAccount, "IsEdgeTokenCacheAvailable"},
            {20, &DAUTH_0::RejectUnavailableNintendoAccount, "EnsureApplicationAuthenticationCacheAsync"},
            {21, &DAUTH_0::RejectUnavailableNintendoAccount, "LoadApplicationAuthenticationTokenCache"},
            {22, &DAUTH_0::RejectUnavailableNintendoAccount, "LoadApplicationNetworkServiceClientConfigCache"},
            {23, &DAUTH_0::RejectUnavailableNintendoAccount, "IsApplicationAuthenticationCacheAvailable"},
            {24, &DAUTH_0::RejectUnavailableNintendoAccount, "InvalidateApplicationAuthenticationCache"},
            {30, &DAUTH_0::RejectUnavailableNintendoAccount, "EnsureGameCardAuthenticationCacheAsync"},
            {31, &DAUTH_0::RejectUnavailableNintendoAccount, "LoadGameCardAuthenticationTokenCache"},
            {32, &DAUTH_0::RejectUnavailableNintendoAccount, "IsGameCardAuthenticationCacheAvailable"},
            {33, &DAUTH_0::RejectUnavailableNintendoAccount, "InvalidateGameCardAuthenticationCache"},
            {1000, &DAUTH_0::RejectUnavailableNintendoAccount, "GetInactiveElicenseUsedEvent"},
            {9000, &DAUTH_0::RejectUnavailableNintendoAccount, "ImportVirtualClientCertificate"},
            {9010, &DAUTH_0::RejectUnavailableNintendoAccount, "DeleteVirtualClientCertificate"},
        };
        // clang-format on
        RegisterHandlers(functions);
    }

    void RejectUnavailableNintendoAccount(HLERequestContext& ctx) {
        LOG_WARNING(Service_ACC, "Rejected DAUTH_0 Nintendo Account/network command: backend unavailable");
        IPC::ResponseBuilder rb{ctx, 2};
        rb.Push(ResultUnknown);
    }

};

class ACC_E final : public Module::Interface {
public:
    explicit ACC_E(std::shared_ptr<Module> module_, std::shared_ptr<ProfileManager> profile_manager_, Core::System& system_)
        : Interface(std::move(module_), std::move(profile_manager_), system_, "acc:e") {
        // clang-format off
        static const FunctionInfo functions[] = {
            { 0, &ACC_E::GetUserCount, "GetUserCount"},
            { 1, &ACC_E::GetUserExistence, "GetUserExistence"},
            { 2, &ACC_E::ListAllUsers, "ListAllUsers"},
            { 3, &ACC_E::ListOpenUsers, "ListOpenUsers"},
            { 4, &ACC_E::GetLastOpenedUser, "GetLastOpenedUser"},
            { 5, &ACC_E::GetProfile, "GetProfile"},
            { 6, &ACC_E::RejectUnsupportedRemainingAccountCommand, "GetProfileDigest"},
            { 50, &ACC_E::IsUserRegistrationRequestPermitted, "IsUserRegistrationRequestPermitted"},
            { 51, &ACC_E::TrySelectUserWithoutInteractionDeprecated, "TrySelectUserWithoutInteractionDeprecated"},
            { 52, &ACC_E::TrySelectUserWithoutInteraction, "TrySelectUserWithoutInteraction"},
            { 99, &ACC_E::RejectUnsupportedOnlineOrDebugCommand, "DebugActivateOpenContextRetention"},
            { 100, &ACC_E::RejectUnsupportedRemainingAccountCommand, "GetUserRegistrationNotifier"},
            { 101, &ACC_E::RejectUnsupportedRemainingAccountCommand, "GetUserStateChangeNotifier"},
            { 102, &ACC_E::GetBaasAccountManagerForSystemService, "GetBaasAccountManagerForSystemService"},
            { 103, &ACC_E::RejectUnsupportedOnlineOrDebugCommand, "GetBaasUserAvailabilityChangeNotifier"},
            { 104, &ACC_E::RejectUnsupportedRemainingAccountCommand, "GetProfileUpdateNotifier"},
            { 105, &ACC_E::RejectUnsupportedOnlineOrDebugCommand, "CheckNetworkServiceAvailabilityAsync"},
            { 106, &ACC_E::RejectUnsupportedOnlineOrDebugCommand, "GetProfileSyncNotifier"},
            { 110, &ACC_E::StoreSaveDataThumbnailSystem, "StoreSaveDataThumbnail"},
            { 111, &ACC_E::ClearSaveDataThumbnail, "ClearSaveDataThumbnail"},
            { 112, &ACC_E::LoadSaveDataThumbnail, "LoadSaveDataThumbnail"},
            { 113, &ACC_E::GetSaveDataThumbnailExistence, "GetSaveDataThumbnailExistence"},
            { 120, &ACC_E::RejectUnsupportedRemainingAccountCommand, "ListOpenUsersInApplication"},
            { 130, &ACC_E::RejectUnsupportedRemainingAccountCommand, "ActivateOpenContextRetention"},
            { 140, &ACC_E::ListQualifiedUsers, "ListQualifiedUsers"},
            { 151, &ACC_E::RejectUnsupportedOnlineOrDebugCommand, "EnsureSignedDeviceIdentifierCacheForNintendoAccountAsync"},
            { 152, &ACC_E::RejectUnsupportedOnlineOrDebugCommand, "LoadSignedDeviceIdentifierCacheForNintendoAccount"},
            { 170, &ACC_E::RejectUnsupportedOnlineOrDebugCommand, "GetNasOp2MembershipStateChangeNotifier"},
            { 191, &ACC_E::RejectUnsupportedRemainingAccountCommand, "UpdateNotificationReceiverInfo"},
            { 200, &ACC_E::BeginUserRegistration, "BeginUserRegistration"},
            { 201, &ACC_E::CompleteUserRegistration, "CompleteUserRegistration"},
            { 202, &ACC_E::CancelUserRegistration, "CancelUserRegistration"},
            { 203, &ACC_E::DeleteUser, "DeleteUser"},
            { 204, &ACC_E::SetUserPosition, "SetUserPosition"},
            { 205, &ACC_E::GetProfileEditor, "GetProfileEditor"},
            { 206, &ACC_E::CompleteUserRegistrationForcibly, "CompleteUserRegistrationForcibly"},
            { 210, &ACC_E::RejectUnsupportedOnlineOrDebugCommand, "CreateFloatingRegistrationRequest"},
            { 211, &ACC_E::RejectUnsupportedOnlineOrDebugCommand, "CreateProcedureToRegisterUserWithNintendoAccount"},
            { 212, &ACC_E::RejectUnsupportedOnlineOrDebugCommand, "ResumeProcedureToRegisterUserWithNintendoAccount"},
            { 213, &ACC_E::RejectUnsupportedOnlineOrDebugCommand, "CreateProcedureToCreateUserWithNintendoAccount"},
            { 214, &ACC_E::RejectUnsupportedOnlineOrDebugCommand, "ResumeProcedureToCreateUserWithNintendoAccount"},
            { 215, &ACC_E::RejectUnsupportedOnlineOrDebugCommand, "ResumeProcedureToCreateUserWithNintendoAccountAfterApplyResponse"},
            { 230, &ACC_E::RejectUnsupportedOnlineOrDebugCommand, "AuthenticateServiceAsync"},
            { 250, &ACC_E::RejectUnsupportedRemainingAccountCommand, "GetBaasAccountAdministrator"},
            { 251, &ACC_E::RejectUnsupportedOnlineOrDebugCommand, "SynchronizeNetworkServiceAccountsSnapshotAsync"},
            { 290, &ACC_E::RejectUnsupportedOnlineOrDebugCommand, "ProxyProcedureForGuestLoginWithNintendoAccount"},
            { 291, &ACC_E::RejectUnsupportedOnlineOrDebugCommand, "ProxyProcedureForFloatingRegistrationWithNintendoAccount"},
            { 292, &ACC_E::RejectUnsupportedOnlineOrDebugCommand, "ProxyProcedureForDeviceMigrationAuthenticatingOperatingUser"},
            { 293, &ACC_E::RejectUnsupportedOnlineOrDebugCommand, "ProxyProcedureForDeviceMigrationDownload"},
            { 299, &ACC_E::RejectUnsupportedOnlineOrDebugCommand, "SuspendBackgroundDaemon"},
            { 350, &ACC_E::RejectUnsupportedOnlineOrDebugCommand, "CreateDeviceMigrationUserExportRequest"},
            { 351, &ACC_E::RejectUnsupportedOnlineOrDebugCommand, "UploadNasCredential"},
            { 352, &ACC_E::RejectUnsupportedOnlineOrDebugCommand, "CreateDeviceMigrationUserImportRequest"},
            { 353, &ACC_E::RejectUnsupportedOnlineOrDebugCommand, "DeleteUserMigrationSaveData"},
            { 400, &ACC_E::RejectUnsupportedRemainingAccountCommand, "SetPinCode"},
            { 401, &ACC_E::RejectUnsupportedRemainingAccountCommand, "GetPinCodeLength"},
            { 402, &ACC_E::RejectUnsupportedRemainingAccountCommand, "GetPinCode"},
            { 403, &ACC_E::RejectUnsupportedRemainingAccountCommand, "GetPinCodeParity"},
            { 404, &ACC_E::RejectUnsupportedRemainingAccountCommand, "VerifyPinCode"},
            { 405, &ACC_E::RejectUnsupportedRemainingAccountCommand, "IsPinCodeVerificationForbidden"},
            { 410, &ACC_E::RejectUnsupportedRemainingAccountCommand, "GetPinCodeErrorCount"},
            { 411, &ACC_E::RejectUnsupportedRemainingAccountCommand, "ResetPinCodeErrorCount"},
            { 412, &ACC_E::RejectUnsupportedRemainingAccountCommand, "IncrementPinCodeErrorCount"},
            { 413, &ACC_E::RejectUnsupportedRemainingAccountCommand, "SetPinCodeErrorCount"},
            { 420, &ACC_E::RejectUnsupportedRemainingAccountCommand, "SetStartPenaltyTime"},
            { 421, &ACC_E::RejectUnsupportedRemainingAccountCommand, "GetStartPenaltyTime"},
            { 900, &ACC_E::RejectUnsupportedOnlineOrDebugCommand, "SetUserUnqualifiedForDebug"},
            { 901, &ACC_E::RejectUnsupportedOnlineOrDebugCommand, "UnsetUserUnqualifiedForDebug"},
            { 902, &ACC_E::RejectUnsupportedOnlineOrDebugCommand, "ListUsersUnqualifiedForDebug"},
            { 910, &ACC_E::RejectUnsupportedOnlineOrDebugCommand, "RefreshFirmwareSettingsForDebug"},
            { 997, &ACC_E::RejectUnsupportedOnlineOrDebugCommand, "DebugInvalidateTokenCacheForUser"},
            { 998, &ACC_E::RejectUnsupportedOnlineOrDebugCommand, "DebugSetUserStateClose"},
            { 999, &ACC_E::RejectUnsupportedOnlineOrDebugCommand, "DebugSetUserStateOpen"},
            { 1000, &ACC_E::RejectUnsupportedRemainingAccountCommand, "CreateIAccountEntityServiceForApplication"},
            { 1100, &ACC_E::RejectUnsupportedRemainingAccountCommand, "CreateIUserStateManager"},
            { 10050, &ACC_E::RejectUnsupportedRemainingAccountCommand, "IsUserRegistrationRequestPermittedForAccountPolicy"},
            { 10105, &ACC_E::RejectUnsupportedOnlineOrDebugCommand, "CheckNetworkServiceAvailabilityAsyncForAccountPolicy"},
        };
        // clang-format on
        RegisterHandlers(functions);
    }
private:
    void RejectUnsupportedOnlineOrDebugCommand(HLERequestContext& ctx) {
        // No Nintendo Account network, migration, or firmware debug backend on PS5.
        IPC::ResponseBuilder rb{ctx, 2};
        rb.Push(ResultUnknown);
    }
private:
    void RejectUnsupportedRemainingAccountCommand(HLERequestContext& ctx) {
        // Local profile/user commands already implemented above remain untouched.
        // Remaining entries require Nintendo Account, notification, PIN policy,
        // debug, thumbnail or firmware services not modeled by Encore.
        IPC::ResponseBuilder rb{ctx, 2};
        rb.Push(IPC::ResultNotSupported);
    }

};

class ACC_E_U1 final : public Module::Interface {
public:
    explicit ACC_E_U1(std::shared_ptr<Module> module_, std::shared_ptr<ProfileManager> profile_manager_, Core::System& system_)
        : Interface(std::move(module_), std::move(profile_manager_), system_, "acc:e:u1") {
        // clang-format off
        static const FunctionInfo functions[] = {
            {0, &ACC_E_U1::GetUserCount, "GetUserCount"},
            {1, &ACC_E_U1::GetUserExistence, "GetUserExistence"},
            {2, &ACC_E_U1::ListAllUsers, "ListAllUsers"},
            {3, &ACC_E_U1::ListOpenUsers, "ListOpenUsers"},
            {4, &ACC_E_U1::GetLastOpenedUser, "GetLastOpenedUser"},
            {5, &ACC_E_U1::GetProfile, "GetProfile"},
            {6, &ACC_E_U1::RejectUnsupportedRemainingAccountCommand, "GetProfileDigest"},
            {50, &ACC_E_U1::IsUserRegistrationRequestPermitted, "IsUserRegistrationRequestPermitted"},
            {51, &ACC_E_U1::TrySelectUserWithoutInteractionDeprecated, "TrySelectUserWithoutInteractionDeprecated"},
            {99, &ACC_E_U1::RejectUnsupportedOnlineOrDebugCommand, "DebugActivateOpenContextRetention"},
            {100, &ACC_E_U1::RejectUnsupportedRemainingAccountCommand, "GetUserRegistrationNotifier"},
            {101, &ACC_E_U1::RejectUnsupportedRemainingAccountCommand, "GetUserStateChangeNotifier"},
            {102, &ACC_E_U1::GetBaasAccountManagerForSystemService, "GetBaasAccountManagerForSystemService"},
            {103, &ACC_E_U1::RejectUnsupportedOnlineOrDebugCommand, "GetBaasUserAvailabilityChangeNotifier"},
            {104, &ACC_E_U1::RejectUnsupportedRemainingAccountCommand, "GetProfileUpdateNotifier"},
            {105, &ACC_E_U1::RejectUnsupportedOnlineOrDebugCommand, "CheckNetworkServiceAvailabilityAsync"},
            {106, &ACC_E_U1::RejectUnsupportedOnlineOrDebugCommand, "GetProfileSyncNotifier"},
            {110, &ACC_E_U1::StoreSaveDataThumbnailSystem, "StoreSaveDataThumbnail"},
            {111, &ACC_E_U1::ClearSaveDataThumbnail, "ClearSaveDataThumbnail"},
            {112, &ACC_E_U1::LoadSaveDataThumbnail, "LoadSaveDataThumbnail"},
            {113, &ACC_E_U1::GetSaveDataThumbnailExistence, "GetSaveDataThumbnailExistence"},
            {120, &ACC_E_U1::RejectUnsupportedRemainingAccountCommand, "ListOpenUsersInApplication"},
            {130, &ACC_E_U1::RejectUnsupportedRemainingAccountCommand, "ActivateOpenContextRetention"},
            {140, &ACC_E_U1::ListQualifiedUsers, "ListQualifiedUsers"},
            {151, &ACC_E_U1::RejectUnsupportedOnlineOrDebugCommand, "EnsureSignedDeviceIdentifierCacheForNintendoAccountAsync"},
            {152, &ACC_E_U1::RejectUnsupportedOnlineOrDebugCommand, "LoadSignedDeviceIdentifierCacheForNintendoAccount"},
            {170, &ACC_E_U1::RejectUnsupportedOnlineOrDebugCommand, "GetNasOp2MembershipStateChangeNotifier"},
            {191, &ACC_E_U1::RejectUnsupportedRemainingAccountCommand, "UpdateNotificationReceiverInfo"},
            {997, &ACC_E_U1::RejectUnsupportedOnlineOrDebugCommand, "DebugInvalidateTokenCacheForUser"},
            {998, &ACC_E_U1::RejectUnsupportedOnlineOrDebugCommand, "DebugSetUserStateClose"},
            {999, &ACC_E_U1::RejectUnsupportedOnlineOrDebugCommand, "DebugSetUserStateOpen"},
        };
        // clang-format on
        RegisterHandlers(functions);
    }
private:
    void RejectUnsupportedOnlineOrDebugCommand(HLERequestContext& ctx) {
        // No Nintendo Account network, migration, or firmware debug backend on PS5.
        IPC::ResponseBuilder rb{ctx, 2};
        rb.Push(ResultUnknown);
    }
private:
    void RejectUnsupportedRemainingAccountCommand(HLERequestContext& ctx) {
        // Local profile/user commands already implemented above remain untouched.
        // Remaining entries require Nintendo Account, notification, PIN policy,
        // debug, thumbnail or firmware services not modeled by Encore.
        IPC::ResponseBuilder rb{ctx, 2};
        rb.Push(IPC::ResultNotSupported);
    }

};

class ACC_E_U2 final : public Module::Interface {
public:
    explicit ACC_E_U2(std::shared_ptr<Module> module_, std::shared_ptr<ProfileManager> profile_manager_, Core::System& system_)
        : Interface(std::move(module_), std::move(profile_manager_), system_, "acc:e:u2") {
        // clang-format off
        static const FunctionInfo functions[] = {
            {0, &ACC_E_U2::GetUserCount, "GetUserCount"},
            {1, &ACC_E_U2::GetUserExistence, "GetUserExistence"},
            {2, &ACC_E_U2::ListAllUsers, "ListAllUsers"},
            {3, &ACC_E_U2::ListOpenUsers, "ListOpenUsers"},
            {4, &ACC_E_U2::GetLastOpenedUser, "GetLastOpenedUser"},
            {5, &ACC_E_U2::GetProfile, "GetProfile"},
            {6, &ACC_E_U2::RejectUnsupportedRemainingAccountCommand, "GetProfileDigest"},
            {50, &ACC_E_U2::IsUserRegistrationRequestPermitted, "#IsUserRegistrationRequestPermitted"},
            {51, &ACC_E_U2::TrySelectUserWithoutInteractionDeprecated, "TrySelectUserWithoutInteractionDeprecated"},
            {52, &ACC_E_U2::TrySelectUserWithoutInteraction, "TrySelectUserWithoutInteraction"},
            {99, &ACC_E_U2::RejectUnsupportedOnlineOrDebugCommand, "DebugActivateOpenContextRetention"},
            {100, &ACC_E_U2::RejectUnsupportedRemainingAccountCommand, "GetUserRegistrationNotifier"},
            {101, &ACC_E_U2::RejectUnsupportedRemainingAccountCommand, "GetUserStateChangeNotifier"},
            {102, &ACC_E_U2::GetBaasAccountManagerForSystemService, "GetBaasAccountManagerForSystemService"},
            {103, &ACC_E_U2::RejectUnsupportedOnlineOrDebugCommand, "GetBaasUserAvailabilityChangeNotifier"},
            {104, &ACC_E_U2::RejectUnsupportedRemainingAccountCommand, "GetProfileUpdateNotifier"},
            {105, &ACC_E_U2::RejectUnsupportedOnlineOrDebugCommand, "CheckNetworkServiceAvailabilityAsync"},
            {106, &ACC_E_U2::RejectUnsupportedOnlineOrDebugCommand, "GetProfileSyncNotifier"},
            {110, &ACC_E_U2::StoreSaveDataThumbnailSystem, "StoreSaveDataThumbnail"},
            {111, &ACC_E_U2::ClearSaveDataThumbnail, "ClearSaveDataThumbnail"},
            {112, &ACC_E_U2::LoadSaveDataThumbnail, "LoadSaveDataThumbnail"},
            {113, &ACC_E_U2::GetSaveDataThumbnailExistence, "GetSaveDataThumbnailExistence"},
            {120, &ACC_E_U2::RejectUnsupportedRemainingAccountCommand, "ListOpenUsersInApplication"},
            {130, &ACC_E_U2::RejectUnsupportedRemainingAccountCommand, "ActivateOpenContextRetention"},
            {140, &ACC_E_U2::ListQualifiedUsers, "ListQualifiedUsers"},
            {151, &ACC_E_U2::RejectUnsupportedOnlineOrDebugCommand, "EnsureSignedDeviceIdentifierCacheForNintendoAccountAsync"},
            {152, &ACC_E_U2::RejectUnsupportedOnlineOrDebugCommand, "LoadSignedDeviceIdentifierCacheForNintendoAccount"},
            {170, &ACC_E_U2::RejectUnsupportedOnlineOrDebugCommand, "GetNasOp2MembershipStateChangeNotifier"},
            {191, &ACC_E_U2::RejectUnsupportedRemainingAccountCommand, "UpdateNotificationReceiverInfo"},
            {205, &ACC_E_U2::GetProfileEditor, "GetProfileEditor"},
            {401, &ACC_E_U2::RejectUnsupportedRemainingAccountCommand, "GetPinCodeLength"},
            {402, &ACC_E_U2::RejectUnsupportedRemainingAccountCommand, "GetPinCode"},
            {403, &ACC_E_U2::RejectUnsupportedRemainingAccountCommand, "GetPinCodeParity"},
            {404, &ACC_E_U2::RejectUnsupportedRemainingAccountCommand, "VerifyPinCode"},
            {405, &ACC_E_U2::RejectUnsupportedRemainingAccountCommand, "IsPinCodeVerificationForbidden"},
            {997, &ACC_E_U2::RejectUnsupportedOnlineOrDebugCommand, "DebugInvalidateTokenCacheForUser"},
            {998, &ACC_E_U2::RejectUnsupportedOnlineOrDebugCommand, "DebugSetUserStateClose"},
            {999, &ACC_E_U2::RejectUnsupportedOnlineOrDebugCommand, "DebugSetUserStateOpen"},
        };
        // clang-format on
        RegisterHandlers(functions);
    }
private:
    void RejectUnsupportedOnlineOrDebugCommand(HLERequestContext& ctx) {
        // No Nintendo Account network, migration, or firmware debug backend on PS5.
        IPC::ResponseBuilder rb{ctx, 2};
        rb.Push(ResultUnknown);
    }
private:
    void RejectUnsupportedRemainingAccountCommand(HLERequestContext& ctx) {
        // Local profile/user commands already implemented above remain untouched.
        // Remaining entries require Nintendo Account, notification, PIN policy,
        // debug, thumbnail or firmware services not modeled by Encore.
        IPC::ResponseBuilder rb{ctx, 2};
        rb.Push(IPC::ResultNotSupported);
    }

};

void LoopProcess(Core::System& system) {
    auto module = std::make_shared<Module>();
    auto profile_manager = std::make_shared<ProfileManager>();
    auto server_manager = std::make_unique<ServerManager>(system);

    server_manager->RegisterNamedService("acc:aa", std::make_shared<ACC_AA>(module, profile_manager, system));
    server_manager->RegisterNamedService("acc:su", std::make_shared<ACC_SU>(module, profile_manager, system));

    server_manager->RegisterNamedService("acc:e", std::make_shared<ACC_E>(module, profile_manager, system));
    server_manager->RegisterNamedService("acc:e:u1", std::make_shared<ACC_E_U1>(module, profile_manager, system));
    server_manager->RegisterNamedService("acc:e:u2", std::make_shared<ACC_E_U2>(module, profile_manager, system));

    server_manager->RegisterNamedService("dauth:0", std::make_shared<DAUTH_0>(system));
    ServerManager::RunServer(std::move(server_manager));
}

} // namespace Service::Account
