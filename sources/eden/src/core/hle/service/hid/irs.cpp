// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

// SPDX-FileCopyrightText: Copyright 2018 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <algorithm>
#include <random>

#include "core/core.h"
#include "core/core_timing.h"
#include "core/hle/kernel/k_shared_memory.h"
#include "core/hle/kernel/k_transfer_memory.h"
#include "core/hle/kernel/kernel.h"
#include "core/hle/service/cmif_serialization.h"
#include "core/hle/service/hid/irs.h"
#include "core/hle/service/ipc_helpers.h"
#include "core/memory.h"
#include "hid_core/frontend/emulated_controller.h"
#include "hid_core/hid_core.h"
#include "hid_core/hid_result.h"
#include "hid_core/hid_util.h"
#include "hid_core/resource_manager.h"
#include "hid_core/resources/npad/npad.h"
#include "hid_core/irsensor/clustering_processor.h"
#include "hid_core/irsensor/image_transfer_processor.h"
#include "hid_core/irsensor/ir_led_processor.h"
#include "hid_core/irsensor/moment_processor.h"
#include "hid_core/irsensor/pointing_processor.h"
#include "hid_core/irsensor/tera_plugin_processor.h"

namespace Service::IRS {

IRS::IRS(Core::System& system_) : ServiceFramework{system_, "irs"} {
    // clang-format off
    static const FunctionInfo functions[] = {
        {302, C<&IRS::ActivateIrsensor>, "ActivateIrsensor"},
        {303, C<&IRS::DeactivateIrsensor>, "DeactivateIrsensor"},
        {304, C<&IRS::GetIrsensorSharedMemoryHandle>, "GetIrsensorSharedMemoryHandle"},
        {305, C<&IRS::StopImageProcessor>, "StopImageProcessor"},
        {306, C<&IRS::RunMomentProcessor>, "RunMomentProcessor"},
        {307, C<&IRS::RunClusteringProcessor>, "RunClusteringProcessor"},
        {308, C<&IRS::RunImageTransferProcessor>, "RunImageTransferProcessor"},
        {309, C<&IRS::GetImageTransferProcessorState>, "GetImageTransferProcessorState"},
        {310, C<&IRS::RunTeraPluginProcessor>, "RunTeraPluginProcessor"},
        {311, C<&IRS::GetNpadIrCameraHandle>, "GetNpadIrCameraHandle"},
        {312, C<&IRS::RunPointingProcessor>, "RunPointingProcessor"},
        {313, C<&IRS::SuspendImageProcessor>, "SuspendImageProcessor"},
        {314, C<&IRS::CheckFirmwareVersion>, "CheckFirmwareVersion"},
        {315, C<&IRS::SetFunctionLevel>, "SetFunctionLevel"},
        {316, C<&IRS::RunImageTransferExProcessor>, "RunImageTransferExProcessor"},
        {317, C<&IRS::RunIrLedProcessor>, "RunIrLedProcessor"},
        {318, C<&IRS::StopImageProcessorAsync>, "StopImageProcessorAsync"},
        {319, C<&IRS::ActivateIrsensorWithFunctionLevel>, "ActivateIrsensorWithFunctionLevel"},
    };
    // clang-format on

    u8* raw_shared_memory = system.Kernel().GetIrsSharedMem().GetPointer();
    RegisterHandlers(functions);
    shared_memory = std::construct_at(reinterpret_cast<StatusManager*>(raw_shared_memory));

    npad_device = system.HIDCore().GetEmulatedController(Core::HID::NpadIdType::Player1);
}
IRS::~IRS() = default;

Result IRS::ActivateIrsensor(ClientAppletResourceUserId aruid) {
    // Encore does not expose physical Joy-Con IR-camera hardware. Activation is therefore an
    // intentional no-op: the emulated IRS shared-memory/service ABI remains available to titles.
    LOG_DEBUG(Service_IRS, "IR-camera activation ignored, applet_resource_user_id={}", aruid.pid);
    R_SUCCEED();
}

Result IRS::DeactivateIrsensor(ClientAppletResourceUserId aruid) {
    // No physical IR-camera resource is owned by Encore, so there is nothing to release.
    LOG_DEBUG(Service_IRS, "IR-camera deactivation ignored, applet_resource_user_id={}", aruid.pid);
    R_SUCCEED();
}

Result IRS::GetIrsensorSharedMemoryHandle(OutCopyHandle<Kernel::KSharedMemory> out_shared_memory,
                                          ClientAppletResourceUserId aruid) {
    LOG_DEBUG(Service_IRS, "called, applet_resource_user_id={}", aruid.pid);

    *out_shared_memory = &system.Kernel().GetIrsSharedMem();
    R_SUCCEED();
}

Result IRS::StopImageProcessor(Core::IrSensor::IrCameraHandle camera_handle,
                               ClientAppletResourceUserId aruid) {
    LOG_WARNING(Service_IRS,
                "(STUBBED) called, npad_type={}, npad_id={}, applet_resource_user_id={}",
                camera_handle.npad_type, camera_handle.npad_id, aruid.pid);

    R_TRY(IsIrCameraHandleValid(camera_handle));

    // TODO: Stop Image processor
    npad_device->SetPollingMode(Core::HID::EmulatedDeviceIndex::RightIndex,
                                Common::Input::PollingMode::Active);
    R_SUCCEED();
}

Result IRS::RunMomentProcessor(
    Core::IrSensor::IrCameraHandle camera_handle, ClientAppletResourceUserId aruid,
    const Core::IrSensor::PackedMomentProcessorConfig& processor_config) {
    LOG_WARNING(Service_IRS,
                "(STUBBED) called, npad_type={}, npad_id={}, applet_resource_user_id={}",
                camera_handle.npad_type, camera_handle.npad_id, aruid.pid);

    R_TRY(IsIrCameraHandleValid(camera_handle));

    auto& device = GetIrCameraSharedMemoryDeviceEntry(camera_handle);
    MakeProcessorWithCoreContext<MomentProcessor>(camera_handle, device);
    auto& image_transfer_processor = GetProcessor<MomentProcessor>(camera_handle);
    image_transfer_processor.SetConfig(processor_config);
    npad_device->SetPollingMode(Core::HID::EmulatedDeviceIndex::RightIndex,
                                Common::Input::PollingMode::IR);

    R_SUCCEED();
}

Result IRS::RunClusteringProcessor(
    Core::IrSensor::IrCameraHandle camera_handle, ClientAppletResourceUserId aruid,
    const Core::IrSensor::PackedClusteringProcessorConfig& processor_config) {
    LOG_WARNING(Service_IRS,
                "(STUBBED) called, npad_type={}, npad_id={}, applet_resource_user_id={}",
                camera_handle.npad_type, camera_handle.npad_id, aruid.pid);

    R_TRY(IsIrCameraHandleValid(camera_handle));

    auto& device = GetIrCameraSharedMemoryDeviceEntry(camera_handle);
    MakeProcessorWithCoreContext<ClusteringProcessor>(camera_handle, device);
    auto& image_transfer_processor = GetProcessor<ClusteringProcessor>(camera_handle);
    image_transfer_processor.SetConfig(processor_config);
    npad_device->SetPollingMode(Core::HID::EmulatedDeviceIndex::RightIndex,
                                Common::Input::PollingMode::IR);

    R_SUCCEED();
}

Result IRS::RunImageTransferProcessor(
    Core::IrSensor::IrCameraHandle camera_handle, ClientAppletResourceUserId aruid,
    const Core::IrSensor::PackedImageTransferProcessorConfig& processor_config,
    u64 transfer_memory_size, InCopyHandle<Kernel::KTransferMemory> t_mem) {

    ASSERT_MSG(t_mem->GetSize() == transfer_memory_size, "t_mem has incorrect size");

    LOG_INFO(Service_IRS,
             "called, npad_type={}, npad_id={}, transfer_memory_size={}, transfer_memory_size={}, "
             "applet_resource_user_id={}",
             camera_handle.npad_type, camera_handle.npad_id, transfer_memory_size, t_mem->GetSize(),
             aruid.pid);

    R_TRY(IsIrCameraHandleValid(camera_handle));

    auto& device = GetIrCameraSharedMemoryDeviceEntry(camera_handle);
    MakeProcessorWithCoreContext<ImageTransferProcessor>(camera_handle, device);
    auto& image_transfer_processor = GetProcessor<ImageTransferProcessor>(camera_handle);
    image_transfer_processor.SetConfig(processor_config);
    image_transfer_processor.SetTransferMemoryAddress(t_mem->GetSourceAddress(), t_mem->GetOwner());
    npad_device->SetPollingMode(Core::HID::EmulatedDeviceIndex::RightIndex,
                                Common::Input::PollingMode::IR);

    R_SUCCEED();
}

Result IRS::GetImageTransferProcessorState(
    Out<Core::IrSensor::ImageTransferProcessorState> out_state,
    Core::IrSensor::IrCameraHandle camera_handle, ClientAppletResourceUserId aruid,
    OutBuffer<BufferAttr_HipcMapAlias> out_buffer_data) {
    LOG_DEBUG(Service_IRS, "(STUBBED) called, npad_type={}, npad_id={}, applet_resource_user_id={}",
              camera_handle.npad_type, camera_handle.npad_id, aruid.pid);

    R_TRY(IsIrCameraHandleValid(camera_handle));

    const auto& device = GetIrCameraSharedMemoryDeviceEntry(camera_handle);

    R_TRY(IsIrCameraHandleValid(camera_handle));
    R_UNLESS(device.mode == Core::IrSensor::IrSensorMode::ImageTransferProcessor,
             InvalidProcessorState);

    *out_state = GetProcessor<ImageTransferProcessor>(camera_handle).GetState(out_buffer_data);

    R_SUCCEED();
}

Result IRS::RunTeraPluginProcessor(Core::IrSensor::IrCameraHandle camera_handle,
                                   Core::IrSensor::PackedTeraPluginProcessorConfig processor_config,
                                   ClientAppletResourceUserId aruid) {
    LOG_WARNING(Service_IRS,
                "(STUBBED) called, npad_type={}, npad_id={}, mode={}, mcu_version={}.{}, "
                "applet_resource_user_id={}",
                camera_handle.npad_type, camera_handle.npad_id, processor_config.mode,
                processor_config.required_mcu_version.major,
                processor_config.required_mcu_version.minor, aruid.pid);

    R_TRY(IsIrCameraHandleValid(camera_handle));

    auto& device = GetIrCameraSharedMemoryDeviceEntry(camera_handle);
    MakeProcessor<TeraPluginProcessor>(camera_handle, device);
    auto& image_transfer_processor = GetProcessor<TeraPluginProcessor>(camera_handle);
    image_transfer_processor.SetConfig(processor_config);
    npad_device->SetPollingMode(Core::HID::EmulatedDeviceIndex::RightIndex,
                                Common::Input::PollingMode::IR);

    R_SUCCEED();
}

Result IRS::GetNpadIrCameraHandle(Out<Core::IrSensor::IrCameraHandle> out_camera_handle,
                                  Core::HID::NpadIdType npad_id) {
    R_UNLESS(HID::IsNpadIdValid(npad_id), HID::ResultInvalidNpadId);

    *out_camera_handle = {
        .npad_id = static_cast<u8>(HID::NpadIdTypeToIndex(npad_id)),
        .npad_type = Core::HID::NpadStyleIndex::None,
    };

    LOG_INFO(Service_IRS, "called, npad_id={}, camera_npad_id={}, camera_npad_type={}", npad_id,
             out_camera_handle->npad_id, out_camera_handle->npad_type);

    R_SUCCEED();
}

Result IRS::RunPointingProcessor(
    Core::IrSensor::IrCameraHandle camera_handle,
    const Core::IrSensor::PackedPointingProcessorConfig& processor_config,
    ClientAppletResourceUserId aruid) {
    LOG_WARNING(
        Service_IRS,
        "(STUBBED) called, npad_type={}, npad_id={}, mcu_version={}.{}, applet_resource_user_id={}",
        camera_handle.npad_type, camera_handle.npad_id, processor_config.required_mcu_version.major,
        processor_config.required_mcu_version.minor, aruid.pid);

    R_TRY(IsIrCameraHandleValid(camera_handle));

    auto& device = GetIrCameraSharedMemoryDeviceEntry(camera_handle);
    MakeProcessor<PointingProcessor>(camera_handle, device);
    auto& image_transfer_processor = GetProcessor<PointingProcessor>(camera_handle);
    image_transfer_processor.SetConfig(processor_config);
    npad_device->SetPollingMode(Core::HID::EmulatedDeviceIndex::RightIndex,
                                Common::Input::PollingMode::IR);

    R_SUCCEED();
}

Result IRS::SuspendImageProcessor(Core::IrSensor::IrCameraHandle camera_handle,
                                  ClientAppletResourceUserId aruid) {
    LOG_DEBUG(Service_IRS,
              "IR image-processor suspend ignored, npad_type={}, npad_id={}, "
              "applet_resource_user_id={}",
              camera_handle.npad_type, camera_handle.npad_id, aruid.pid);

    R_TRY(IsIrCameraHandleValid(camera_handle));

    // Encore has no physical IRS capture thread to suspend. Processor state remains intact so a
    // later resume/run call can continue using the emulated shared-memory state.
    R_SUCCEED();
}

Result IRS::CheckFirmwareVersion(Core::IrSensor::IrCameraHandle camera_handle,
                                 Core::IrSensor::PackedMcuVersion mcu_version,
                                 ClientAppletResourceUserId aruid) {
    LOG_WARNING(
        Service_IRS,
        "(STUBBED) called, npad_type={}, npad_id={}, applet_resource_user_id={}, mcu_version={}.{}",
        camera_handle.npad_type, camera_handle.npad_id, aruid.pid, mcu_version.major,
        mcu_version.minor);

    R_TRY(IsIrCameraHandleValid(camera_handle));

    // TODO: Check firmware version

    R_SUCCEED();
}

Result IRS::SetFunctionLevel(Core::IrSensor::IrCameraHandle camera_handle,
                             Core::IrSensor::PackedFunctionLevel function_level,
                             ClientAppletResourceUserId aruid) {
    LOG_DEBUG(Service_IRS,
              "IR function level ignored, npad_type={}, npad_id={}, function_level={}, "
              "applet_resource_user_id={}",
              camera_handle.npad_type, camera_handle.npad_id, function_level.function_level,
              aruid.pid);

    R_TRY(IsIrCameraHandleValid(camera_handle));

    // Function-level negotiation controls physical MCU features that Encore does not expose.
    R_SUCCEED();
}

Result IRS::RunImageTransferExProcessor(
    Core::IrSensor::IrCameraHandle camera_handle, ClientAppletResourceUserId aruid,
    const Core::IrSensor::PackedImageTransferProcessorExConfig& processor_config,
    u64 transfer_memory_size, InCopyHandle<Kernel::KTransferMemory> t_mem) {

    ASSERT_MSG(t_mem->GetSize() == transfer_memory_size, "t_mem has incorrect size");

    LOG_INFO(Service_IRS,
             "called, npad_type={}, npad_id={}, transfer_memory_size={}, "
             "applet_resource_user_id={}",
             camera_handle.npad_type, camera_handle.npad_id, transfer_memory_size, aruid.pid);

    R_TRY(IsIrCameraHandleValid(camera_handle));

    auto& device = GetIrCameraSharedMemoryDeviceEntry(camera_handle);
    MakeProcessorWithCoreContext<ImageTransferProcessor>(camera_handle, device);
    auto& image_transfer_processor = GetProcessor<ImageTransferProcessor>(camera_handle);
    image_transfer_processor.SetConfig(processor_config);
    image_transfer_processor.SetTransferMemoryAddress(t_mem->GetSourceAddress(),
                                                      t_mem->GetOwner());
    npad_device->SetPollingMode(Core::HID::EmulatedDeviceIndex::RightIndex,
                                Common::Input::PollingMode::IR);

    R_SUCCEED();
}

Result IRS::RunIrLedProcessor(Core::IrSensor::IrCameraHandle camera_handle,
                              Core::IrSensor::PackedIrLedProcessorConfig processor_config,
                              ClientAppletResourceUserId aruid) {
    LOG_WARNING(Service_IRS,
                "(STUBBED) called, npad_type={}, npad_id={}, light_target={}, mcu_version={}.{} "
                "applet_resource_user_id={}",
                camera_handle.npad_type, camera_handle.npad_id, processor_config.light_target,
                processor_config.required_mcu_version.major,
                processor_config.required_mcu_version.minor, aruid.pid);

    R_TRY(IsIrCameraHandleValid(camera_handle));

    auto& device = GetIrCameraSharedMemoryDeviceEntry(camera_handle);
    MakeProcessor<IrLedProcessor>(camera_handle, device);
    auto& image_transfer_processor = GetProcessor<IrLedProcessor>(camera_handle);
    image_transfer_processor.SetConfig(processor_config);
    npad_device->SetPollingMode(Core::HID::EmulatedDeviceIndex::RightIndex,
                                Common::Input::PollingMode::IR);

    R_SUCCEED();
}

Result IRS::StopImageProcessorAsync(Core::IrSensor::IrCameraHandle camera_handle,
                                    ClientAppletResourceUserId aruid) {
    LOG_WARNING(Service_IRS,
                "(STUBBED) called, npad_type={}, npad_id={}, applet_resource_user_id={}",
                camera_handle.npad_type, camera_handle.npad_id, aruid.pid);

    R_TRY(IsIrCameraHandleValid(camera_handle));

    // TODO: Stop image processor async
    npad_device->SetPollingMode(Core::HID::EmulatedDeviceIndex::RightIndex,
                                Common::Input::PollingMode::Active);

    R_SUCCEED();
}

Result IRS::ActivateIrsensorWithFunctionLevel(Core::IrSensor::PackedFunctionLevel function_level,
                                              ClientAppletResourceUserId aruid) {
    // Same intentional no-op as ActivateIrsensor: the requested physical MCU function level has
    // no PS5 equivalent, while the IRS service ABI must remain callable by guest software.
    LOG_DEBUG(Service_IRS, "IR-camera activation level ignored, function_level={}, "
                           "applet_resource_user_id={}",
              function_level.function_level, aruid.pid);
    R_SUCCEED();
}

Result IRS::IsIrCameraHandleValid(const Core::IrSensor::IrCameraHandle& camera_handle) const {
    if (camera_handle.npad_id >
        static_cast<u8>(HID::NpadIdTypeToIndex(Core::HID::NpadIdType::Handheld))) {
        return InvalidIrCameraHandle;
    }
    if (camera_handle.npad_type != Core::HID::NpadStyleIndex::None) {
        return InvalidIrCameraHandle;
    }
    return ResultSuccess;
}

Core::IrSensor::DeviceFormat& IRS::GetIrCameraSharedMemoryDeviceEntry(
    const Core::IrSensor::IrCameraHandle& camera_handle) {
    const auto npad_id_max_index = static_cast<u8>(sizeof(StatusManager::device));
    ASSERT_MSG(camera_handle.npad_id < npad_id_max_index, "invalid npad_id");
    return shared_memory->device[camera_handle.npad_id];
}

IRS_SYS::IRS_SYS(Core::System& system_, std::shared_ptr<Service::HID::ResourceManager> resource_manager_)
    : ServiceFramework{system_, "irs:sys"}, resource_manager{std::move(resource_manager_)} {
    // clang-format off
    static const FunctionInfo functions[] = {
        {500, nullptr, "SetAppletResourceUserId"},
        {501, &IRS_SYS::RegisterAppletResourceUserId, "RegisterAppletResourceUserId"},
        {502, &IRS_SYS::UnregisterAppletResourceUserId, "UnregisterAppletResourceUserId"},
        {503, &IRS_SYS::EnableAppletToGetInput, "EnableAppletToGetInput"},
    };
    // clang-format on

    RegisterHandlers(functions);
}


void IRS_SYS::RegisterAppletResourceUserId(HLERequestContext& ctx) {
    IPC::RequestParser rp{ctx};
    struct Parameters {
        bool enable_input;
        INSERT_PADDING_WORDS_NOINIT(1);
        u64 applet_resource_user_id;
    };
    static_assert(sizeof(Parameters) == 0x10);

    const auto parameters = rp.PopRaw<Parameters>();
    const Result result = resource_manager->RegisterAppletResourceUserId(
        parameters.applet_resource_user_id, parameters.enable_input);

    IPC::ResponseBuilder rb{ctx, 2};
    rb.Push(result);
}

void IRS_SYS::UnregisterAppletResourceUserId(HLERequestContext& ctx) {
    IPC::RequestParser rp{ctx};
    const u64 applet_resource_user_id = rp.Pop<u64>();
    resource_manager->UnregisterAppletResourceUserId(applet_resource_user_id);

    IPC::ResponseBuilder rb{ctx, 2};
    rb.Push(ResultSuccess);
}

void IRS_SYS::EnableAppletToGetInput(HLERequestContext& ctx) {
    IPC::RequestParser rp{ctx};
    struct Parameters {
        bool is_enabled;
        INSERT_PADDING_WORDS_NOINIT(1);
        u64 applet_resource_user_id;
    };
    static_assert(sizeof(Parameters) == 0x10);

    const auto parameters = rp.PopRaw<Parameters>();
    resource_manager->EnableInput(parameters.applet_resource_user_id, parameters.is_enabled);
    resource_manager->GetNpad()->EnableAppletToGetInput(parameters.applet_resource_user_id);

    IPC::ResponseBuilder rb{ctx, 2};
    rb.Push(ResultSuccess);
}

IRS_SYS::~IRS_SYS() = default;

} // namespace Service::IRS
