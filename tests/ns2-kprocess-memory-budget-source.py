#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Pin the narrow upstream NS2 KProcess memory-budget backport.

This is source qualification only. It proves the pre-#1773 KProcess successor
differs from the pinned Eden owner only by Eglantine/Lichii dce3adbc's
IsSwitch2 alias-region memory budget plus bounded PS5 stack/context receipts.
The current owner may additionally carry Lichii #1773 generation propagation;
that delta is reversed exactly below and independently qualified by
ns2_alias_128g_no_kprocess_layout_host.py.
"""
from pathlib import Path
import hashlib

ROOT = Path(__file__).resolve().parents[1]
BASE = ROOT / "sources/eden/src/core/hle/kernel/k_process.cpp"
ACTIVE = ROOT / "encore-active/ns2-runtime/process/core/hle/kernel/k_process.cpp"

base = BASE.read_text()
active = ACTIVE.read_text()

# PS5 receipt compile successor: KProcessAddress is Common::ProcessAddress, a
# TypedAddress with no integer conversion operator. Qualify Eden's supported
# accessor, then normalize only the five log operands in the audit copy so the
# historical dce3adbc / #1773 receipts remain byte-for-byte immutable.
typed = (ROOT / "sources/eden/src/common/typed_address.h").read_text()
assert "constexpr inline uint64_t GetInteger(Common::TypedAddress<Virtual, T> address)" in typed
assert "return address.GetValue();" in typed
typed_receipts = (
    ("static_cast<unsigned long long>(GetInteger(stack_bottom))",
     "static_cast<unsigned long long>(stack_bottom)", 1),
    ("static_cast<unsigned long long>(GetInteger(stack_top))",
     "static_cast<unsigned long long>(stack_top)", 2),
    ("static_cast<unsigned long long>(GetInteger(probe))",
     "static_cast<unsigned long long>(probe)", 1),
    ("static_cast<unsigned long long>(GetInteger(this->GetEntryPoint()))",
     "static_cast<unsigned long long>(this->GetEntryPoint())", 1),
)
for current, historical, expected_count in typed_receipts:
    assert active.count(current) == expected_count, (current, active.count(current))
    assert historical not in active
    active = active.replace(current, historical)
print("NS2_KPROCESS_TYPED_ADDRESS_RECEIPT_PASS", "operands=5", "api=GetInteger")

# Lichii #1773 is independently qualified and deliberately does not add a
# KProcess data member. Reverse its three KProcess-source edits exactly so
# this older dce3adbc receipt can retain byte-for-byte provenance.
alias_wrapper = """Result KProcess::Initialize(KernelCore& kernel, const Svc::CreateProcessParameter& params,
                            std::span<const u32> user_caps, KResourceLimit* res_limit,
                            KMemoryManager::Pool pool, KProcessAddress aslr_space_start) {
    R_RETURN(this->Initialize(kernel, params, user_caps, res_limit, pool, aslr_space_start, false));
}

