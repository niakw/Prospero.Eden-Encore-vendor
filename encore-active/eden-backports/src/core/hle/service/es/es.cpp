// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

// SPDX-FileCopyrightText: Copyright 2018 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <algorithm>
#include <type_traits>
#include <variant>
#include <vector>

#include "core/crypto/key_manager.h"
#include "core/hle/service/es/es.h"
#include "core/hle/service/ipc_helpers.h"
#include "core/hle/service/server_manager.h"
#include "core/hle/service/service.h"
#include "frontend_common/firmware_manager.h"

namespace Service::ES {

constexpr Result ERROR_INVALID_ARGUMENT{ErrorModule::ETicket, 2};
constexpr Result ERROR_INVALID_RIGHTS_ID{ErrorModule::ETicket, 3};

class ETicket final : public ServiceFramework<ETicket> {
public:
    explicit ETicket(Core::System& system_) : ServiceFramework{system_, "es"} {
        // clang-format off
        static const FunctionInfo functions[] = {
            {1, &ETicket::ImportTicket, "ImportTicket"},
            {2, &ETicket::RejectUnavailableTicketManagement, "ImportTicketCertificateSet"},
            {3, &ETicket::RejectUnavailableTicketManagement, "DeleteTicket"},
            {4, &ETicket::RejectUnavailableTicketManagement, "DeletePersonalizedTicket"},
            {5, &ETicket::RejectUnavailableTicketManagement, "DeleteAllCommonTicket"},
            {6, &ETicket::RejectUnavailableTicketManagement, "DeleteAllPersonalizedTicket"},
            {7, &ETicket::RejectUnavailableTicketManagement, "DeleteAllPersonalizedTicketEx"},
            {8, &ETicket::GetTitleKey, "GetTitleKey"},
            {9, &ETicket::CountCommonTicket, "CountCommonTicket"},
            {10, &ETicket::CountPersonalizedTicket, "CountPersonalizedTicket"},
            {11, &ETicket::ListCommonTicketRightsIds, "ListCommonTicketRightsIds"},
            {12, &ETicket::ListPersonalizedTicketRightsIds, "ListPersonalizedTicketRightsIds"},
            {13, &ETicket::RejectUnavailableTicketManagement, "ListMissingPersonalizedTicket"},
            {14, &ETicket::GetCommonTicketSize, "GetCommonTicketSize"},
            {15, &ETicket::GetPersonalizedTicketSize, "GetPersonalizedTicketSize"},
            {16, &ETicket::GetCommonTicketData, "GetCommonTicketData"},
            {17, &ETicket::GetPersonalizedTicketData, "GetPersonalizedTicketData"},
            {18, &ETicket::RejectUnavailableTicketManagement, "OwnTicket"},
            {19, &ETicket::RejectUnavailableTicketManagement, "GetTicketInfo"},
            {20, &ETicket::RejectUnavailableTicketManagement, "ListLightTicketInfo"},
            {21, &ETicket::RejectUnavailableTicketManagement, "SignData"},
            {22, &ETicket::RejectUnavailableTicketManagement, "GetCommonTicketAndCertificateSize"},
            {23, &ETicket::RejectUnavailableTicketManagement, "GetCommonTicketAndCertificateData"},
            {24, &ETicket::RejectUnavailableTicketManagement, "ImportPrepurchaseRecord"},
            {25, &ETicket::RejectUnavailableTicketManagement, "DeletePrepurchaseRecord"},
            {26, &ETicket::RejectUnavailableTicketManagement, "DeleteAllPrepurchaseRecord"},
            {27, &ETicket::RejectUnavailableTicketManagement, "CountPrepurchaseRecord"},
            {28, &ETicket::RejectUnavailableTicketManagement, "ListPrepurchaseRecordRightsIds"},
            {29, &ETicket::RejectUnavailableTicketManagement, "ListPrepurchaseRecordInfo"},
            {30, &ETicket::CountTicket, "CountTicket"},
            {31, &ETicket::ListTicketRightsIds, "ListTicketRightsIds"},
            {32, &ETicket::RejectUnavailableTicketManagement, "CountPrepurchaseRecordEx"},
            {33, &ETicket::RejectUnavailableTicketManagement, "ListPrepurchaseRecordRightsIdsEx"},
            {34, &ETicket::RejectUnavailableTicketManagement, "GetEncryptedTicketSize"},
            {35, &ETicket::RejectUnavailableTicketManagement, "GetEncryptedTicketData"},
            {36, &ETicket::RejectUnavailableTicketManagement, "DeleteAllInactiveELicenseRequiredPersonalizedTicket"},
            {37, &ETicket::RejectUnavailableTicketManagement, "OwnTicket2"},
            {38, &ETicket::RejectUnavailableTicketManagement, "OwnTicket3"},
            {39, &ETicket::RejectUnavailableTicketManagement, "DeleteAllInactivePersonalizedTicket"},
            {40, &ETicket::RejectUnavailableTicketManagement, "DeletePrepurchaseRecordByNintendoAccountId"},
            {101, &ETicket::ReturnDocumentedUnavailableResult, "Unknown101"}, //18.0.0+
            {102, &ETicket::ReturnDocumentedUnavailableResult, "Unknown102"}, //18.0.0+
            {103, &ETicket::ReturnDocumentedUnavailableResult, "Unknown103"}, //18.0.0+
            {104, &ETicket::ReturnDocumentedUnavailableResult, "Unknown104"}, //18.0.0+
            {105, &ETicket::ReturnDocumentedUnavailableResult, "Unknown105"}, //20.0.0+
            {201, &ETicket::ReturnDocumentedUnavailableResult, "Unknown201"}, //18.0.0+
            {202, &ETicket::ReturnDocumentedUnavailableResult, "Unknown202"}, //18.0.0+
            {203, &ETicket::ReturnDocumentedUnavailableResult, "Unknown203"}, //18.0.0+
            {204, &ETicket::ReturnDocumentedUnavailableResult, "Unknown204"}, //18.0.0+
            {205, nullptr, "Unknown205"}, //18.0.0+
            {501, nullptr, "Unknown501"},
            {502, nullptr, "Unknown502"},
            {503, nullptr, "GetTitleKey"},
            {504, nullptr, "Unknown504"},
            {508, nullptr, "Unknown508"},
            {509, nullptr, "Unknown509"},
            {510, nullptr, "Unknown510"},
            {511, nullptr, "Unknown511"},
            {1001, nullptr, "Unknown1001"},
            {1002, nullptr, "Unknown1001"},
            {1003, nullptr, "Unknown1003"},
            {1004, nullptr, "Unknown1004"},
            {1005, nullptr, "Unknown1005"},
            {1006, nullptr, "Unknown1006"},
            {1007, nullptr, "Unknown1007"},
            {1009, nullptr, "Unknown1009"},
            {1010, nullptr, "Unknown1010"},
            {1011, nullptr, "Unknown1011"},
            {1012, nullptr, "Unknown1012"},
            {1013, nullptr, "Unknown1013"},
            {1014, nullptr, "Unknown1014"},
            {1015, nullptr, "Unknown1015"},
            {1016, nullptr, "Unknown1016"},
            {1017, nullptr, "Unknown1017"},
            {1018, nullptr, "Unknown1018"},
            {1019, nullptr, "Unknown1019"},
            {1020, nullptr, "Unknown1020"},
            {1021, nullptr, "Unknown1021"},
            {1501, nullptr, "Unknown1501"},
            {1502, nullptr, "Unknown1502"},
            {1503, nullptr, "Unknown1503"},
            {1504, nullptr, "Unknown1504"},
            {1505, nullptr, "Unknown1505"},
            {1506, nullptr, "Unknown1506"},
            {2000, nullptr, "Unknown2000"},
            {2001, nullptr, "Unknown2001"},
            {2002, nullptr, "Unknown2002"},
            {2003, nullptr, "Unknown2003"},
            {2100, nullptr, "Unknown2100"},
            {2501, nullptr, "Unknown2501"},
            {2502, nullptr, "Unknown2502"},
            {2601, nullptr, "Unknown2601"},
            {3001, nullptr, "Unknown3001"},
            {3002, nullptr, "Unknown3002"},
        };
        // clang-format on
        RegisterHandlers(functions);

        keys.PopulateTickets();
        keys.SynthesizeTickets();
    }

private:
    // Horizon returns 0x3F4A91 for these 18.0+/20.0+ commands (SwitchBrew ETicket).
    // Preserve the observed failure rather than reporting success or a generic IPC error.
    void ReturnDocumentedUnavailableResult(HLERequestContext& ctx) {
        IPC::ResponseBuilder rb{ctx, 2};
        rb.Push(Result{0x3F4A91});
    }

