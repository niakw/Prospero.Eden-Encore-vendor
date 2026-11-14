#!/usr/bin/env python3
"""Source proof for bounded P0 guest prefetch-fault context diagnostics."""
from pathlib import Path
import hashlib

ROOT = Path(__file__).resolve().parents[1]
FILES = {
    "encore-active/eden-core/src/core/hle/kernel/physical_core_dev.cpp":
        "09e27e5361e70d123cdbbd6d450c596d7a272657",
    "encore-active/eden-core/src/core/hle/kernel/physical_core_standard.cpp":
        "bbcc4658c87dc530f5820f8e1e6a71da1aa9cea1",
}
INSERT = r'''                    Kernel::KMemoryInfo sp_info{};
                    Kernel::Svc::PageInfo sp_page{};
                    const bool sp_query = process &&
                        !process->GetPageTable().QueryInfo(
                            &sp_info, &sp_page, fault_context.sp).IsError();
                    std::fprintf(stderr,
                        "EDEN_GUEST_FAULT_OWNER pid=%llu program=0x%016llx entry=0x%llx core=%zu tid=%llu\n",
                        static_cast<unsigned long long>(process ? process->GetProcessId() : 0),
                        static_cast<unsigned long long>(process ? process->GetProgramId() : 0),
                        static_cast<unsigned long long>(process ? GetInteger(process->GetEntryPoint()) : 0),
                        static_cast<std::size_t>(m_core_index),
                        static_cast<unsigned long long>(thread->GetThreadId()));
                    std::fprintf(stderr,
                        "EDEN_GUEST_FAULT_SP_MAP sp=0x%llx query=%s start=0x%llx size=0x%llx state=%u\n",
                        static_cast<unsigned long long>(fault_context.sp),
                        sp_query ? "ok" : "failed",
                        static_cast<unsigned long long>(sp_info.m_address),
                        static_cast<unsigned long long>(sp_info.m_size),
                        static_cast<unsigned>(sp_info.m_state));
'''

def blob(text: str) -> str:
    raw = text.encode()
    return hashlib.sha1(b"blob " + str(len(raw)).encode() + b"\0" + raw).hexdigest()

for rel, previous in FILES.items():
    text = (ROOT / rel).read_text()
    assert text.count(INSERT) == 1, rel
    assert text.count("EDEN_GUEST_FAULT_OWNER") == 1, rel
    assert text.count("EDEN_GUEST_FAULT_SP_MAP") == 1, rel
    assert text.index("EDEN_GUEST_FAULT_OWNER") < text.index("EDEN_GUEST_FAULT_LR_MAP"), rel
    reconstructed = text.replace(INSERT, "", 1)
    assert blob(reconstructed) == previous, (rel, blob(reconstructed), previous)
    # Diagnostic-only: page-table metadata queries, no guest memory read/write.
    owner = text[text.index("EDEN_GUEST_FAULT_OWNER")-900:text.index("EDEN_GUEST_FAULT_LR_MAP")]
    assert "Read" not in owner and "Write" not in owner and "GetMemory()" not in owner, rel

pinned = (ROOT / "sources/eden/src/core/hle/kernel/physical_core.cpp").read_text()
assert "EDEN_GUEST_FAULT_OWNER" not in pinned
assert "EDEN_GUEST_FAULT_SP_MAP" not in pinned
print("P0_GUEST_FAULT_CONTEXT_SOURCE_PASS owner=1 stack_map=1 no_guest_deref=1 upstream_immutable=1")
