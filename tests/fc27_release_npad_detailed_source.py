#!/usr/bin/env python3
"""Release Detailed-Logging NPad Plus handoff witness.

The normal shipping NPad owner must stay byte-derivable from the previously
reviewed source plus one runtime-gated diagnostic block. No button mapping,
sampling cadence, IPC or shared-memory write semantics are changed.
"""
from pathlib import Path
import hashlib

R=Path(__file__).resolve().parents[1]
P=R/"encore-active/eden-core/src/hid_core/resources/npad/npad.cpp"
text=P.read_text()
old_includes="#include <algorithm>\n#include <array>\n#include <cstring>\n"
new_includes="#include <algorithm>\n#include <array>\n#include <atomic>\n#include <chrono>\n#include <cstdio>\n#include <cstring>\n"
helper="#include \"hid_core/resources/shared_memory_format.h\"\n\n#ifdef PS5_NATIVE\nextern \"C\" bool eden_native_detailed_logging() noexcept __attribute__((weak));\n#endif\n\nnamespace Service::HID {\nnamespace {\n[[nodiscard]] bool EdenDetailedNpadLogging() noexcept {\n#ifdef PS5_NATIVE\n    return eden_native_detailed_logging && eden_native_detailed_logging();\n#else\n    return false;\n#endif\n}\n} // namespace\n"
block="                if (EdenDetailedNpadLogging()) {\n                    // Diagnostic only: the event reached emulated HID shared memory.\n                    // It does not prove that the guest application consumed this sample.\n                    static std::atomic<unsigned long long> eden_fullkey_writes{0};\n                    static std::atomic<unsigned long long> eden_fullkey_plus_edges{0};\n                    static std::atomic<bool> eden_fullkey_last_plus{false};\n                    const bool eden_plus_now = static_cast<bool>(pad_state.npad_buttons.plus);\n                    const bool eden_plus_before = eden_fullkey_last_plus.exchange(\n                        eden_plus_now, std::memory_order_relaxed);\n                    const auto eden_writes =\n                        eden_fullkey_writes.fetch_add(1, std::memory_order_relaxed) + 1;\n                    if (eden_plus_now && !eden_plus_before) {\n                        const auto eden_edges =\n                            eden_fullkey_plus_edges.fetch_add(1, std::memory_order_relaxed) + 1;\n                        if (eden_edges <= 64) {\n                            const auto eden_mono_ns =\n                                std::chrono::duration_cast<std::chrono::nanoseconds>(\n                                    std::chrono::steady_clock::now().time_since_epoch()).count();\n                            std::fprintf(\n                                stderr,\n                                \"EDEN_NPAD_PLUS_LIFO edges=%llu writes=%llu sample=%llu \"\n                                \"aruid=%llu mono_ns=%lld\\n\",\n                                eden_edges, eden_writes,\n                                static_cast<unsigned long long>(pad_state.sampling_number),\n                                static_cast<unsigned long long>(aruid),\n                                static_cast<long long>(eden_mono_ns));\n                        }\n                    }\n                    if ((eden_writes % 1500) == 0) {\n                        std::fprintf(\n                            stderr,\n                            \"EDEN_NPAD_PROGRESS fullkey_writes=%llu plus_edges=%llu\\n\",\n                            eden_writes,\n                            eden_fullkey_plus_edges.load(std::memory_order_relaxed));\n                    }\n                }\n"

assert 'eden_native_detailed_logging() noexcept __attribute__((weak))' in text
assert 'return eden_native_detailed_logging && eden_native_detailed_logging();' in text
assert text.count('EDEN_NPAD_PLUS_LIFO edges=%llu') == 1
assert text.count('EDEN_NPAD_PROGRESS fullkey_writes=%llu') == 1
assert text.count('std::chrono::steady_clock::now()') == 1
assert text.count('npad->fullkey_lifo.WriteNextEntry(pad_state);') == 2
assert 'eden_edges <= 64' in text
assert 'sample=%llu' in text and 'mono_ns=%lld' in text

# Exact reverse to the previously reviewed production blob.
prior=text
assert prior.count(block)==1
prior=prior.replace(block,"",1)
assert prior.count(helper)==1
prior=prior.replace(helper,"",1)
assert prior.count(new_includes)==1
prior=prior.replace(new_includes,old_includes,1)
raw=prior.encode()
blob=hashlib.sha1(b"blob "+str(len(raw)).encode()+b"\0"+raw).hexdigest()
assert blob=="454138c445bc140568068737646349cac98a686d",blob
print("FC27_RELEASE_NPAD_DETAILED_SOURCE_PASS plus=edge_only cap=64 quiet=zero_clock old_blob=454138c4")
