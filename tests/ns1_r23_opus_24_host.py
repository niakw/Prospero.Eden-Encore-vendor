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

# FC26 issue #684 / FC27-family host libopus safety. Nintendo's guest transfer
# size is not a safe host-libopus state size, and PCM spans are bytes.
assert decoder.count("* sizeof(s16)") >= 2
assert "hardware_opus.GetWorkBufferSize(params.channel_count)" in decoder
assert "hardware_opus.GetWorkBufferSizeForMultiStream(" in decoder
assert decoder.count("std::max<u64>(transfer_memory_size, host_work_size)") == 2
assert decoder.count("work_size + buffer_size") == 2
assert cpp.count("output_data_size / (static_cast<u64>(channel_count) * sizeof(s16))") == 2
assert "R_UNLESS(channel_count > 0 && channel_count <= shared_memory.channel_mapping.size()," in cpp
assert "ASSERT(channel_count <= shared_memory.channel_mapping.size())" not in cpp
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
# Reproduce the arithmetic behind Eden issue #684 without guest or PS5
# dependencies. ASan/UBSan guards the resulting non-overlapping host spans.
layout_test=r"""
#include <algorithm>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <vector>
using u64=std::uint64_t;
struct Layout {u64 state_end,in_begin,in_end,out_begin,out_end,total;};
static u64 AlignUp(u64 x,u64 a){return (x+a-1)/a*a;}
static Layout Multi(u64 transfer,u64 host_state,u64 streams,u64 frame,u64 channels,u64 rate){
 const u64 in=AlignUp(1500ull*streams,64);
 const u64 out=AlignUp(frame*channels*sizeof(std::int16_t)/(48000/rate),16);
 const u64 work=std::max(transfer,host_state+in);
 assert(work<=std::numeric_limits<std::size_t>::max()-out);
 const u64 total=work+out;
 return {host_state,work-in,work,work,total,total};
}
int main(){
 // Concrete FC-family regression from upstream: guest area 43520 bytes,
 // linked libopus multistream state 45568 bytes.
 auto l=Multi(43520,45568,2,1920,2,48000);
 assert(l.state_end==45568);
 assert(l.in_begin>=l.state_end);
 assert(l.in_end==l.out_begin);
 assert(l.out_end==l.total);
 assert(l.total>43520);
 std::vector<std::byte> storage(static_cast<std::size_t>(l.total));
 storage.at(static_cast<std::size_t>(l.state_end-1))=std::byte{1};
 storage.at(static_cast<std::size_t>(l.in_begin))=std::byte{2};
 storage.at(static_cast<std::size_t>(l.out_end-1))=std::byte{3};
 const u64 output_bytes=l.out_end-l.out_begin;
 assert(output_bytes==1920*2*sizeof(std::int16_t));
 const u64 opus_frame_capacity=output_bytes/(2*sizeof(std::int16_t));
 assert(opus_frame_capacity==1920);
}
"""
with tempfile.TemporaryDirectory(prefix="fc-opus-buffer-") as td:
 p=Path(td)
 src=p/"layout.cpp"; exe=p/"layout"
 src.write_text(layout_test)
 subprocess.run([os.environ.get("CXX","clang++"),"-std=c++20","-O1","-g",
   "-fsanitize=address,undefined","-fno-omit-frame-pointer",str(src),"-o",str(exe)],check=True,timeout=90)
 subprocess.run([str(exe)],check=True,timeout=25,
   env={**os.environ,"ASAN_OPTIONS":"detect_leaks=1:halt_on_error=1"})
print("FC_OPUS_HOST_BUFFER_SAFETY_PASS guest=43520 host_state=45568 pcm_bytes=7680 opus_capacity_samples=1920")
print("R23_OPUS_24_LIVE_SLOT_HOST_PASS last_slot=24 over_cap=385 reused_slot=1 concurrent=32")
print("R23_OPUS_NATIVE_PS5_UNQUALIFIED")
