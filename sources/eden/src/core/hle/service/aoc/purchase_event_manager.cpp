// SPDX-FileCopyrightText: Copyright 2018 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "core/hle/service/aoc/purchase_event_manager.h"
#include "core/hle/service/cmif_serialization.h"

namespace Service::AOC {

constexpr Result ResultNoPurchasedProductInfoAvailable{ErrorModule::NIMShop, 400};

IPurchaseEventManager::IPurchaseEventManager(Core::System& system_)
    : ServiceFramework{system_, "IPurchaseEventManager"}, service_context{system,
                                                                          "IPurchaseEventManager"} {
    // clang-format off
        static const FunctionInfo functions[] = {
            {0, D<&IPurchaseEventManager::SetDefaultDeliveryTarget>, "SetDefaultDeliveryTarget"},
            {1, D<&IPurchaseEventManager::SetDeliveryTarget>, "SetDeliveryTarget"},
            {2, D<&IPurchaseEventManager::GetPurchasedEvent>, "GetPurchasedEvent"},
            {3, D<&IPurchaseEventManager::PopPurchasedProductInfo>, "PopPurchasedProductInfo"},
            {4, D<&IPurchaseEventManager::PopPurchasedProductInfoWithUid>, "PopPurchasedProductInfoWithUid"},
        };
    // clang-format on

    RegisterHandlers(functions);

    purchased_event = service_context.CreateEvent("IPurchaseEventManager:PurchasedEvent");
}

IPurchaseEventManager::~IPurchaseEventManager() {
    service_context.CloseEvent(purchased_event);
}

Result IPurchaseEventManager::SetDefaultDeliveryTarget(
    ClientProcessId process_id, InBuffer<BufferAttr_HipcMapAlias> in_buffer) {
    LOG_DEBUG(Service_AOC,
              "called, process_id={} (Nintendo purchase transport unavailable; target ignored)",
              process_id.pid);

    // Encore has no Nintendo eShop purchase transport. With no purchase producer, the delivery
    // target has no observable effect and is intentionally accepted as a no-op.
    R_SUCCEED();
}

Result IPurchaseEventManager::SetDeliveryTarget(u64 unknown,
                                                InBuffer<BufferAttr_HipcMapAlias> in_buffer) {
    LOG_DEBUG(Service_AOC,
              "called, target={} (Nintendo purchase transport unavailable; target ignored)",
              unknown);

    // See SetDefaultDeliveryTarget: there is no remote purchase producer to route.
    R_SUCCEED();
}

Result IPurchaseEventManager::GetPurchasedEvent(OutCopyHandle<Kernel::KReadableEvent> out_event) {
    LOG_WARNING(Service_AOC, "called");

    *out_event = &purchased_event->GetReadableEvent();

    R_SUCCEED();
}

Result IPurchaseEventManager::PopPurchasedProductInfo() {
    LOG_DEBUG(Service_AOC, "called, purchase queue empty (no Nintendo purchase backend)");

    R_RETURN(ResultNoPurchasedProductInfoAvailable);
}

Result IPurchaseEventManager::PopPurchasedProductInfoWithUid() {
    LOG_DEBUG(Service_AOC, "called, purchase queue empty for uid (no Nintendo purchase backend)");

    R_RETURN(ResultNoPurchasedProductInfoAvailable);
}

} // namespace Service::AOC