    // Switch eTicket mutation and prepurchase service lacks an authenticated backend.
    // Preserve local ticket reads and title-key access on the existing KeyManager.
    void RejectUnavailableTicketManagement(HLERequestContext& ctx) {
        IPC::ResponseBuilder rb{ctx, 2};
        rb.Push(ResultUnknown);
    }

    bool CheckRightsId(HLERequestContext& ctx, const u128& rights_id) {
        if (rights_id == u128{}) {
            LOG_ERROR(Service_ETicket, "The rights ID was invalid!");
            IPC::ResponseBuilder rb{ctx, 2};
            rb.Push(ERROR_INVALID_RIGHTS_ID);
            return false;
        }

        return true;
    }

    void ImportTicket(HLERequestContext& ctx) {
        const auto raw_ticket = ctx.ReadBuffer();
        [[maybe_unused]] const auto cert = ctx.ReadBuffer(1);

        if (raw_ticket.size() < sizeof(Core::Crypto::SignatureType)) {
            LOG_ERROR(Service_ETicket, "The input buffer is not large enough!");
            IPC::ResponseBuilder rb{ctx, 2};
            rb.Push(ERROR_INVALID_ARGUMENT);
            return;
        }

        Core::Crypto::Ticket ticket = Core::Crypto::Ticket::Read(raw_ticket);
        if (!ticket.IsValid() || raw_ticket.size() < ticket.GetSize() ||
            !keys.AddTicket(ticket)) {
            LOG_ERROR(Service_ETicket, "The ticket could not be imported!");
            IPC::ResponseBuilder rb{ctx, 2};
            rb.Push(ERROR_INVALID_ARGUMENT);
            return;
        }

        IPC::ResponseBuilder rb{ctx, 2};
        rb.Push(ResultSuccess);
    }