Result KProcess::Initialize(KernelCore& kernel, const Svc::CreateProcessParameter& params,
                            std::span<const u32> user_caps, KResourceLimit* res_limit,
                            KMemoryManager::Pool pool, KProcessAddress aslr_space_start,
                            bool is_switch2) {
"""
prior_signature = """Result KProcess::Initialize(KernelCore& kernel, const Svc::CreateProcessParameter& params,
                            std::span<const u32> user_caps, KResourceLimit* res_limit,
                            KMemoryManager::Pool pool, KProcessAddress aslr_space_start) {
"""
alias_page_call = """        R_TRY(m_page_table.Initialize(as_type, enable_aslr, enable_das_merge, !enable_aslr, pool,
                                      params.code_address, code_size, m_system_resource, res_limit,
                                      m_memory, aslr_space_start, is_switch2));
"""
prior_page_call = """        R_TRY(m_page_table.Initialize(as_type, enable_aslr, enable_das_merge, !enable_aslr, pool,
                                      params.code_address, code_size, m_system_resource, res_limit,
                                      m_memory, aslr_space_start));
"""
alias_metadata_call = """    // Lichii #1773: generation changes only page-table region sizing. Pass it
    // explicitly instead of adding an m_is_switch2 field to KProcess; Encore's
    // shared 4/6-core ABI requires the KProcess object layout to remain identical.
    R_TRY(this->Initialize(kernel, params, metadata.GetKernelCapabilities(), res_limit, pool,
                           aslr_space_start, metadata.IsSwitch2()));
"""
prior_metadata_call = """    R_TRY(this->Initialize(kernel, params, metadata.GetKernelCapabilities(), res_limit, pool, aslr_space_start));
"""

assert active.count(alias_wrapper) == 1
assert active.count(alias_page_call) == 1
assert active.count(alias_metadata_call) == 1
# Encore intentionally does NOT take Lichii #1773's KProcess data member:
# the shared PS5 4/6-core ABI must retain the existing object layout. Comments
# may mention the upstream field, so test actual declarations/assignments.
assert "bool m_is_switch2" not in active
assert "m_is_switch2 =" not in active
assert active.count("metadata.IsSwitch2()") == 2

current_method = active.split("Result KProcess::LoadFromMetadata(", 1)[1].split(
    "\nvoid KProcess::LoadModule(", 1
)[0]
layout_generation = current_method.index(
    "aslr_space_start, metadata.IsSwitch2())"
)
budget_generation = current_method.index("if (metadata.IsSwitch2())")
assert layout_generation < budget_generation
assert budget_generation < current_method.index("InitializeInterfaces(kernel)")

pre_alias = active.replace(alias_wrapper, prior_signature, 1)
pre_alias = pre_alias.replace(alias_page_call, prior_page_call, 1)
pre_alias = pre_alias.replace(alias_metadata_call, prior_metadata_call, 1)

def blob(text: str) -> str:
    raw = text.encode()
    return hashlib.sha1(b"blob " + str(len(raw)).encode() + bytes([0]) + raw).hexdigest()

assert blob(pre_alias) == "21f098061b52e45962db90cad0146c7cc6673eff"
active = pre_alias

block = """    // Upstream Eglantine/Lichii dce3adbc: Switch 2 applications size
    // nninitStartup heap from total memory, which exceeds the ordinary
    // 64-bit heap region. Count the alias region too, bounded by the
    // process resource limit. This changes no KProcess class layout and
    // is inert for Switch 1 metadata.
    if (metadata.IsSwitch2()) {
        m_max_process_memory =
            m_page_table.GetHeapRegionSize() + m_page_table.GetAliasRegionSize();
    }

"""

assert active.count(block) == 1
assert base.count(block) == 0
assert active.count("metadata.IsSwitch2()") == 1
assert "m_is_switch2" not in active
assert "IsSwitch2() const" not in active

stack_receipt = """#if defined(PS5_NATIVE)
        // Fatal guest PC=4 reports on PS5 showed SP in a state=0 region.
        // Prove the page-table state immediately after the real KProcess map,
        // before any thread runs or teardown can modify it. One receipt per
        // process start; no guest memory is dereferenced.
        KMemoryInfo stack_info{};
        Svc::PageInfo stack_page{};
        const KProcessAddress probe = stack_top - 16;
        const bool stack_query =
            !m_page_table.QueryInfo(&stack_info, &stack_page, probe).IsError();
        std::fprintf(stderr,
                     "EDEN_MAIN_STACK_MAP bottom=0x%llx top=0x%llx bytes=0x%llx "
                     "probe=0x%llx query=%s start=0x%llx size=0x%llx state=%u\\n",
                     static_cast<unsigned long long>(stack_bottom),
                     static_cast<unsigned long long>(stack_top),
                     static_cast<unsigned long long>(stack_size),
                     static_cast<unsigned long long>(probe),
                     stack_query ? "ok" : "failed",
                     static_cast<unsigned long long>(stack_info.m_address),
                     static_cast<unsigned long long>(stack_info.m_size),
                     static_cast<unsigned>(stack_info.m_state));
#endif
"""
context_receipt = """#if defined(PS5_NATIVE)
    {
        const auto& initial_context = main_thread->GetContext();
        std::fprintf(stderr,
                     "EDEN_MAIN_THREAD_CONTEXT entry=0x%llx pc=0x%llx sp=0x%llx "
                     "lr=0x%llx stack_top=0x%llx\\n",
                     static_cast<unsigned long long>(this->GetEntryPoint()),
                     static_cast<unsigned long long>(initial_context.pc),
                     static_cast<unsigned long long>(initial_context.sp),
                     static_cast<unsigned long long>(initial_context.lr),
                     static_cast<unsigned long long>(stack_top));
    }
#endif
"""

assert active.count(stack_receipt) == 1
assert active.count(context_receipt) == 1
run = active.split("Result KProcess::Run(", 1)[1].split("\nResult KProcess::Terminate(", 1)[0]
assert run.index("m_page_table.MapPages(") < run.index("EDEN_MAIN_STACK_MAP")
assert run.index("EDEN_MAIN_STACK_MAP") < run.index("KThread::InitializeUserThread(")
assert run.index("KThread::InitializeUserThread(") < run.index("EDEN_MAIN_THREAD_CONTEXT")
assert "m_page_table.QueryInfo(&stack_info, &stack_page, probe)" in run
assert "stack_top - 16" in run
assert "initial_context.sp" in run and "initial_context.pc" in run

restored = active.replace(stack_receipt, "", 1).replace(context_receipt, "", 1)
restored = restored.replace("#include <cstdio>\n", "", 1)
restored = restored.replace(block, "", 1)
assert restored == base

assert blob(base) == "f338b293eb5feadceed8948b89ba0b77dcd17c26"
assert blob(active) == "21f098061b52e45962db90cad0146c7cc6673eff"

method = active.split("Result KProcess::LoadFromMetadata(", 1)[1].split(
    "\nvoid KProcess::LoadModule(", 1
)[0]
assert method.index("this->Initialize(") < method.index("metadata.IsSwitch2()")
assert method.index("metadata.IsSwitch2()") < method.index("InitializeInterfaces(kernel)")
assert method.count("GetHeapRegionSize() + m_page_table.GetAliasRegionSize()") == 1

print("NS2_KPROCESS_MEMORY_BUDGET_SOURCE_PASS upstream=dce3adbc alias128_reversed=1 abi_layout_unchanged=1 stack_receipt=1 thread_context_receipt=1")
