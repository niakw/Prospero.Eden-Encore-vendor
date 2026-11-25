// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

// SPDX-FileCopyrightText: Copyright 2018 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "core/hle/service/spl/spl.h"

namespace Service::SPL {

SPL::SPL(Core::System& system_, std::shared_ptr<Module> module_)
    : Interface(system_, std::move(module_), "spl:") {
    // clang-format off
    static const FunctionInfo functions[] = {
        {0, &SPL::GetConfig, "GetConfig"},
        {1, &SPL::ModularExponentiate, "ModularExponentiate"},
        {5, &SPL::SetConfig, "SetConfig"},
        {7, &SPL::GenerateRandomBytes, "GenerateRandomBytes"},
        {11, &SPL::IsDevelopment, "IsDevelopment"},
        {24, &SPL::SetBootReason, "SetBootReason"},
        {25, &SPL::GetBootReason, "GetBootReason"},
    };
    // clang-format on

    RegisterHandlers(functions);
}

SPL::~SPL() = default;

SPL_MIG::SPL_MIG(Core::System& system_, std::shared_ptr<Module> module_)
    : Interface(system_, std::move(module_), "spl:mig") {
    // clang-format off
    static const FunctionInfo functions[] = {
        {0, &SPL::GetConfig, "GetConfig"},
        {1, &SPL::ModularExponentiate, "ModularExponentiate"},
        {2, &SPL::GenerateAesKek, "GenerateAesKek"},
        {3, &SPL::LoadAesKey, "LoadAesKey"},
        {4, &SPL::GenerateAesKey, "GenerateAesKey"},
        {5, &SPL::SetConfig, "SetConfig"},
        {7, &SPL::GenerateRandomBytes, "GenerateRandomBytes"},
        {11, &SPL::IsDevelopment, "IsDevelopment"},
        {14, &SPL::DecryptAesKey, "DecryptAesKey"},
        {15, &SPL::CryptAesCtr, "CryptAesCtr"},
        {16, &SPL::ComputeCmac, "ComputeCmac"},
        {21, &SPL::AllocateAesKeyslot, "AllocateAesKeyslot"},
        {22, &SPL::DeallocateAesKeySlot, "DeallocateAesKeySlot"},
        {23, &SPL::GetAesKeyslotAvailableEvent, "GetAesKeyslotAvailableEvent"},
        {24, &SPL::SetBootReason, "SetBootReason"},
        {25, &SPL::GetBootReason, "GetBootReason"},
    };
    // clang-format on

    RegisterHandlers(functions);
}

SPL_MIG::~SPL_MIG() = default;

SPL_FS::SPL_FS(Core::System& system_, std::shared_ptr<Module> module_)
    : Interface(system_, std::move(module_), "spl:fs") {
    // clang-format off
    static const FunctionInfo functions[] = {
        {0, &SPL::GetConfig, "GetConfig"},
        {1, &SPL::ModularExponentiate, "ModularExponentiate"},
        {2, &SPL::GenerateAesKek, "GenerateAesKek"},
        {3, &SPL::LoadAesKey, "LoadAesKey"},
        {4, &SPL::GenerateAesKey, "GenerateAesKey"},
        {5, &SPL::SetConfig, "SetConfig"},
        {7, &SPL::GenerateRandomBytes, "GenerateRandomBytes"},
        {9, &SPL::ImportLotusKey, "ImportLotusKey"},
        {10, &SPL::DecryptLotusMessage, "DecryptLotusMessage"},
        {11, &SPL::IsDevelopment, "IsDevelopment"},
        {12, &SPL::GenerateSpecificAesKey, "GenerateSpecificAesKey"},
        {14, &SPL::DecryptAesKey, "DecryptAesKey"},
        {15, &SPL::CryptAesCtr, "CryptAesCtr"},
        {16, &SPL::ComputeCmac, "ComputeCmac"},
        {19, &SPL::LoadTitleKey, "LoadTitleKey"},
        {21, &SPL::AllocateAesKeyslot, "AllocateAesKeyslot"},
        {22, &SPL::DeallocateAesKeySlot, "DeallocateAesKeySlot"},
        {23, &SPL::GetAesKeyslotAvailableEvent, "GetAesKeyslotAvailableEvent"},
        {24, &SPL::SetBootReason, "SetBootReason"},
        {25, &SPL::GetBootReason, "GetBootReason"},
        {31, &SPL::GetPackage2Hash, "GetPackage2Hash"},
    };
    // clang-format on

    RegisterHandlers(functions);
}

SPL_FS::~SPL_FS() = default;

SPL_SSL::SPL_SSL(Core::System& system_, std::shared_ptr<Module> module_)
    : Interface(system_, std::move(module_), "spl:ssl") {
    // clang-format off
    static const FunctionInfo functions[] = {
        {0, &SPL::GetConfig, "GetConfig"},
        {1, &SPL::ModularExponentiate, "ModularExponentiate"},
        {2, &SPL::GenerateAesKek, "GenerateAesKek"},
        {3, &SPL::LoadAesKey, "LoadAesKey"},
        {4, &SPL::GenerateAesKey, "GenerateAesKey"},
        {5, &SPL::SetConfig, "SetConfig"},
        {7, &SPL::GenerateRandomBytes, "GenerateRandomBytes"},
        {11, &SPL::IsDevelopment, "IsDevelopment"},
        {13, &SPL::DecryptDeviceUniqueData, "DecryptDeviceUniqueData"},
        {14, &SPL::DecryptAesKey, "DecryptAesKey"},
        {15, &SPL::CryptAesCtr, "CryptAesCtr"},
        {16, &SPL::ComputeCmac, "ComputeCmac"},
        {21, &SPL::AllocateAesKeyslot, "AllocateAesKeyslot"},
        {22, &SPL::DeallocateAesKeySlot, "DeallocateAesKeySlot"},
        {23, &SPL::GetAesKeyslotAvailableEvent, "GetAesKeyslotAvailableEvent"},
        {24, &SPL::SetBootReason, "SetBootReason"},
        {25, &SPL::GetBootReason, "GetBootReason"},
        {26, &SPL::ImportSecureExpModKey, "DecryptAndStoreSslClientCertKey"},
        {27, &SPL::SecureExpModWithStoredKey, "ModularExponentiateWithSslClientCertKey"},
    };
    // clang-format on

    RegisterHandlers(functions);
}

SPL_SSL::~SPL_SSL() = default;

SPL_ES::SPL_ES(Core::System& system_, std::shared_ptr<Module> module_)
    : Interface(system_, std::move(module_), "spl:es") {
    // clang-format off
    static const FunctionInfo functions[] = {
        {0, &SPL::GetConfig, "GetConfig"},
        {1, &SPL::ModularExponentiate, "ModularExponentiate"},
        {2, &SPL::GenerateAesKek, "GenerateAesKek"},
        {3, &SPL::LoadAesKey, "LoadAesKey"},
        {4, &SPL::GenerateAesKey, "GenerateAesKey"},
        {5, &SPL::SetConfig, "SetConfig"},
        {7, &SPL::GenerateRandomBytes, "GenerateRandomBytes"},
        {11, &SPL::IsDevelopment, "IsDevelopment"},
        {13, &SPL::DecryptDeviceUniqueData, "DecryptDeviceUniqueData"},
        {14, &SPL::DecryptAesKey, "DecryptAesKey"},
        {15, &SPL::CryptAesCtr, "CryptAesCtr"},
        {16, &SPL::ComputeCmac, "ComputeCmac"},
        {17, &SPL::ImportSecureExpModKey, "ImportEsKey"},
        {18, &SPL::UnwrapRsaOaepWrappedKey, "UnwrapTitleKey"},
        {20, &SPL::PrepareEsCommonKey, "PrepareEsCommonKey"},
        {21, &SPL::AllocateAesKeyslot, "AllocateAesKeyslot"},
        {22, &SPL::DeallocateAesKeySlot, "DeallocateAesKeySlot"},
        {23, &SPL::GetAesKeyslotAvailableEvent, "GetAesKeyslotAvailableEvent"},
        {24, &SPL::SetBootReason, "SetBootReason"},
        {25, &SPL::GetBootReason, "GetBootReason"},
        {28, &SPL::ImportSecureExpModKey, "DecryptAndStoreDrmDeviceCertKey"},
        {29, &SPL::SecureExpModWithStoredKey, "ModularExponentiateWithDrmDeviceCertKey"},
        {31, &SPL::UnwrapRsaOaepWrappedKey, "PrepareEsArchiveKey"},
        {32, &SPL::LoadPreparedAesKey, "LoadPreparedAesKey"},
    };
    // clang-format on

    RegisterHandlers(functions);
}

SPL_ES::~SPL_ES() = default;

SPL_MANU::SPL_MANU(Core::System& system_, std::shared_ptr<Module> module_)
    : Interface(system_, std::move(module_), "spl:manu") {
    // clang-format off
    static const FunctionInfo functions[] = {
        {0, &SPL::GetConfig, "GetConfig"},
        {1, &SPL::ModularExponentiate, "ModularExponentiate"},
        {2, &SPL::GenerateAesKek, "GenerateAesKek"},
        {3, &SPL::LoadAesKey, "LoadAesKey"},
        {4, &SPL::GenerateAesKey, "GenerateAesKey"},
        {5, &SPL::SetConfig, "SetConfig"},
        {7, &SPL::GenerateRandomBytes, "GenerateRandomBytes"},
        {11, &SPL::IsDevelopment, "IsDevelopment"},
        {13, &SPL::DecryptDeviceUniqueData, "DecryptDeviceUniqueData"},
        {14, &SPL::DecryptAesKey, "DecryptAesKey"},
        {15, &SPL::CryptAesCtr, "CryptAesCtr"},
        {16, &SPL::ComputeCmac, "ComputeCmac"},
        {21, &SPL::AllocateAesKeyslot, "AllocateAesKeyslot"},
        {22, &SPL::DeallocateAesKeySlot, "DeallocateAesKeySlot"},
        {23, &SPL::GetAesKeyslotAvailableEvent, "GetAesKeyslotAvailableEvent"},
        {24, &SPL::SetBootReason, "SetBootReason"},
        {25, &SPL::GetBootReason, "GetBootReason"},
        {30, &SPL::ReencryptDeviceUniqueData, "ReencryptDeviceUniqueData"},
    };
    // clang-format on

    RegisterHandlers(functions);
}

SPL_MANU::~SPL_MANU() = default;

} // namespace Service::SPL