    void GetTitleKey(HLERequestContext& ctx) {
        IPC::RequestParser rp{ctx};
        const auto rights_id = rp.PopRaw<u128>();

        LOG_DEBUG(Service_ETicket, "called, rights_id={:016X}{:016X}", rights_id[1], rights_id[0]);

        if (!CheckRightsId(ctx, rights_id))
            return;

        const auto key =
            keys.GetKey(Core::Crypto::S128KeyType::Titlekey, rights_id[1], rights_id[0]);

        if (key == Core::Crypto::Key128{}) {
            LOG_ERROR(Service_ETicket,
                      "The titlekey doesn't exist in the KeyManager or the rights ID was invalid!");
            IPC::ResponseBuilder rb{ctx, 2};
            rb.Push(ERROR_INVALID_RIGHTS_ID);
            return;
        }

        ctx.WriteBuffer(key);

        IPC::ResponseBuilder rb{ctx, 2};
        rb.Push(ResultSuccess);
    }

    void CountCommonTicket(HLERequestContext& ctx) {
        keys.PopulateTickets();
        LOG_DEBUG(Service_ETicket, "called");

        const u32 count = static_cast<u32>(keys.GetCommonTickets().size());

        IPC::ResponseBuilder rb{ctx, 3};
        rb.Push(ResultSuccess);
        rb.Push<u32>(count);
    }

