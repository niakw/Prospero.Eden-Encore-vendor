// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <string_view>

#include "core/hle/service/cmif_types.h"
#include "core/hle/service/service.h"

namespace Service::AM {
    struct Applet;

    class IOverlayFunctions final : public ServiceFramework<IOverlayFunctions> {
    public:
        explicit IOverlayFunctions(Core::System &system_, std::shared_ptr<Applet> applet);
        ~IOverlayFunctions() override;

    private:
        Result RejectDebugOnlyCommand();
        Result BeginToWatchShortHomeButtonMessage();
        Result EndToWatchShortHomeButtonMessage();
        Result GetApplicationIdForLogo(Out<u64> out_application_id);
        u64 ResolveApplicationIdForLogo();
        Result ReadApplicationLogoResource(
            Out<u64> out_size, OutBuffer<BufferAttr_HipcMapAlias> out_buffer,
            std::string_view file_name);
        Result GetApplicationNintendoLogo(Out<u64> out_size,
                                           OutBuffer<BufferAttr_HipcMapAlias> out_buffer);
        Result GetApplicationStartupMovie(Out<u64> out_size,
                                           OutBuffer<BufferAttr_HipcMapAlias> out_buffer);
        Result SetGpuTimeSliceBoost(u64 value);
        Result SetAutoSleepTimeAndDimmingTimeEnabled(bool enabled);
        Result SetGpuTimeSliceBoostForApplication(u64 value);
        Result SetRequiresGpuResourceUse(bool required);
        Result TerminateApplicationAndSetReason(Result reason);
        Result StartShutdownSequenceForOverlay();
        Result StartRebootSequenceForOverlay();
        Result SetScreenShotPermissionGlobally(bool enabled);
        Result SetHealthWarningShowingState(bool showing);
        Result IsHealthWarningRequired(Out<bool> is_required);
        Result SetHandlingHomeButtonShortPressedEnabled(bool enabled);
        Result SetHandlingTouchScreenInputEnabled(bool enabled);
        Result RejectUnknown60(bool value);
        Result Unknown70();
        Result RejectUnknown71();
        Result RejectUnknown75(Out<u64> out_value);

    private:
        const std::shared_ptr<Applet> m_applet;
    };
} // namespace Service::AM
