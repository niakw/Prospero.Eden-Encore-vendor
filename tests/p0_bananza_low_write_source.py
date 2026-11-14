#!/usr/bin/env python3
"""Exact source proof for bounded low-page guest-write diagnostics."""
from pathlib import Path
import hashlib

ROOT=Path(__file__).resolve().parents[1]
FILES={
 "encore-active/eden-core/src/core/arm/dynarmic/g11/arm_dynarmic_64_native_normal.cpp":
     "b03c862e9e2d54adaa49e4200bd980ec6d605c0b",
 "encore-active/eden-core/src/core/arm/dynarmic/g11/arm_dynarmic_64_native_dev.cpp":
     "812ce056ef23d671dcab69bb74c2b8877a70fc43",
}
HELPER='''namespace {
std::atomic<u64> low_guest_write_sequence{0};

inline void ReportLowGuestWrite(u64 address, std::size_t bytes, u64 value,
                                u64 pc, u64 thread_id, std::size_t core) {
    if (address >= 0x1000) {
        return;
    }
    const u64 sequence =
        low_guest_write_sequence.fetch_add(1, std::memory_order_relaxed) + 1;
    if (sequence > 64 && (sequence & (sequence - 1)) != 0) {
        return;
    }
    LOG_ERROR(Core_ARM,
              "EDEN_GUEST_LOW_WRITE seq={} pc={:#016x} address={:#x} bytes={} value={:#016x} core={} tid={}",
              sequence, pc, address, bytes, value, core, thread_id);
}
} // namespace

'''
METHODS=[
 ("MemoryWrite8(u64 vaddr, u8 value)","1","u64(value)"),
 ("MemoryWrite16(u64 vaddr, u16 value)","2","u64(value)"),
 ("MemoryWrite32(u64 vaddr, u32 value)","4","u64(value)"),
 ("MemoryWrite64(u64 vaddr, u64 value)","8","value"),
 ("MemoryWrite128(u64 vaddr, Dynarmic::A64::Vector value)","16","u64(value[0])"),
 ("MemoryWriteExclusive8(u64 vaddr, std::uint8_t value, std::uint8_t expected)","1","u64(value)"),
 ("MemoryWriteExclusive16(u64 vaddr, std::uint16_t value, std::uint16_t expected)","2","u64(value)"),
 ("MemoryWriteExclusive32(u64 vaddr, std::uint32_t value, std::uint32_t expected)","4","u64(value)"),
 ("MemoryWriteExclusive64(u64 vaddr, std::uint64_t value, std::uint64_t expected)","8","value"),
 ("MemoryWriteExclusive128(u64 vaddr, Dynarmic::A64::Vector value, Dynarmic::A64::Vector expected)","16","u64(value[0])"),
]

def blob(text):
 raw=text.encode()
 return hashlib.sha1(b"blob "+str(len(raw)).encode()+b"\0"+raw).hexdigest()

for rel,previous in FILES.items():
 text=(ROOT/rel).read_text()
 assert text.count("EDEN_GUEST_LOW_WRITE")==1,rel
 assert text.count("vaddr < 0x1000")==10,rel
 assert text.count("ReportLowGuestWrite(vaddr,")==10,rel
 assert "sequence > 64 && (sequence & (sequence - 1)) != 0" in text
 reconstructed=text
 for sig,size,value in METHODS:
  marker=f"{sig} {{\n"
  call=(f"    if (vaddr < 0x1000) ReportLowGuestWrite(vaddr, {size}, {value}, m_parent.m_jit->GetPC(),\n"
        "        Kernel::GetCurrentThread(m_parent.m_system.Kernel()).GetThreadId(), m_parent.m_core_index);\n")
  assert reconstructed.count(call)==1,(rel,sig)
  reconstructed=reconstructed.replace(call,"",1)
 assert reconstructed.count(HELPER)==1,rel
 reconstructed=reconstructed.replace(HELPER,"",1)
 assert reconstructed.startswith("#include <atomic>\n#include <bit>\n")
 reconstructed=reconstructed.replace("#include <atomic>\n#include <bit>\n","#include <bit>\n",1)
 assert blob(reconstructed)==previous,(rel,blob(reconstructed),previous)

pinned=(ROOT/"sources/eden/src/core/arm/dynarmic/arm_dynarmic_64.cpp").read_text()
assert "EDEN_GUEST_LOW_WRITE" not in pinned
print("P0_BANANZA_LOW_WRITE_SOURCE_PASS profiles=2 writes=10 low_page_only=1 bounded=1 semantics_unchanged=1")