    void CountPersonalizedTicket(HLERequestContext& ctx) {
        keys.PopulateTickets();
        LOG_DEBUG(Service_ETicket, "called");

        const u32 count = static_cast<u32>(keys.GetPersonalizedTickets().size());

        IPC::ResponseBuilder rb{ctx, 3};
        rb.Push(ResultSuccess);
        rb.Push<u32>(count);
    }

    void CountTicket(HLERequestContext& ctx) {
        keys.PopulateTickets();
        const auto count = static_cast<u32>(
            keys.GetCommonTickets().size() + keys.GetPersonalizedTickets().size());
        IPC::ResponseBuilder rb{ctx, 3};
        rb.Push(ResultSuccess);
        rb.Push<u32>(count);
    }

    void ListTicketRightsIds(HLERequestContext& ctx) {
        keys.PopulateTickets();
        const auto& common = keys.GetCommonTickets();
        const auto& personalized = keys.GetPersonalizedTickets();
        std::vector<u128> ids;
        ids.reserve(common.size() + personalized.size());
        for (const auto& [rights_id, ticket] : common) {
            ids.push_back(rights_id);
        }
        for (const auto& [rights_id, ticket] : personalized) {
            ids.push_back(rights_id);
        }
        std::sort(ids.begin(), ids.end());
        const auto count = (std::min)(ids.size(), ctx.GetWriteBufferNumElements<u128>());
        if (count != 0) {
            ctx.WriteBuffer(ids.data(), count * sizeof(u128));
        }
        IPC::ResponseBuilder rb{ctx, 3};
        rb.Push(ResultSuccess);
        rb.Push<u32>(static_cast<u32>(count));
    }

    void ListCommonTicketRightsIds(HLERequestContext& ctx) {
        keys.PopulateTickets();
        size_t out_entries = 0;
        if (!keys.GetCommonTickets().empty()) {
            out_entries = ctx.GetWriteBufferNumElements<u128>();
        }
        LOG_DEBUG(Service_ETicket, "called, entries={:016X}", out_entries);

        const auto& tickets = keys.GetCommonTickets();
        std::vector<u128> ids;
        std::transform(tickets.begin(), tickets.end(), std::back_inserter(ids),
                       [](const auto& pair) { return pair.first; });

        out_entries = (std::min)(ids.size(), out_entries);
        ctx.WriteBuffer(ids.data(), out_entries * sizeof(u128));

        IPC::ResponseBuilder rb{ctx, 3};
        rb.Push(ResultSuccess);
        rb.Push<u32>(static_cast<u32>(out_entries));
    }

    void ListPersonalizedTicketRightsIds(HLERequestContext& ctx) {
        keys.PopulateTickets();
        size_t out_entries = 0;
        if (!keys.GetPersonalizedTickets().empty()) {
            out_entries = ctx.GetWriteBufferNumElements<u128>();
        }

        LOG_DEBUG(Service_ETicket, "called, entries={:016X}", out_entries);

        const auto& tickets = keys.GetPersonalizedTickets();
        std::vector<u128> ids;
        std::transform(tickets.begin(), tickets.end(), std::back_inserter(ids),
                       [](const auto& pair) { return pair.first; });

        out_entries = (std::min)(ids.size(), out_entries);
        ctx.WriteBuffer(ids.data(), out_entries * sizeof(u128));

        IPC::ResponseBuilder rb{ctx, 3};
        rb.Push(ResultSuccess);
        rb.Push<u32>(static_cast<u32>(out_entries));
    }

