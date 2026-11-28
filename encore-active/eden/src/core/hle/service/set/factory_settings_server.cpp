// SPDX-FileCopyrightText: Copyright 2018 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <algorithm>
#include <array>
#include <cstring>
#include <span>
#include <type_traits>

#include "core/core.h"
#include "core/hle/result.h"
#include "core/hle/service/ipc_helpers.h"
#include "core/file_sys/bis_factory.h"
#include "core/file_sys/errors.h"
#include "core/hle/service/cmif_serialization.h"
#include "core/hle/service/filesystem/filesystem.h"
#include "core/hle/service/set/factory_settings_server.h"

namespace Service::Set {

namespace {

Result OpenCalibrationBinary(FileSys::VirtualFile* out_file, Core::System& system) {
    R_TRY(system.GetFileSystemController().OpenBISPartitionStorage(
        out_file, FileSys::BisPartitionId::CalibrationBinary));
    R_UNLESS(*out_file, FileSys::ResultPathNotFound);
    R_UNLESS((*out_file)->GetSize() >= 0x40, FileSys::ResultOutOfRange);

    std::array<u8, 4> magic{};
    R_UNLESS((*out_file)->Read(magic.data(), magic.size(), 0) == magic.size(),
             FileSys::ResultOutOfRange);
    constexpr std::array<u8, 4> expected_magic{'C', 'A', 'L', '0'};
    R_UNLESS(magic == expected_magic, FileSys::ResultInvalidArgument);
    R_SUCCEED();
}

Result ReadCalibrationBytes(Core::System& system, std::size_t offset, std::span<u8> out) {
    FileSys::VirtualFile file;
    R_TRY(OpenCalibrationBinary(&file, system));
    R_UNLESS(offset <= file->GetSize() && out.size() <= file->GetSize() - offset,
             FileSys::ResultOutOfRange);
    R_UNLESS(file->Read(out.data(), out.size(), offset) == out.size(), FileSys::ResultOutOfRange);
    R_SUCCEED();
}

template <typename T>
Result ReadCalibrationValue(Core::System& system, std::size_t offset, Out<T> out) {
    static_assert(std::is_trivially_copyable_v<T>);
    R_RETURN(ReadCalibrationBytes(
        system, offset,
        std::span<u8>{reinterpret_cast<u8*>(out.Get()), sizeof(T)}));
}

template <std::size_t N>
Result ReadCalibrationField(Core::System& system, std::size_t offset, Out<std::array<u8, N>> out) {
    R_RETURN(ReadCalibrationBytes(system, offset, std::span<u8>{out->data(), out->size()}));
}

} // namespace


IFactorySettingsServer::IFactorySettingsServer(Core::System& system_)
    : ServiceFramework{system_, "set:cal"} {
    // clang-format off
    static const FunctionInfo functions[] = {
        {0, C<&IFactorySettingsServer::GetBluetoothBdAddress>, "GetBluetoothBdAddress"},
        {1, C<&IFactorySettingsServer::GetConfigurationId1>, "GetConfigurationId1"},
        {2, C<&IFactorySettingsServer::GetAccelerometerOffset>, "GetAccelerometerOffset"},
        {3, C<&IFactorySettingsServer::GetAccelerometerScale>, "GetAccelerometerScale"},
        {4, C<&IFactorySettingsServer::GetGyroscopeOffset>, "GetGyroscopeOffset"},
        {5, C<&IFactorySettingsServer::GetGyroscopeScale>, "GetGyroscopeScale"},
        {6, C<&IFactorySettingsServer::GetWirelessLanMacAddress>, "GetWirelessLanMacAddress"},
        {7, C<&IFactorySettingsServer::GetWirelessLanCountryCodeCount>, "GetWirelessLanCountryCodeCount"},
        {8, C<&IFactorySettingsServer::GetWirelessLanCountryCodes>, "GetWirelessLanCountryCodes"},
        {9, C<&IFactorySettingsServer::GetSerialNumber>, "GetSerialNumber"},
        {10, &IFactorySettingsServer::RejectUnsupportedFactoryCommand, "SetInitialSystemAppletProgramId"},
        {11, &IFactorySettingsServer::RejectUnsupportedFactoryCommand, "SetOverlayDispProgramId"},
        {12, C<&IFactorySettingsServer::GetBatteryLot>, "GetBatteryLot"},
        {14, &IFactorySettingsServer::RejectUnsupportedFactoryCommand, "GetEciDeviceCertificate"},
        {15, &IFactorySettingsServer::RejectUnsupportedFactoryCommand, "GetEticketDeviceCertificate"},
        {16, &IFactorySettingsServer::RejectUnsupportedFactoryCommand, "GetSslKey"},
        {17, &IFactorySettingsServer::RejectUnsupportedFactoryCommand, "GetSslCertificate"},
        {18, &IFactorySettingsServer::RejectUnsupportedFactoryCommand, "GetGameCardKey"},
        {19, &IFactorySettingsServer::RejectUnsupportedFactoryCommand, "GetGameCardCertificate"},
        {20, &IFactorySettingsServer::RejectUnsupportedFactoryCommand, "GetEciDeviceKey"},
        {21, &IFactorySettingsServer::RejectUnsupportedFactoryCommand, "GetEticketDeviceKey"},
        {22, C<&IFactorySettingsServer::GetSpeakerParameter>, "GetSpeakerParameter"},
        {23, C<&IFactorySettingsServer::GetLcdVendorId>, "GetLcdVendorId"},
        {24, &IFactorySettingsServer::RejectUnsupportedFactoryCommand, "GetEciDeviceCertificate2"},
        {25, &IFactorySettingsServer::RejectUnsupportedFactoryCommand, "GetEciDeviceKey2"},
        {26, &IFactorySettingsServer::RejectUnsupportedFactoryCommand, "GetAmiiboKey"},
        {27, &IFactorySettingsServer::RejectUnsupportedFactoryCommand, "GetAmiiboEcqvCertificate"},
        {28, &IFactorySettingsServer::RejectUnsupportedFactoryCommand, "GetAmiiboEcdsaCertificate"},
        {29, &IFactorySettingsServer::RejectUnsupportedFactoryCommand, "GetAmiiboEcqvBlsKey"},
        {30, &IFactorySettingsServer::RejectUnsupportedFactoryCommand, "GetAmiiboEcqvBlsCertificate"},
        {31, &IFactorySettingsServer::RejectUnsupportedFactoryCommand, "GetAmiiboEcqvBlsRootCertificate"},
        {32, C<&IFactorySettingsServer::GetUsbTypeCPowerSourceCircuitVersion>, "GetUsbTypeCPowerSourceCircuitVersion"},
        {33, C<&IFactorySettingsServer::GetAnalogStickModuleTypeL>, "GetAnalogStickModuleTypeL"},
        {34, C<&IFactorySettingsServer::GetAnalogStickModelParameterL>, "GetAnalogStickModelParameterL"},
        {35, C<&IFactorySettingsServer::GetAnalogStickFactoryCalibrationL>, "GetAnalogStickFactoryCalibrationL"},
        {36, C<&IFactorySettingsServer::GetAnalogStickModuleTypeR>, "GetAnalogStickModuleTypeR"},
        {37, C<&IFactorySettingsServer::GetAnalogStickModelParameterR>, "GetAnalogStickModelParameterR"},
        {38, C<&IFactorySettingsServer::GetAnalogStickFactoryCalibrationR>, "GetAnalogStickFactoryCalibrationR"},
        {39, C<&IFactorySettingsServer::GetConsoleSixAxisSensorModuleType>, "GetConsoleSixAxisSensorModuleType"},
        {40, C<&IFactorySettingsServer::GetConsoleSixAxisSensorHorizontalOffset>, "GetConsoleSixAxisSensorHorizontalOffset"},
        {41, C<&IFactorySettingsServer::GetBatteryVersion>, "GetBatteryVersion"},
        {42, &IFactorySettingsServer::RejectUnsupportedFactoryCommand, "GetDeviceId"},
        {43, &IFactorySettingsServer::RejectUnsupportedFactoryCommand, "GetConsoleSixAxisSensorMountType"},
    };
    // clang-format on

    RegisterHandlers(functions);
}

void IFactorySettingsServer::RejectUnsupportedFactoryCommand(HLERequestContext& ctx) {
    IPC::ResponseBuilder rb{ctx, 2};
    rb.Push(ResultUnknown);
}


Result IFactorySettingsServer::GetBluetoothBdAddress(Out<std::array<u8, 0x6>> out_value) {
    R_RETURN(ReadCalibrationField(system, 0x220, out_value));
}

Result IFactorySettingsServer::GetConfigurationId1(Out<std::array<u8, 0x1E>> out_value) {
    R_RETURN(ReadCalibrationField(system, 0x40, out_value));
}

Result IFactorySettingsServer::GetAccelerometerOffset(Out<std::array<u8, 0x6>> out_value) {
    R_RETURN(ReadCalibrationField(system, 0x230, out_value));
}

Result IFactorySettingsServer::GetAccelerometerScale(Out<std::array<u8, 0x6>> out_value) {
    R_RETURN(ReadCalibrationField(system, 0x238, out_value));
}

Result IFactorySettingsServer::GetGyroscopeOffset(Out<std::array<u8, 0x6>> out_value) {
    R_RETURN(ReadCalibrationField(system, 0x240, out_value));
}

Result IFactorySettingsServer::GetGyroscopeScale(Out<std::array<u8, 0x6>> out_value) {
    R_RETURN(ReadCalibrationField(system, 0x248, out_value));
}

Result IFactorySettingsServer::GetWirelessLanMacAddress(Out<std::array<u8, 0x6>> out_value) {
    R_RETURN(ReadCalibrationField(system, 0x210, out_value));
}

Result IFactorySettingsServer::GetWirelessLanCountryCodeCount(Out<s32> out_count) {
    std::array<u8, sizeof(u32)> raw_count{};
    R_TRY(ReadCalibrationBytes(system, 0x80, raw_count));
    u32 stored_count{};
    std::memcpy(&stored_count, raw_count.data(), sizeof(stored_count));
    *out_count = static_cast<s32>((std::min)(stored_count, 0x80U));
    R_SUCCEED();
}

Result IFactorySettingsServer::GetWirelessLanCountryCodes(
    Out<s32> out_count,
    OutArray<std::array<u8, 0x3>, BufferAttr_HipcPointer> out_codes) {
    std::array<u8, sizeof(u32)> raw_count{};
    R_TRY(ReadCalibrationBytes(system, 0x80, raw_count));
    u32 stored_count{};
    std::memcpy(&stored_count, raw_count.data(), sizeof(stored_count));

    const auto count = (std::min)({
        static_cast<std::size_t>(stored_count), std::size_t{0x80}, out_codes.size()});
    if (count != 0) {
        R_TRY(ReadCalibrationBytes(
            system, 0x88, std::span<u8>{reinterpret_cast<u8*>(out_codes.data()), count * 0x3}));
    }
    *out_count = static_cast<s32>(count);
    R_SUCCEED();
}

Result IFactorySettingsServer::GetSerialNumber(Out<std::array<u8, 0x18>> out_value) {
    R_RETURN(ReadCalibrationField(system, 0x250, out_value));
}

Result IFactorySettingsServer::GetBatteryLot(Out<std::array<u8, 0x18>> out_value) {
    R_RETURN(ReadCalibrationField(system, 0x2CE0, out_value));
}


Result IFactorySettingsServer::GetSpeakerParameter(Out<std::array<u8, 0x5A>> out_value) {
    R_RETURN(ReadCalibrationField(system, 0x2D00, out_value));
}

Result IFactorySettingsServer::GetLcdVendorId(Out<u32> out_value) {
    R_RETURN(ReadCalibrationValue(system, 0x3D60, out_value));
}

Result IFactorySettingsServer::GetUsbTypeCPowerSourceCircuitVersion(Out<u8> out_value) {
    R_RETURN(ReadCalibrationValue(system, 0x4210, out_value));
}

Result IFactorySettingsServer::GetAnalogStickModuleTypeL(Out<u8> out_value) {
    R_RETURN(ReadCalibrationValue(system, 0x4270, out_value));
}

Result IFactorySettingsServer::GetAnalogStickModelParameterL(
    Out<std::array<u8, 0x12>> out_value) {
    R_RETURN(ReadCalibrationField(system, 0x4280, out_value));
}

Result IFactorySettingsServer::GetAnalogStickFactoryCalibrationL(
    Out<std::array<u8, 0x9>> out_value) {
    R_RETURN(ReadCalibrationField(system, 0x42A0, out_value));
}

Result IFactorySettingsServer::GetAnalogStickModuleTypeR(Out<u8> out_value) {
    R_RETURN(ReadCalibrationValue(system, 0x42B0, out_value));
}

Result IFactorySettingsServer::GetAnalogStickModelParameterR(
    Out<std::array<u8, 0x12>> out_value) {
    R_RETURN(ReadCalibrationField(system, 0x42C0, out_value));
}

Result IFactorySettingsServer::GetAnalogStickFactoryCalibrationR(
    Out<std::array<u8, 0x9>> out_value) {
    R_RETURN(ReadCalibrationField(system, 0x42E0, out_value));
}

Result IFactorySettingsServer::GetConsoleSixAxisSensorModuleType(Out<u8> out_value) {
    R_RETURN(ReadCalibrationValue(system, 0x42F0, out_value));
}

Result IFactorySettingsServer::GetConsoleSixAxisSensorHorizontalOffset(
    Out<std::array<u8, 0x6>> out_value) {
    R_RETURN(ReadCalibrationField(system, 0x4300, out_value));
}

Result IFactorySettingsServer::GetBatteryVersion(Out<u8> out_value) {
    R_RETURN(ReadCalibrationValue(system, 0x4310, out_value));
}

IFactorySettingsServer::~IFactorySettingsServer() = default;

} // namespace Service::Set
