// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

// SPDX-FileCopyrightText: Copyright 2024 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "core/core.h"
#include "core/hle/service/am/applet.h"
#include "core/hle/service/am/applet_manager.h"
#include "core/hle/service/am/window_system.h"
#include "core/hle/service/cmif_serialization.h"
#include "core/hle/service/nvnflinger/hos_binder_driver.h"
#include "core/hle/service/nvnflinger/parcel.h"
#include "core/hle/service/os/event.h"
#include "core/hle/service/vi/application_display_service.h"
#include "core/hle/service/vi/container.h"
#include "core/hle/service/vi/manager_display_service.h"
#include "core/hle/service/vi/system_display_service.h"
#include "core/hle/service/vi/vi_results.h"
#include "service_creator.h"

namespace Service::VI {

IApplicationDisplayService::IApplicationDisplayService(Core::System& system_,
                                                       std::shared_ptr<Container> container)
    : ServiceFramework{system_, "IApplicationDisplayService"},
      m_container{std::move(container)}, m_context{system, "IApplicationDisplayService"} {
    // clang-format off
    static const FunctionInfo functions[] = {
        {100, C<&IApplicationDisplayService::GetRelayService>, "GetRelayService"},
        {101, C<&IApplicationDisplayService::GetSystemDisplayService>, "GetSystemDisplayService"},
        {102, C<&IApplicationDisplayService::GetManagerDisplayService>, "GetManagerDisplayService"},
        {103, C<&IApplicationDisplayService::GetIndirectDisplayTransactionService>, "GetIndirectDisplayTransactionService"},
        {1000, C<&IApplicationDisplayService::ListDisplays>, "ListDisplays"},
        {1010, C<&IApplicationDisplayService::OpenDisplay>, "OpenDisplay"},
        {1011, C<&IApplicationDisplayService::OpenDefaultDisplay>, "OpenDefaultDisplay"},
        {1020, C<&IApplicationDisplayService::CloseDisplay>, "CloseDisplay"},
        {1101, C<&IApplicationDisplayService::SetDisplayEnabled>, "SetDisplayEnabled"},
        {1102, C<&IApplicationDisplayService::GetDisplayResolution>, "GetDisplayResolution"},
        {2020, C<&IApplicationDisplayService::OpenLayer>, "OpenLayer"},
        {2021, C<&IApplicationDisplayService::CloseLayer>, "CloseLayer"},
        {2030, C<&IApplicationDisplayService::CreateStrayLayer>, "CreateStrayLayer"},
        {2031, C<&IApplicationDisplayService::DestroyStrayLayer>, "DestroyStrayLayer"},
        {2101, C<&IApplicationDisplayService::SetLayerScalingMode>, "SetLayerScalingMode"},
        {2102, C<&IApplicationDisplayService::ConvertScalingMode>, "ConvertScalingMode"},
        {2103, C<&IApplicationDisplayService::Cmd2103>, "Cmd2103"},
        {2450, C<&IApplicationDisplayService::GetIndirectLayerImageMap>, "GetIndirectLayerImageMap"},
        {2451, C<&IApplicationDisplayService::GetIndirectLayerImageCropMap>, "GetIndirectLayerImageCropMap"},
        {2460, C<&IApplicationDisplayService::GetIndirectLayerImageRequiredMemoryInfo>, "GetIndirectLayerImageRequiredMemoryInfo"},
        {5202, C<&IApplicationDisplayService::GetDisplayVsyncEvent>, "GetDisplayVsyncEvent"},
        {5203, C<&IApplicationDisplayService::GetDisplayVsyncEvent>, "GetDisplayVsyncEventForDebug"},
    };
    // clang-format on

    RegisterHandlers(functions);
}

IApplicationDisplayService::~IApplicationDisplayService() {
    for (auto& [display_id, event] : m_display_vsync_events) {
        m_container->UnlinkVsyncEvent(display_id, &event);
    }
    for (const auto layer_id : m_open_layer_ids) {
        m_container->CloseLayer(layer_id);
    }
    for (const auto layer_id : m_stray_layer_ids) {
        m_container->DestroyStrayLayer(layer_id);
    }
}

Result IApplicationDisplayService::GetRelayService(
    Out<SharedPointer<Nvnflinger::IHOSBinderDriver>> out_relay_service) {
    LOG_DEBUG(Service_VI, "Returning relay binder service");
    R_RETURN(m_container->GetBinderDriver(out_relay_service));
}

Result IApplicationDisplayService::GetSystemDisplayService(
    Out<SharedPointer<ISystemDisplayService>> out_system_display_service) {
    LOG_DEBUG(Service_VI, "Creating system display service");
    *out_system_display_service = std::make_shared<ISystemDisplayService>(system, m_container);
    R_SUCCEED();
}

Result IApplicationDisplayService::GetManagerDisplayService(
    Out<SharedPointer<IManagerDisplayService>> out_manager_display_service) {
    LOG_DEBUG(Service_VI, "Creating manager display service");
    *out_manager_display_service = std::make_shared<IManagerDisplayService>(system, m_container);
    R_SUCCEED();
}

Result IApplicationDisplayService::GetIndirectDisplayTransactionService(
    Out<SharedPointer<Nvnflinger::IHOSBinderDriver>> out_indirect_display_transaction_service) {
    LOG_DEBUG(Service_VI, "Returning indirect-display binder service");
    R_RETURN(m_container->GetBinderDriver(out_indirect_display_transaction_service));
}

Result IApplicationDisplayService::OpenDisplay(Out<u64> out_display_id, DisplayName display_name) {
    LOG_DEBUG(Service_VI, "called with display_name={}", display_name.data());

    // Ensure the display name is null-terminated
    display_name[display_name.size() - 1] = '\0';

    // According to switchbrew, only "Default", "External", "Edid", "Internal" and "Null" are valid
    const std::array<std::string_view, 5> valid_names = {
        "Default", "External", "Edid", "Internal", "Null"
    };

    bool valid_name = false;
    for (const auto& name : valid_names) {
        if (name == display_name.data()) {
            valid_name = true;
            break;
        }
    }

    R_UNLESS(valid_name, ResultOperationFailed);

    R_RETURN(m_container->OpenDisplay(out_display_id, display_name));
}

Result IApplicationDisplayService::OpenDefaultDisplay(Out<u64> out_display_id) {
    LOG_DEBUG(Service_VI, "called");
    R_RETURN(this->OpenDisplay(out_display_id, DisplayName{"Default"}));
}

Result IApplicationDisplayService::CloseDisplay(u64 display_id) {
    LOG_DEBUG(Service_VI, "called");
    R_RETURN(m_container->CloseDisplay(display_id));
}

Result IApplicationDisplayService::SetDisplayEnabled(u32 state, u64 display_id) {
    LOG_DEBUG(Service_VI, "called");

    // This literally does nothing internally in the actual service itself,
    // and just returns a successful result code regardless of the input.
    R_SUCCEED();
}

Result IApplicationDisplayService::GetDisplayResolution(Out<s64> out_width, Out<s64> out_height,
                                                        u64 display_id) {
    LOG_DEBUG(Service_VI, "called. display_id={}", display_id);

    // This only returns the fixed values of 1280x720 and makes no distinguishing
    // between docked and undocked dimensions.
    *out_width = static_cast<s64>(DisplayResolution::UndockedWidth);
    *out_height = static_cast<s64>(DisplayResolution::UndockedHeight);
    R_SUCCEED();
}

Result IApplicationDisplayService::SetLayerScalingMode(NintendoScaleMode scale_mode, u64 layer_id) {
    LOG_DEBUG(Service_VI, "called. scale_mode={}, unknown={:#016x}", scale_mode, layer_id);

    if (scale_mode > NintendoScaleMode::PreserveAspectRatio) {
        LOG_ERROR(Service_VI, "Invalid scaling mode provided.");
        R_THROW(VI::ResultOperationFailed);
    }

    if (scale_mode != NintendoScaleMode::ScaleToWindow &&
        scale_mode != NintendoScaleMode::PreserveAspectRatio) {
        LOG_ERROR(Service_VI, "Unsupported scaling mode supplied.");
        R_THROW(VI::ResultNotSupported);
    }

    R_SUCCEED();
}

Result IApplicationDisplayService::ListDisplays(
    Out<u64> out_count, OutArray<DisplayInfo, BufferAttr_HipcMapAlias> out_displays) {
    LOG_DEBUG(Service_VI, "Listing the emulated Default display");

    if (out_displays.size() > 0) {
        out_displays[0] = DisplayInfo{};
        *out_count = 1;
    } else {
        *out_count = 0;
    }

    R_SUCCEED();
}

Result IApplicationDisplayService::OpenLayer(Out<u64> out_size,
                                             OutBuffer<BufferAttr_HipcMapAlias> out_native_window,
                                             DisplayName display_name, u64 layer_id,
                                             ClientAppletResourceUserId aruid) {
    display_name[display_name.size() - 1] = '\0';

    LOG_DEBUG(Service_VI, "called. layer_id={}, aruid={:#x}", layer_id, aruid.pid);

    u64 display_id;
    R_TRY(m_container->OpenDisplay(&display_id, display_name));

    s32 producer_binder_id;
    R_TRY(m_container->OpenLayer(&producer_binder_id, layer_id, aruid.pid));

    {
        std::scoped_lock lk{m_lock};
        m_open_layer_ids.insert(layer_id);
    }

    android::OutputParcel parcel;
    parcel.WriteInterface(NativeWindow{producer_binder_id});

    const auto buffer = parcel.Serialize();
    std::memcpy(out_native_window.data(), buffer.data(),
                (std::min)(out_native_window.size(), buffer.size()));
    *out_size = buffer.size();

    R_SUCCEED();
}

Result IApplicationDisplayService::CloseLayer(u64 layer_id) {
    LOG_DEBUG(Service_VI, "called. layer_id={}", layer_id);

    {
        std::scoped_lock lk{m_lock};
        R_UNLESS(m_open_layer_ids.contains(layer_id), VI::ResultNotFound);
        m_open_layer_ids.erase(layer_id);
    }

    R_RETURN(m_container->CloseLayer(layer_id));
}

Result IApplicationDisplayService::CreateStrayLayer(
    Out<u64> out_layer_id, Out<u64> out_size, OutBuffer<BufferAttr_HipcMapAlias> out_native_window,
    u32 flags, u64 display_id) {
    LOG_DEBUG(Service_VI, "called. flags={}, display_id={}", flags, display_id);

    s32 producer_binder_id;
    R_TRY(m_container->CreateStrayLayer(&producer_binder_id, out_layer_id, display_id));

    std::scoped_lock lk{m_lock};
    m_stray_layer_ids.insert(*out_layer_id);

    android::OutputParcel parcel;
    parcel.WriteInterface(NativeWindow{producer_binder_id});

    const auto buffer = parcel.Serialize();
    std::memcpy(out_native_window.data(), buffer.data(),
                (std::min)(out_native_window.size(), buffer.size()));

    *out_size = buffer.size();

    R_SUCCEED();
}

Result IApplicationDisplayService::DestroyStrayLayer(u64 layer_id) {
    LOG_DEBUG(Service_VI, "DestroyStrayLayer called. layer_id={}", layer_id);

    {
        std::scoped_lock lk{m_lock};
        R_UNLESS(m_stray_layer_ids.contains(layer_id), VI::ResultNotFound);
        m_stray_layer_ids.erase(layer_id);
    }

    R_RETURN(m_container->DestroyStrayLayer(layer_id));
}

Result IApplicationDisplayService::GetDisplayVsyncEvent(
    OutCopyHandle<Kernel::KReadableEvent> out_vsync_event, u64 display_id) {
    LOG_DEBUG(Service_VI, "called. display_id={}", display_id);

    std::scoped_lock lk{m_lock};

    auto [it, created] = m_display_vsync_events.emplace(display_id, m_context);
    R_UNLESS(created, VI::ResultPermissionDenied);

    m_container->LinkVsyncEvent(display_id, &it->second);
    *out_vsync_event = it->second.GetHandle();

    R_SUCCEED();
}

Result IApplicationDisplayService::ConvertScalingMode(Out<ConvertedScaleMode> out_scaling_mode,
                                                      NintendoScaleMode mode) {
    LOG_DEBUG(Service_VI, "called mode={}", mode);

    switch (mode) {
    case NintendoScaleMode::None:
        *out_scaling_mode = ConvertedScaleMode::None;
        R_SUCCEED();
    case NintendoScaleMode::Freeze:
        *out_scaling_mode = ConvertedScaleMode::Freeze;
        R_SUCCEED();
    case NintendoScaleMode::ScaleToWindow:
        *out_scaling_mode = ConvertedScaleMode::ScaleToWindow;
        R_SUCCEED();
    case NintendoScaleMode::ScaleAndCrop:
        *out_scaling_mode = ConvertedScaleMode::ScaleAndCrop;
        R_SUCCEED();
    case NintendoScaleMode::PreserveAspectRatio:
        *out_scaling_mode = ConvertedScaleMode::PreserveAspectRatio;
        R_SUCCEED();
    default:
        LOG_ERROR(Service_VI, "Invalid scaling mode specified, mode={}", mode);
        R_THROW(VI::ResultOperationFailed);
    }
}

Result IApplicationDisplayService::Cmd2103(Out<std::array<u8, 0x18>> out_unk18) {
    // HOS 21.0.0+ exposes this command with no input and exactly 0x18 output bytes,
    // but its public name/semantics remain unknown. Known compatibility implementations
    // return a zero payload. Keep the ABI deterministic while retaining AMBIGU status.
    *out_unk18 = {};
    LOG_WARNING(Service_VI, "Undocumented HOS21+ Cmd2103: returning zero compatibility payload");
    R_SUCCEED();
}

Result IApplicationDisplayService::GetIndirectLayerImageMap(
    Out<u64> out_size, Out<u64> out_stride,
    OutBuffer<BufferAttr_HipcMapTransferAllowsNonSecure | BufferAttr_HipcMapAlias> out_buffer,
    s64 width, s64 height, u64 indirect_layer_consumer_handle, u64 applet_resource_user_id,
    ClientProcessId process_id) {
    (void)out_size;
    (void)out_stride;
    R_UNLESS(width > 0 && height > 0, ResultOperationFailed);

    auto* window_system = system.GetAppletManager().GetWindowSystem();
    R_UNLESS(window_system != nullptr, ResultNotFound);

    const auto caller = window_system->GetByAppletResourceUserId(applet_resource_user_id);
    R_UNLESS(caller != nullptr && caller->process != nullptr, ResultPermissionDenied);
    R_UNLESS(process_id.pid != 0 && caller->process->GetProcessId() == process_id.pid,
             ResultPermissionDenied);

    const auto child = window_system->GetByAppletResourceUserId(indirect_layer_consumer_handle);
    R_UNLESS(child != nullptr, ResultNotFound);
    R_UNLESS(child->library_applet_mode ==
                 AM::LibraryAppletMode::PartialForegroundIndirectDisplay,
             ResultPermissionDenied);
    R_UNLESS(child->caller_applet.lock() == caller, ResultPermissionDenied);

    LOG_WARNING(Service_VI,
                "Indirect-layer image backend unavailable: caller_pid={}, caller_aruid={}, "
                "child_aruid={}, width={}, height={}, buffer_size={}",
                process_id.pid, applet_resource_user_id, indirect_layer_consumer_handle, width,
                height, out_buffer.size());
    R_THROW(ResultNotSupported);
}

Result IApplicationDisplayService::GetIndirectLayerImageCropMap(
    Out<u64> out_size, Out<u64> out_stride,
    OutBuffer<BufferAttr_HipcMapTransferAllowsNonSecure | BufferAttr_HipcMapAlias> out_buffer,
    float x1, float y1, float x2, float y2, s64 width, s64 height,
    u64 indirect_layer_consumer_handle, u64 applet_resource_user_id,
    ClientProcessId process_id) {
    LOG_DEBUG(Service_VI,
              "GetIndirectLayerImageCropMap crop=({}, {}, {}, {}), width={}, height={}", x1, y1,
              x2, y2, width, height);
    R_RETURN(GetIndirectLayerImageMap(out_size, out_stride, out_buffer, width, height,
                                      indirect_layer_consumer_handle, applet_resource_user_id,
                                      process_id));
}

Result IApplicationDisplayService::GetIndirectLayerImageRequiredMemoryInfo(Out<s64> out_size,
                                                                           Out<s64> out_alignment,
                                                                           s64 width, s64 height) {
    LOG_DEBUG(Service_VI, "called width={}, height={}", width, height);

    constexpr u64 base_size = 0x20000;
    const auto texture_size = width * height * 4;

    *out_alignment = 0x1000;
    *out_size = (texture_size + base_size - 1) / base_size * base_size;

    R_SUCCEED();
}

} // namespace Service::VI
