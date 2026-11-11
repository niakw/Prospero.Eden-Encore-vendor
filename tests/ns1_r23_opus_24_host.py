#!/usr/bin/env python3
"""R23 #153: source-extracted session cap positive/negative test (host only)."""
from pathlib import Path
import os
import subprocess
import tempfile

root=Path(__file__).resolve().parents[1]
prefix=root/"encore-active/eden-core/src"
h=(prefix/"audio_core/opus/hardware_opus.h").read_text()
cpp=(prefix/"audio_core/opus/hardware_opus.cpp").read_text()
decoder=(prefix/"audio_core/opus/decoder.cpp").read_text()
errors=(prefix/"core/hle/service/audio/errors.h").read_text()
assert h.count("std::array<OpusDecoder*, 24> decoders{};")==1
assert errors.count("ResultOutOfOpusDecoders{ErrorModule::HwOpus, 385}")==1
start=cpp.index("Result HardwareOpus::RegisterDecoder(")
end=cpp.index("u32 HardwareOpus::GetWorkBufferSize(",start)
method=cpp[start:end]
assert method.count("std::scoped_lock l{mutex};")==2
assert method.count("R_THROW(ResultOutOfOpusDecoders);")==1
assert "hardware_opus.UnregisterDecoder(this);" in decoder
assert decoder.count("R_TRY(hardware_opus.RegisterDecoder(this));")==2
assert decoder.count("SCOPE_EXIT {")==2
test=r"""
#include <algorithm>
#include <array>
#include <cassert>
#include <mutex>
#include <ranges>
struct OpusDecoder {};
struct Result {int code;};
constexpr Result ResultOutOfOpusDecoders{385};
#define R_THROW(x) return (x)
#define R_SUCCEED() return Result{0}
struct HardwareOpus {
 std::mutex mutex;
 std::array<OpusDecoder*,24> decoders{};
 Result RegisterDecoder(OpusDecoder*);
 void UnregisterDecoder(OpusDecoder*);
};
""" + method + r"""
#include <thread>
#include <vector>
int main() {
 HardwareOpus hw;
 std::array<OpusDecoder,25> decoders{};
 for(int i=0;i<24;i++) assert(hw.RegisterDecoder(&decoders[i]).code==0);
 assert(hw.RegisterDecoder(&decoders[24]).code==385);
 hw.UnregisterDecoder(&decoders[9]);
 assert(hw.RegisterDecoder(&decoders[24]).code==0);
 assert(hw.RegisterDecoder(&decoders[9]).code==385);
 hw.UnregisterDecoder(&decoders[24]);
 assert(hw.RegisterDecoder(&decoders[9]).code==0);
 for(int i=0;i<24;i++) hw.UnregisterDecoder(&decoders[i]);
 for(auto p:hw.decoders) assert(p==nullptr);
 assert(hw.RegisterDecoder(&decoders[24]).code==0);
 hw.UnregisterDecoder(&decoders[24]);
 // Concurrent registration must never exceed exactly 24 owners.
 std::array<OpusDecoder,32> parallel{};
 std::array<int,32> codes{};
 std::vector<std::thread> workers;
 for (int i=0;i<32;i++) workers.emplace_back([&,i] {
   codes[i]=hw.RegisterDecoder(&parallel[i]).code;
 });
 for(auto& worker:workers) worker.join();
 int successes=0, limits=0;
 for(int c:codes) {successes+=(c==0); limits+=(c==385);}
 assert(successes==24 && limits==8);
 for (int i=0;i<32;i++) hw.UnregisterDecoder(&parallel[i]);
 for(auto p:hw.decoders) assert(p==nullptr);
}
"""
with tempfile.TemporaryDirectory(prefix="ns1-opus-24-") as td:
 p=Path(td)
 (p/"test.cpp").write_text(test)
 subprocess.run([os.environ.get("CXX","clang++"),"-std=c++20","-O1","-g","-pthread",
   "-fsanitize=address,undefined","-fno-omit-frame-pointer",str(p/"test.cpp"),"-o",str(p/"opus")],check=True,timeout=90)
 subprocess.run([str(p/"opus")],check=True,timeout=25,env={**os.environ,"ASAN_OPTIONS":"detect_leaks=1:halt_on_error=1"})
print("R23_OPUS_24_LIVE_SLOT_HOST_PASS last_slot=24 over_cap=385 reused_slot=1 concurrent=32")
print("R23_OPUS_NATIVE_PS5_UNQUALIFIED")