    void GetCommonTicketSize(HLERequestContext& ctx) {
        keys.PopulateTickets();
        IPC::RequestParser rp{ctx};
        const auto rights_id = rp.PopRaw<u128>();

        LOG_DEBUG(Service_ETicket, "called, rights_id={:016X}{:016X}", rights_id[1], rights_id[0]);

        if (!CheckRightsId(ctx, rights_id))
            return;

        const auto& tickets = keys.GetCommonTickets();
        const auto ticket_it = tickets.find(rights_id);
        if (ticket_it == tickets.end() || !ticket_it->second.IsValid()) {
            IPC::ResponseBuilder rb{ctx, 2};
            rb.Push(ERROR_INVALID_RIGHTS_ID);
            return;
        }
        const auto& ticket = ticket_it->second;

        IPC::ResponseBuilder rb{ctx, 4};
        rb.Push(ResultSuccess);
        rb.Push<u64>(ticket.GetSize());
    }

    void GetPersonalizedTicketSize(HLERequestContext& ctx) {
        keys.PopulateTickets();
        IPC::RequestParser rp{ctx};
        const auto rights_id = rp.PopRaw<u128>();

        LOG_DEBUG(Service_ETicket, "called, rights_id={:016X}{:016X}", rights_id[1], rights_id[0]);

        if (!CheckRightsId(ctx, rights_id))
            return;

        const auto& tickets = keys.GetPersonalizedTickets();
        const auto ticket_it = tickets.find(rights_id);
        if (ticket_it == tickets.end() || !ticket_it->second.IsValid()) {
            IPC::ResponseBuilder rb{ctx, 2};
            rb.Push(ERROR_INVALID_RIGHTS_ID);
            return;
        }
        const auto& ticket = ticket_it->second;

        IPC::ResponseBuilder rb{ctx, 4};
        rb.Push(ResultSuccess);
        rb.Push<u64>(ticket.GetSize());
    }

    void GetCommonTicketData(HLERequestContext& ctx) {
        keys.PopulateTickets();
        IPC::RequestParser rp{ctx};
        const auto rights_id = rp.PopRaw<u128>();

        LOG_DEBUG(Service_ETicket, "called, rights_id={:016X}{:016X}", rights_id[1], rights_id[0]);

        if (!CheckRightsId(ctx, rights_id))
            return;

        const auto& tickets = keys.GetCommonTickets();
        const auto ticket_it = tickets.find(rights_id);
        if (ticket_it == tickets.end() || !ticket_it->second.IsValid()) {
            IPC::ResponseBuilder rb{ctx, 2};
            rb.Push(ERROR_INVALID_RIGHTS_ID);
            return;
        }
        const auto& ticket = ticket_it->second;

        const auto write_size = std::min<u64>(ticket.GetSize(), ctx.GetWriteBufferSize());
        std::visit(
            [&](const auto& ticket_variant) {
                using T = std::decay_t<decltype(ticket_variant)>;
                if constexpr (!std::is_same_v<T, std::monostate>) {
                    ctx.WriteBuffer(&ticket_variant, write_size);
                }
            },
            ticket.data);

        IPC::ResponseBuilder rb{ctx, 4};
        rb.Push(ResultSuccess);
        rb.Push<u64>(write_size);
    }

    void GetPersonalizedTicketData(HLERequestContext& ctx) {
        keys.PopulateTickets();
        IPC::RequestParser rp{ctx};
        const auto rights_id = rp.PopRaw<u128>();

        LOG_DEBUG(Service_ETicket, "called, rights_id={:016X}{:016X}", rights_id[1], rights_id[0]);

        if (!CheckRightsId(ctx, rights_id))
            return;

        const auto& tickets = keys.GetPersonalizedTickets();
        const auto ticket_it = tickets.find(rights_id);
        if (ticket_it == tickets.end() || !ticket_it->second.IsValid()) {
            IPC::ResponseBuilder rb{ctx, 2};
            rb.Push(ERROR_INVALID_RIGHTS_ID);
            return;
        }
        const auto& ticket = ticket_it->second;

        const auto write_size = std::min<u64>(ticket.GetSize(), ctx.GetWriteBufferSize());
        std::visit(
            [&](const auto& ticket_variant) {
                using T = std::decay_t<decltype(ticket_variant)>;
                if constexpr (!std::is_same_v<T, std::monostate>) {
                    ctx.WriteBuffer(&ticket_variant, write_size);
                }
            },
            ticket.data);

        IPC::ResponseBuilder rb{ctx, 4};
        rb.Push(ResultSuccess);
        rb.Push<u64>(write_size);
    }

