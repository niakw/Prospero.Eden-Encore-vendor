// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

// SPDX-FileCopyrightText: Copyright 2021 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "common/logging.h"
#include "common/settings.h"
#include "core/core.h"
#include "core/core_timing.h"
#include "core/hle/kernel/k_event.h"
#include "core/hle/kernel/k_readable_event.h"
#include "core/hle/kernel/k_shared_memory.h"
#include "core/hle/kernel/k_transfer_memory.h"
#include "core/hle/service/cmif_serialization.h"
#include "core/hle/service/hid/hidbus.h"
#include "core/hle/service/ipc_helpers.h"
#include "core/hle/service/service.h"
#include "core/memory.h"
#include "hid_core/hid_types.h"
#include "hid_core/hidbus/ringcon.h"
#include "hid_core/hidbus/starlink.h"
#include "hid_core/hidbus/stubbed.h"

namespace Service::HID {
// (15ms, 66Hz)
constexpr auto hidbus_update_ns = std::chrono::nanoseconds{15 * 1000 * 1000};

Hidbus::Hidbus(Core::System& system_)
    : ServiceFramework{system_, "hidbus"}, service_context{system_, service_name} {

    // clang-format off
    static const FunctionInfo functions[] = {
            {1, C<&Hidbus::GetBusHandle>, "GetBusHandle"},
            {2, C<&Hidbus::IsExternalDeviceConnected>, "IsExternalDeviceConnected"},
            {3, C<&Hidbus::Initialize>, "Initialize"},
            {4, C<&Hidbus::Finalize>, "Finalize"},
            {5, C<&Hidbus::EnableExternalDevice>, "EnableExternalDevice"},
            {6, C<&Hidbus::GetExternalDeviceId>, "GetExternalDeviceId"},
            {7, C<&Hidbus::SendCommandAsync>, "SendCommandAsync"},
            {8, C<&Hidbus::GetSendCommandAsynceResult>, "GetSendCommandAsynceResult"},
            {9, C<&Hidbus::SetEventForSendCommandAsycResult>, "SetEventForSendCommandAsycResult"},
            {10, C<&Hidbus::GetSharedMemoryHandle>, "GetSharedMemoryHandle"},
            {11, C<&Hidbus::EnableJoyPollingReceiveMode>, "EnableJoyPollingReceiveMode"},
            {12, C<&Hidbus::DisableJoyPollingReceiveMode>, "DisableJoyPollingReceiveMode"},
            {13, &Hidbus::RejectLegacyPollingData, "GetPollingData"},
            {14, C<&Hidbus::SetStatusManagerType>, "SetStatusManagerType"},
    };
    // clang-format on

    RegisterHandlers(functions);

    // Register update callbacks
    hidbus_update_event = Core::Timing::CreateEvent(
        "Hidbus::UpdateCallback",
        [this](s64 time,
               std::chrono::nanoseconds ns_late) -> std::optional<std::chrono::nanoseconds> {
            const auto guard = LockService();
            UpdateHidbus(ns_late);
            return std::nullopt;
        });

    system_.CoreTiming().ScheduleLoopingEvent(hidbus_update_ns, hidbus_update_ns,
                                              hidbus_update_event);
}

Hidbus::~Hidbus() {
    system.CoreTiming().UnscheduleEvent(hidbus_update_event);
    for (auto& entry : devices) {
        ReleasePollingMemory(entry);
        if (entry.device) {
            entry.device->DeactivateDevice();
        }
    }
}

void Hidbus::ReleasePollingMemory(HidbusDevice& entry) {
    if (entry.device) {
        entry.device->DisablePollingMode();
        entry.device->SetTransferMemoryAddress({}, nullptr);
    }
    if (entry.polling_memory) {
        entry.polling_memory->Close(system.Kernel());
        entry.polling_memory = nullptr;
    }
}

void Hidbus::UpdateHidbus(std::chrono::nanoseconds ns_late) {
    if (is_hidbus_enabled) {
        for (std::size_t i = 0; i < devices.size(); ++i) {
            if (devices[i].is_device_initialized && devices[i].device) {
                auto& device = devices[i].device;
                device->OnUpdate();
                auto& cur_entry = hidbus_status.entries[devices[i].handle.internal_index];
                cur_entry.is_polling_mode = device->IsPollingMode();
                cur_entry.polling_mode = device->GetPollingMode();
                cur_entry.is_enabled = device->IsEnabled();
                u8* shared_memory = system.Kernel().GetHidBusSharedMem().GetPointer();
                std::memcpy(shared_memory + (i * sizeof(HidbusStatusManagerEntry)), &cur_entry,
                            sizeof(cur_entry));
            }
        }
    }
}

std::optional<std::size_t> Hidbus::GetDeviceIndexFromHandle(BusHandle handle) const {
    if (!handle.is_valid) {
        return std::nullopt;
    }
    auto const it = std::ranges::find_if(devices, [&handle](auto const& e) {
        return handle.abstracted_pad_id == e.handle.abstracted_pad_id
            && handle.internal_index == e.handle.internal_index
            && handle.player_number == e.handle.player_number
            && handle.bus_type_id == e.handle.bus_type_id
            && handle.is_valid == e.handle.is_valid;
    });
    return it != devices.end()
        ? std::optional<std::size_t>{std::distance(devices.begin(), it)}
        : std::nullopt;
}

Result Hidbus::GetBusHandle(Out<bool> out_is_valid, Out<BusHandle> out_bus_handle, Core::HID::NpadIdType npad_id, BusType bus_type, AppletResourceUserId aruid) {
    LOG_INFO(Service_HID, "called, npad_id={}, bus_type={}, applet_resource_user_id={}", npad_id, bus_type, aruid.pid);
    *out_is_valid = false;
    *out_bus_handle = BusHandle{};
    if (auto const it = std::ranges::find_if(devices, [npad_id, bus_type](auto const& e) {
        return e.handle.is_valid && e.handle.player_number == u64(npad_id) && e.handle.bus_type_id == u8(bus_type);
    }); it != devices.end()) {
        *out_bus_handle = devices[std::distance(devices.begin(), it)].handle;
        *out_is_valid = true;
    // Handle not found. Create a new one
    } else  if (auto const free_it = std::ranges::find_if(devices, [](auto const& e) {
        return !e.handle.is_valid;
    }); free_it != devices.end()) {
        auto const i = std::distance(devices.begin(), free_it);
        devices[i].handle.raw = 0;
        devices[i].handle.abstracted_pad_id.Assign(i);
        devices[i].handle.internal_index.Assign(i);
        devices[i].handle.player_number.Assign(u8(npad_id));
        devices[i].handle.bus_type_id.Assign(u8(bus_type));
        devices[i].handle.is_valid.Assign(true);
        *out_bus_handle = devices[i].handle;
        *out_is_valid = true;
    } else {
        LOG_ERROR(Service_HID, "no free or matching handle found");
    }
    R_SUCCEED();
}

Result Hidbus::IsExternalDeviceConnected(Out<bool> out_is_connected, BusHandle bus_handle) {
    LOG_INFO(Service_HID,
             "Called, abstracted_pad_id={}, bus_type={}, internal_index={}, "
             "player_number={}, is_valid={}",
             bus_handle.abstracted_pad_id, bus_handle.bus_type_id, bus_handle.internal_index,
             bus_handle.player_number, bus_handle.is_valid);

    const auto device_index = GetDeviceIndexFromHandle(bus_handle);

    R_UNLESS(device_index.has_value(), ResultUnknown);
    R_UNLESS(devices[*device_index].is_device_initialized && devices[*device_index].device,
             ResultUnknown);

    *out_is_connected = devices[device_index.value()].device->IsDeviceActivated();
    R_SUCCEED();
}

Result Hidbus::Initialize(BusHandle bus_handle, AppletResourceUserId aruid) {
    LOG_INFO(Service_HID,
             "called, abstracted_pad_id={} bus_type={} internal_index={} "
             "player_number={} is_valid={}, applet_resource_user_id={}",
             bus_handle.abstracted_pad_id, bus_handle.bus_type_id, bus_handle.internal_index,
             bus_handle.player_number, bus_handle.is_valid, aruid.pid);

    const auto device_index = GetDeviceIndexFromHandle(bus_handle);
    R_UNLESS(device_index.has_value(), ResultUnknown);
    if (devices[*device_index].is_device_initialized) {
        R_SUCCEED();
    }

    is_hidbus_enabled = true;
    const auto entry_index = devices[device_index.value()].handle.internal_index;
    auto& cur_entry = hidbus_status.entries[entry_index];

    if (bus_handle.internal_index == 0 && Settings::values.enable_ring_controller) {
        MakeDevice<RingController>(bus_handle);
        devices[device_index.value()].is_device_initialized = true;
        devices[device_index.value()].device->ActivateDevice();
        cur_entry.is_in_focus = true;
        cur_entry.is_connected = true;
        cur_entry.is_connected_result = ResultSuccess;
        cur_entry.is_enabled = false;
        cur_entry.is_polling_mode = false;
    } else {
        MakeDevice<HidbusStubbed>(bus_handle);
        devices[device_index.value()].is_device_initialized = true;
        cur_entry.is_in_focus = true;
        cur_entry.is_connected = false;
        cur_entry.is_connected_result = ResultSuccess;
        cur_entry.is_enabled = false;
        cur_entry.is_polling_mode = false;
    }

    std::memcpy(system.Kernel().GetHidBusSharedMem().GetPointer(), &hidbus_status,
                sizeof(hidbus_status));
    R_SUCCEED();
}

Result Hidbus::Finalize(BusHandle bus_handle, AppletResourceUserId aruid) {
    LOG_INFO(Service_HID,
             "called, abstracted_pad_id={}, bus_type={}, internal_index={}, "
             "player_number={}, is_valid={}, applet_resource_user_id={}",
             bus_handle.abstracted_pad_id, bus_handle.bus_type_id, bus_handle.internal_index,
             bus_handle.player_number, bus_handle.is_valid, aruid.pid);

    const auto device_index = GetDeviceIndexFromHandle(bus_handle);

    R_UNLESS(device_index.has_value(), ResultUnknown);
    R_UNLESS(devices[*device_index].is_device_initialized && devices[*device_index].device,
             ResultUnknown);

    const auto entry_index = devices[device_index.value()].handle.internal_index;
    auto& cur_entry = hidbus_status.entries[entry_index];
    auto& device = devices[device_index.value()].device;
    devices[device_index.value()].is_device_initialized = false;
    ReleasePollingMemory(devices[*device_index]);
    device->DeactivateDevice();
    device.reset();

    cur_entry.is_in_focus = true;
    cur_entry.is_connected = false;
    cur_entry.is_connected_result = ResultSuccess;
    cur_entry.is_enabled = false;
    cur_entry.is_polling_mode = false;
    std::memcpy(system.Kernel().GetHidBusSharedMem().GetPointer(), &hidbus_status,
                sizeof(hidbus_status));
    R_SUCCEED();
}

Result Hidbus::EnableExternalDevice(bool is_enabled, BusHandle bus_handle, u64 inval,
                                    AppletResourceUserId aruid) {
    LOG_DEBUG(Service_HID,
              "called, enable={}, abstracted_pad_id={}, bus_type={}, internal_index={}, "
              "player_number={}, is_valid={}, inval={}, applet_resource_user_id{}",
              is_enabled, bus_handle.abstracted_pad_id, bus_handle.bus_type_id,
              bus_handle.internal_index, bus_handle.player_number, bus_handle.is_valid, inval,
              aruid.pid);

    const auto device_index = GetDeviceIndexFromHandle(bus_handle);

    R_UNLESS(device_index.has_value(), ResultUnknown);
    R_UNLESS(devices[*device_index].is_device_initialized && devices[*device_index].device,
             ResultUnknown);
    devices[device_index.value()].device->Enable(is_enabled);
    R_SUCCEED();
}

Result Hidbus::GetExternalDeviceId(Out<u32> out_device_id, BusHandle bus_handle) {
    LOG_DEBUG(Service_HID,
              "called, abstracted_pad_id={}, bus_type={}, internal_index={}, player_number={}, "
              "is_valid={}",
              bus_handle.abstracted_pad_id, bus_handle.bus_type_id, bus_handle.internal_index,
              bus_handle.player_number, bus_handle.is_valid);

    const auto device_index = GetDeviceIndexFromHandle(bus_handle);

    R_UNLESS(device_index.has_value(), ResultUnknown);
    R_UNLESS(devices[*device_index].is_device_initialized && devices[*device_index].device,
             ResultUnknown);

    *out_device_id = devices[device_index.value()].device->GetDeviceId();
    R_SUCCEED();
}

Result Hidbus::SendCommandAsync(BusHandle bus_handle,
                                InBuffer<BufferAttr_HipcAutoSelect> buffer_data) {
    LOG_DEBUG(Service_HID,
              "called, data_size={}, abstracted_pad_id={}, bus_type={}, internal_index={}, "
              "player_number={}, is_valid={}",
              buffer_data.size(), bus_handle.abstracted_pad_id, bus_handle.bus_type_id,
              bus_handle.internal_index, bus_handle.player_number, bus_handle.is_valid);

    const auto device_index = GetDeviceIndexFromHandle(bus_handle);

    R_UNLESS(device_index.has_value(), ResultUnknown);
    R_UNLESS(devices[*device_index].is_device_initialized && devices[*device_index].device,
             ResultUnknown);

    devices[device_index.value()].device->SetCommand(buffer_data);
    R_SUCCEED();
};

Result Hidbus::GetSendCommandAsynceResult(Out<u64> out_data_size, BusHandle bus_handle,
                                          OutBuffer<BufferAttr_HipcAutoSelect> out_buffer_data) {
    LOG_DEBUG(Service_HID,
              "called, abstracted_pad_id={}, bus_type={}, internal_index={}, player_number={}, "
              "is_valid={}",
              bus_handle.abstracted_pad_id, bus_handle.bus_type_id, bus_handle.internal_index,
              bus_handle.player_number, bus_handle.is_valid);

    const auto device_index = GetDeviceIndexFromHandle(bus_handle);

    R_UNLESS(device_index.has_value(), ResultUnknown);
    R_UNLESS(devices[*device_index].is_device_initialized && devices[*device_index].device,
             ResultUnknown);

    *out_data_size = devices[device_index.value()].device->GetReply(out_buffer_data);
    R_SUCCEED();
};

Result Hidbus::SetEventForSendCommandAsycResult(OutCopyHandle<Kernel::KReadableEvent> out_event,
                                                BusHandle bus_handle) {
    LOG_INFO(Service_HID,
             "called, abstracted_pad_id={}, bus_type={}, internal_index={}, player_number={}, "
             "is_valid={}",
             bus_handle.abstracted_pad_id, bus_handle.bus_type_id, bus_handle.internal_index,
             bus_handle.player_number, bus_handle.is_valid);

    const auto device_index = GetDeviceIndexFromHandle(bus_handle);

    R_UNLESS(device_index.has_value(), ResultUnknown);
    R_UNLESS(devices[*device_index].is_device_initialized && devices[*device_index].device,
             ResultUnknown);

    *out_event = &devices[device_index.value()].device->GetSendCommandAsycEvent();
    R_SUCCEED();
};

Result Hidbus::GetSharedMemoryHandle(OutCopyHandle<Kernel::KSharedMemory> out_shared_memory) {
    LOG_DEBUG(Service_HID, "called");

    *out_shared_memory = &system.Kernel().GetHidBusSharedMem();
    R_SUCCEED();
}

Result Hidbus::EnableJoyPollingReceiveMode(u32 t_mem_size, JoyPollingMode polling_mode,
                                           BusHandle bus_handle,
                                           InCopyHandle<Kernel::KTransferMemory> t_mem) {
    R_UNLESS(t_mem && t_mem_size == 0x1000 && t_mem->GetSize() == t_mem_size &&
                 t_mem->GetOwner() != nullptr,
             ResultUnknown);
    R_UNLESS(polling_mode == JoyPollingMode::SixAxisSensorDisable ||
                 polling_mode == JoyPollingMode::SixAxisSensorEnable ||
                 polling_mode == JoyPollingMode::ButtonOnly,
             ResultUnknown);

    LOG_INFO(Service_HID,
             "called, polling_mode={}, abstracted_pad_id={}, bus_type={}, "
             "internal_index={}, player_number={}, is_valid={}",
             polling_mode, bus_handle.abstracted_pad_id, bus_handle.bus_type_id,
             bus_handle.internal_index, bus_handle.player_number, bus_handle.is_valid);

    const auto device_index = GetDeviceIndexFromHandle(bus_handle);

    R_UNLESS(device_index.has_value(), ResultUnknown);
    R_UNLESS(devices[*device_index].is_device_initialized && devices[*device_index].device,
             ResultUnknown);

    // The IPC copy handle alone does not retain the memory after this request.
    // Keep the mapping and owning process alive until polling stops.
    R_UNLESS(t_mem->Open(system.Kernel()), ResultUnknown);
    auto& entry = devices[*device_index];
    ReleasePollingMemory(entry);
    entry.polling_memory = t_mem.Get();
    entry.device->SetTransferMemoryAddress(t_mem->GetSourceAddress(), t_mem->GetOwner());
    entry.device->SetPollingMode(polling_mode);
    R_SUCCEED();
}

Result Hidbus::DisableJoyPollingReceiveMode(BusHandle bus_handle) {
    LOG_INFO(Service_HID,
             "called, abstracted_pad_id={}, bus_type={}, internal_index={}, player_number={}, "
             "is_valid={}",
             bus_handle.abstracted_pad_id, bus_handle.bus_type_id, bus_handle.internal_index,
             bus_handle.player_number, bus_handle.is_valid);

    const auto device_index = GetDeviceIndexFromHandle(bus_handle);

    R_UNLESS(device_index.has_value(), ResultUnknown);
    R_UNLESS(devices[*device_index].is_device_initialized && devices[*device_index].device,
             ResultUnknown);

    ReleasePollingMemory(devices[*device_index]);
    R_SUCCEED();
}

void Hidbus::RejectLegacyPollingData(HLERequestContext& ctx) {
    // GetPollingData existed only on HOS 5.0.0-6.2.0. Modern Horizon uses
    // the polling transfer-memory/shared-memory path instead.
    IPC::ResponseBuilder rb{ctx, 2};
    rb.Push(IPC::ResultNotSupported);
}

Result Hidbus::SetStatusManagerType(StatusManagerType manager_type) {
    // The status-manager selection only affects Horizon's physical HID-bus manager. Encore has a
    // single local HID-bus status manager, so accepting the selector is an intentional no-op.
    LOG_DEBUG(Service_HID, "Status manager type ignored, manager_type={}", manager_type);
    R_SUCCEED();
};

} // namespace Service::HID
