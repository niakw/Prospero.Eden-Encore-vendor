#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Pin the narrow upstream NS2 KProcess memory-budget backport.

This is source qualification only. It proves the selected successor differs
from the pinned Eden owner only by Eglantine/Lichii dce3adbc's IsSwitch2
alias-region memory budget plus bounded PS5 stack/context receipts. The
receipts prove source ordering, not that a real PS5 guest stack maps.
"""
from pathlib import Path
import hashlib

ROOT = Path(__file__).resolve().parents[1]
BASE = ROOT / "sources/eden/src/core/hle/kernel/k_process.cpp"
ACTIVE = ROOT / "encore-active/ns2-runtime/process/core/hle/kernel/k_process.cpp"

base = BASE.read_text()
active = ACTIVE.read_text()

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

def blob(text: str) -> str:
    raw = text.encode()
    return hashlib.sha1(b"blob " + str(len(raw)).encode() + bytes([0]) + raw).hexdigest()

assert blob(base) == "f338b293eb5feadceed8948b89ba0b77dcd17c26"
assert blob(active) == "21f098061b52e45962db90cad0146c7cc6673eff"

method = active.split("Result KProcess::LoadFromMetadata(", 1)[1].split(
    "\nvoid KProcess::LoadModule(", 1
)[0]
assert method.index("this->Initialize(") < method.index("metadata.IsSwitch2()")
assert method.index("metadata.IsSwitch2()") < method.index("InitializeInterfaces(kernel)")
assert method.count("GetHeapRegionSize() + m_page_table.GetAliasRegionSize()") == 1

print("NS2_KPROCESS_MEMORY_BUDGET_SOURCE_PASS upstream=dce3adbc abi_layout_unchanged=1 stack_receipt=1 thread_context_receipt=1")
