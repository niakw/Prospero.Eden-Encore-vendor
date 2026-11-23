// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: Copyright 2024 yuzu Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#include <algorithm>
#include <cstring>

#include "core/core.h"
#include "core/hle/kernel/k_event.h"
#include "core/hle/service/cmif_serialization.h"
#include "core/hle/service/ldn/ldn_results.h"
#include "core/hle/service/ldn/system_local_communication_service.h"
#include "core/internal_network/network.h"
#include "core/internal_network/network_interface.h"
#include "network/network.h"

#undef CreateEvent

namespace Service::LDN {

ISystemLocalCommunicationService::ISystemLocalCommunicationService(Core::System& system_)
    : ServiceFramework{system_, "ISystemLocalCommunicationService"},
      service_context{system, "ISystemLocalCommunicationService"},
      lan_discovery{} {
    // clang-format off
    static const FunctionInfo functions[] = {
        {0, D<&ISystemLocalCommunicationService::GetState>, "GetState"},
        {1, D<&ISystemLocalCommunicationService::GetNetworkInfo>, "GetNetworkInfo"},
        {2, D<&ISystemLocalCommunicationService::GetIpv4Address>, "GetIpv4Address"},
        {3, D<&ISystemLocalCommunicationService::GetDisconnectReason>, "GetDisconnectReason"},
        {4, D<&ISystemLocalCommunicationService::GetSecurityParameter>, "GetSecurityParameter"},
        {5, D<&ISystemLocalCommunicationService::GetNetworkConfig>, "GetNetworkConfig"},
        {100, D<&ISystemLocalCommunicationService::AttachStateChangeEvent>, "AttachStateChangeEvent"},
        {101, D<&ISystemLocalCommunicationService::GetNetworkInfoLatestUpdate>, "GetNetworkInfoLatestUpdate"},
        {102, D<&ISystemLocalCommunicationService::Scan>, "Scan"},
        {103, D<&ISystemLocalCommunicationService::ScanPrivate>, "ScanPrivate"},
        {104, nullptr, "SetWirelessControllerRestriction"},
        {200, D<&ISystemLocalCommunicationService::OpenAccessPoint>, "OpenAccessPoint"},
        {201, D<&ISystemLocalCommunicationService::CloseAccessPoint>, "CloseAccessPoint"},
        {202, D<&ISystemLocalCommunicationService::CreateNetwork>, "CreateNetwork"},
        {203, D<&ISystemLocalCommunicationService::CreateNetworkPrivate>, "CreateNetworkPrivate"},
        {204, D<&ISystemLocalCommunicationService::DestroyNetwork>, "DestroyNetwork"},
        {205, nullptr, "Reject"},
        {206, D<&ISystemLocalCommunicationService::SetAdvertiseData>, "SetAdvertiseData"},
        {207, nullptr, "SetStationAcceptPolicy"},
        {208, nullptr, "AddAcceptFilterEntry"},
        {209, nullptr, "ClearAcceptFilter"},
        {300, D<&ISystemLocalCommunicationService::OpenStation>, "OpenStation"},
        {301, D<&ISystemLocalCommunicationService::CloseStation>, "CloseStation"},
        {302, D<&ISystemLocalCommunicationService::Connect>, "Connect"},
        {303, nullptr, "ConnectPrivate"},
        {304, D<&ISystemLocalCommunicationService::Disconnect>, "Disconnect"},
        {400, D<&ISystemLocalCommunicationService::InitializeSystem>, "InitializeSystem"},
        {401, D<&ISystemLocalCommunicationService::FinalizeSystem>, "FinalizeSystem"},
        {402, nullptr, "SetOperationMode"},
        {403, C<&ISystemLocalCommunicationService::InitializeSystem2>, "InitializeSystem2"},
    };
    // clang-format on
    RegisterHandlers(functions);

    state_change_event =
        service_context.CreateEvent("ISystemLocalCommunicationService:StateChangeEvent");
}

ISystemLocalCommunicationService::~ISystemLocalCommunicationService() {
    if (is_initialized) {
        if (auto room_member = Network::GetRoomMember().lock()) {
            room_member->Unbind(ldn_packet_received);
        }
    }
    service_context.CloseEvent(state_change_event);
}

Result ISystemLocalCommunicationService::GetState(Out<State> out_state) {
    *out_state = is_initialized ? lan_discovery.GetState() : State::Error;
    R_SUCCEED();
}

Result ISystemLocalCommunicationService::GetNetworkInfo(
    OutLargeData<NetworkInfo, BufferAttr_HipcPointer> out_network_info) {
    R_RETURN(lan_discovery.GetNetworkInfo(*out_network_info));
}

Result ISystemLocalCommunicationService::GetIpv4Address(
    Out<Ipv4Address> out_current_address, Out<Ipv4Address> out_subnet_mask) {
    const auto network_interface = Network::GetSelectedNetworkInterface();
    R_UNLESS(network_interface.has_value(), ResultNoIpAddress);

    *out_current_address = {Network::TranslateIPv4(network_interface->ip_address)};
    *out_subnet_mask = {Network::TranslateIPv4(network_interface->subnet_mask)};
    if (auto room_member = Network::GetRoomMember().lock(); room_member && room_member->IsConnected()) {
        *out_current_address = room_member->GetFakeIpAddress();
    }
    std::reverse(std::begin(*out_current_address), std::end(*out_current_address));
    std::reverse(std::begin(*out_subnet_mask), std::end(*out_subnet_mask));
    R_SUCCEED();
}

Result ISystemLocalCommunicationService::GetDisconnectReason(
    Out<DisconnectReason> out_disconnect_reason) {
    *out_disconnect_reason = lan_discovery.GetDisconnectReason();
    R_SUCCEED();
}

Result ISystemLocalCommunicationService::GetSecurityParameter(
    Out<SecurityParameter> out_security_parameter) {
    NetworkInfo info{};
    R_TRY(lan_discovery.GetNetworkInfo(info));
    out_security_parameter->session_id = info.network_id.session_id;
    std::memcpy(out_security_parameter->data.data(), info.ldn.security_parameter.data(),
                sizeof(SecurityParameter::data));
    R_SUCCEED();
}

Result ISystemLocalCommunicationService::GetNetworkConfig(Out<NetworkConfig> out_network_config) {
    NetworkInfo info{};
    R_TRY(lan_discovery.GetNetworkInfo(info));
    out_network_config->intent_id = info.network_id.intent_id;
    out_network_config->channel = info.common.channel;
    out_network_config->node_count_max = info.ldn.node_count_max;
    out_network_config->local_communication_version = info.ldn.nodes[0].local_communication_version;
    R_SUCCEED();
}

Result ISystemLocalCommunicationService::AttachStateChangeEvent(
    OutCopyHandle<Kernel::KReadableEvent> out_event) {
    *out_event = &state_change_event->GetReadableEvent();
    R_SUCCEED();
}

Result ISystemLocalCommunicationService::GetNetworkInfoLatestUpdate(
    OutLargeData<NetworkInfo, BufferAttr_HipcPointer> out_network_info,
    OutArray<NodeLatestUpdate, BufferAttr_HipcPointer> out_node_latest_update) {
    R_UNLESS(!out_node_latest_update.empty(), ResultBadInput);
    R_RETURN(lan_discovery.GetNetworkInfo(*out_network_info, out_node_latest_update));
}

Result ISystemLocalCommunicationService::Scan(
    Out<s16> network_count, WifiChannel channel, const ScanFilter& scan_filter,
    OutArray<NetworkInfo, BufferAttr_HipcAutoSelect> out_network_info) {
    R_UNLESS(!out_network_info.empty(), ResultBadInput);
    R_RETURN(lan_discovery.Scan(out_network_info, *network_count, scan_filter));
}

Result ISystemLocalCommunicationService::ScanPrivate(
    Out<s16> network_count, WifiChannel channel, const ScanFilter& scan_filter,
    OutArray<NetworkInfo, BufferAttr_HipcAutoSelect> out_network_info) {
    R_UNLESS(out_network_info.empty(), ResultBadInput);
    R_RETURN(lan_discovery.Scan(out_network_info, *network_count, scan_filter));
}

Result ISystemLocalCommunicationService::OpenAccessPoint() {
    R_RETURN(lan_discovery.OpenAccessPoint());
}

Result ISystemLocalCommunicationService::CloseAccessPoint() {
    R_RETURN(lan_discovery.CloseAccessPoint());
}

Result ISystemLocalCommunicationService::CreateNetwork(const CreateNetworkConfig& create_config) {
    R_RETURN(lan_discovery.CreateNetwork(create_config.security_config, create_config.user_config,
                                         create_config.network_config));
}

Result ISystemLocalCommunicationService::CreateNetworkPrivate(
    const CreateNetworkConfigPrivate& create_config,
    InArray<AddressEntry, BufferAttr_HipcPointer> address_list) {
    R_RETURN(lan_discovery.CreateNetwork(create_config.security_config, create_config.user_config,
                                         create_config.network_config));
}

Result ISystemLocalCommunicationService::DestroyNetwork() {
    R_RETURN(lan_discovery.DestroyNetwork());
}

Result ISystemLocalCommunicationService::SetAdvertiseData(
    InBuffer<BufferAttr_HipcAutoSelect> buffer_data) {
    R_RETURN(lan_discovery.SetAdvertiseData(buffer_data));
}

Result ISystemLocalCommunicationService::OpenStation() {
    R_RETURN(lan_discovery.OpenStation());
}

Result ISystemLocalCommunicationService::CloseStation() {
    R_RETURN(lan_discovery.CloseStation());
}

Result ISystemLocalCommunicationService::Connect(
    const ConnectNetworkData& connect_data,
    InLargeData<NetworkInfo, BufferAttr_HipcPointer> network_info) {
    R_RETURN(lan_discovery.Connect(*network_info, connect_data.user_config,
                                   static_cast<u16>(connect_data.local_communication_version)));
}

Result ISystemLocalCommunicationService::Disconnect() {
    R_RETURN(lan_discovery.Disconnect());
}

Result ISystemLocalCommunicationService::InitializeSystem(ClientProcessId aruid) {
    const auto network_interface = Network::GetSelectedNetworkInterface();
    R_UNLESS(network_interface, ResultAirplaneModeEnabled);

    if (auto room_member = Network::GetRoomMember().lock()) {
        ldn_packet_received = room_member->BindOnLdnPacketReceived(
            [this](const Network::LDNPacket& packet) { OnLDNPacketReceived(packet); });
    } else {
        R_RETURN(ResultAirplaneModeEnabled);
    }

    R_TRY(lan_discovery.Initialize([&]() { OnEventFired(); }));
    is_initialized = true;
    R_SUCCEED();
}

Result ISystemLocalCommunicationService::FinalizeSystem() {
    if (auto room_member = Network::GetRoomMember().lock()) {
        room_member->Unbind(ldn_packet_received);
    }
    is_initialized = false;
    R_RETURN(lan_discovery.Finalize());
}

Result ISystemLocalCommunicationService::InitializeSystem2() {
    LOG_WARNING(Service_LDN, "(STUBBED) called; ABI parameters are not represented by this Eden interface");
    R_SUCCEED();
}

void ISystemLocalCommunicationService::OnLDNPacketReceived(const Network::LDNPacket& packet) {
    lan_discovery.ReceivePacket(packet);
}

void ISystemLocalCommunicationService::OnEventFired() {
    state_change_event->Signal(system.Kernel());
}

} // namespace Service::LDN