    Core::Crypto::KeyManager& keys = Core::Crypto::KeyManager::Instance();
};

class NDRM_LU final : public ServiceFramework<NDRM_LU> {
public:
    explicit NDRM_LU(Core::System& system_)
        : ServiceFramework{system_, "ndrm:lu"} {
        // clang-format off
        static const FunctionInfo functions[] = {
            {1, &NDRM_LU::RejectUnavailableNdrm, "Cmd1"},
            {2, &NDRM_LU::RejectUnavailableNdrm, "Cmd2"},
            {3, &NDRM_LU::RejectUnavailableNdrm, "Cmd3"},
            {1000, &NDRM_LU::RejectUnavailableNdrm, "Cmd1000"},
            {8000, &NDRM_LU::RejectUnavailableNdrm, "Cmd8000"},
        };
        // clang-format on
        RegisterHandlers(functions);
    }
private:
    // NDRM relies on Nintendo entitlement services, not available to the PS5 guest.
    void RejectUnavailableNdrm(HLERequestContext& ctx) {
        IPC::ResponseBuilder rb{ctx, 2};
        rb.Push(ResultUnknown);
    }
};

class NDRM_LA final : public ServiceFramework<NDRM_LA> {
public:
    explicit NDRM_LA(Core::System& system_)
        : ServiceFramework{system_, "ndrm:la"} {
        // clang-format off
        static const FunctionInfo functions[] = {
            {1, &NDRM_LA::RejectUnavailableNdrm, "Cmd1"},
            {2, &NDRM_LA::RejectUnavailableNdrm, "Cmd2"},
            {3, &NDRM_LA::RejectUnavailableNdrm, "Cmd3"},
            {4, &NDRM_LA::RejectUnavailableNdrm, "Cmd4"},
            {5, &NDRM_LA::RejectUnavailableNdrm, "Cmd5"},
            {6, &NDRM_LA::RejectUnavailableNdrm, "Cmd6"},
            {7, &NDRM_LA::RejectUnavailableNdrm, "Cmd7"},
            {8, &NDRM_LA::RejectUnavailableNdrm, "Cmd8"},
            {9, &NDRM_LA::RejectUnavailableNdrm, "Cmd9"},
            {10, &NDRM_LA::RejectUnavailableNdrm, "Cmd10"},
            {11, &NDRM_LA::RejectUnavailableNdrm, "Cmd11"},
            {12, &NDRM_LA::RejectUnavailableNdrm, "Cmd12"},
            {13, &NDRM_LA::RejectUnavailableNdrm, "Cmd13"},
            {14, &NDRM_LA::RejectUnavailableNdrm, "Cmd14"},
            {15, &NDRM_LA::RejectUnavailableNdrm, "Cmd15"},
            {16, &NDRM_LA::RejectUnavailableNdrm, "Cmd16"},
            {17, &NDRM_LA::RejectUnavailableNdrm, "Cmd17"},
            {18, &NDRM_LA::RejectUnavailableNdrm, "Cmd18"},
            {19, &NDRM_LA::RejectUnavailableNdrm, "Cmd19"},
            {20, &NDRM_LA::RejectUnavailableNdrm, "Cmd20"},
            {21, &NDRM_LA::RejectUnavailableNdrm, "Cmd21"},
            {22, &NDRM_LA::RejectUnavailableNdrm, "Cmd22"},
            {23, &NDRM_LA::RejectUnavailableNdrm, "Cmd23"},
            {24, &NDRM_LA::RejectUnavailableNdrm, "Cmd24"},
            {25, &NDRM_LA::RejectUnavailableNdrm, "Cmd25"},
            {26, &NDRM_LA::RejectUnavailableNdrm, "Cmd26"},
            {27, &NDRM_LA::RejectUnavailableNdrm, "Cmd27"},
            {28, &NDRM_LA::RejectUnavailableNdrm, "Cmd28"},
            {29, &NDRM_LA::RejectUnavailableNdrm, "Cmd29"},
            {30, &NDRM_LA::RejectUnavailableNdrm, "Cmd30"},
            {31, &NDRM_LA::RejectUnavailableNdrm, "Cmd31"},
            {32, &NDRM_LA::RejectUnavailableNdrm, "Cmd32"},
            {33, &NDRM_LA::RejectUnavailableNdrm, "Cmd33"},
            {34, &NDRM_LA::RejectUnavailableNdrm, "Cmd34"},
            {35, &NDRM_LA::RejectUnavailableNdrm, "Cmd35"},
            {36, &NDRM_LA::RejectUnavailableNdrm, "Cmd36"},
            {37, &NDRM_LA::RejectUnavailableNdrm, "Cmd37"},
            {38, &NDRM_LA::RejectUnavailableNdrm, "Cmd38"},
            {39, &NDRM_LA::RejectUnavailableNdrm, "Cmd39"},
            {40, &NDRM_LA::RejectUnavailableNdrm, "Cmd40"},
            {42, &NDRM_LA::RejectUnavailableNdrm, "Cmd42"},
            {43, &NDRM_LA::RejectUnavailableNdrm, "Cmd43"},
            {44, &NDRM_LA::RejectUnavailableNdrm, "Cmd44"},
            {45, &NDRM_LA::RejectUnavailableNdrm, "Cmd45"},
            {46, &NDRM_LA::RejectUnavailableNdrm, "Cmd46"},
            {47, &NDRM_LA::RejectUnavailableNdrm, "Cmd47"},
            {48, &NDRM_LA::RejectUnavailableNdrm, "Cmd48"},
            {49, &NDRM_LA::RejectUnavailableNdrm, "Cmd49"},
            {50, &NDRM_LA::RejectUnavailableNdrm, "Cmd50"},
            {51, &NDRM_LA::RejectUnavailableNdrm, "Cmd51"},
            {8000, &NDRM_LA::RejectUnavailableNdrm, "Cmd8000"},
            {8001, &NDRM_LA::RejectUnavailableNdrm, "Cmd8001"},
            {8002, &NDRM_LA::RejectUnavailableNdrm, "Cmd8002"},
            {8003, &NDRM_LA::RejectUnavailableNdrm, "Cmd8003"},
        };
        // clang-format on
        RegisterHandlers(functions);
    }
private:
    // NDRM relies on Nintendo entitlement services, not available to the PS5 guest.
    void RejectUnavailableNdrm(HLERequestContext& ctx) {
        IPC::ResponseBuilder rb{ctx, 2};
        rb.Push(ResultUnknown);
    }
};

void LoopProcess(Core::System& system) {
    auto server_manager = std::make_unique<ServerManager>(system);

    server_manager->RegisterNamedService("es", std::make_shared<ETicket>(system));
    // +13.0.0
    if (FirmwareManager::GetFirmwareVersion(system).first.major >= 13) {
        server_manager->RegisterNamedService("ndrm:lu", std::make_shared<NDRM_LU>(system));
        server_manager->RegisterNamedService("ndrm:la", std::make_shared<NDRM_LA>(system));
    }
    ServerManager::RunServer(std::move(server_manager));
}

} // namespace Service::ES
