#!/usr/bin/env python3
"""PS5 R457 AudioOut null-memory regression contract (host-only).

Uses the real changed source for structural checks and a tiny C++ readiness
oracle. It does not emulate FC27 or prove a game boots on PS5.
"""
from pathlib import Path
import shutil
import subprocess
import tempfile

root = Path(__file__).resolve().parents[2]
header = (root / "encore-active/eden-core/overrides/core/memory.h").read_text()
memory = (root / "encore-active/eden-core/src/core/memory_native_a32.cpp").read_text()
audio = (root / "encore-active/eden-core/src/audio_core/device/device_session.cpp").read_text()

standard_memory = (root / "encore-active/eden-core/src/core/memory_native_standard.cpp").read_text()
assert "bool Memory::ReadyForAudioRead() const noexcept" in standard_memory
assert "return impl && impl->current_page_table != nullptr;" in standard_memory
# The immutable source pin is deliberately untouched.
assert "ReadyForAudioRead" not in (root / "sources/eden/src/core/memory.h").read_text()
assert "ReadyForAudioRead" not in (root / "sources/eden/src/audio_core/device/device_session.cpp").read_text()

assert "bool ReadyForAudioRead() const noexcept;" in header
assert "bool Memory::ReadyForAudioRead() const noexcept" in memory
assert "return impl && impl->current_page_table != nullptr;" in memory
assert "#ifdef PS5_NATIVE" in memory
assert "void Memory::SetCurrentPageTable" in memory
assert "if (!handle || !handle->GetMemory().ReadyForAudioRead())" in audio
assert "std::fill_n(tmp_samples.data(), buffer.size / sizeof(s16), s16{0});" in audio
assert "EDEN_NS1_AUDIO_MEMORY_UNREADY temporary_silence=1" in audio
assert "static std::atomic_flag warned = ATOMIC_FLAG_INIT;" in audio
assert 'stream->AppendBuffer(new_buffer, tmp_samples);' in audio
assert "continue;" in audio[audio.index("if (!handle || !handle->GetMemory().ReadyForAudioRead())"):][:720]
# Actual native path and the original guest-pointer read remain intact.
assert audio.index("ReadyForAudioRead()") < audio.index("CpuGuestMemory<s16")
assert "handle->GetMemory(), buffer.samples, buffer.size / sizeof(s16)" in audio
assert "stream->AppendBuffer(new_buffer, samples);" in audio
# Ensure a future refactor cannot move the fallback outside the PS5 guard.
readiness = audio.index("if (!handle || !handle->GetMemory().ReadyForAudioRead())")
guard = audio.rfind("#ifdef PS5_NATIVE", 0, readiness)
assert guard >= 0
assert audio.find("#endif", guard) > readiness

cc = shutil.which("clang++") or shutil.which("g++")
assert cc, "C++20 compiler missing"
source = r"""
#include <algorithm>
#include <array>
#include <atomic>
#include <cassert>
#include <cstddef>
#include <memory>
struct Impl { const void* current_page_table{}; };
struct Memory {
  std::unique_ptr<Impl> impl;
  bool ReadyForAudioRead() const noexcept {
    return impl && impl->current_page_table != nullptr;
  }
};
struct Stream {
  int calls{};
  std::array<short, 4> last{};
  void AppendBuffer(const std::array<short,4>& samples) {
    last = samples;
    ++calls;
  }
};
void append(bool ready, Stream& stream) {
  std::array<short,4> temporary{7,7,7,7};
  if (!ready) {
    std::fill_n(temporary.data(), temporary.size(), short{0});
    stream.AppendBuffer(temporary);
    return;
  }
  std::array<short,4> samples{1,2,3,4};
  stream.AppendBuffer(samples);
}
int main() {
  Memory state{};
  Stream sink{};
  assert(!state.ReadyForAudioRead());
  append(state.ReadyForAudioRead(), sink);
  assert(sink.calls == 1);
  assert(sink.last == (std::array<short,4>{0,0,0,0}));
  state.impl = std::make_unique<Impl>();
  assert(!state.ReadyForAudioRead());
  append(state.ReadyForAudioRead(), sink);
  assert(sink.calls == 2);
  assert(sink.last == (std::array<short,4>{0,0,0,0}));
  int mapped = 1;
  state.impl->current_page_table = &mapped;
  assert(state.ReadyForAudioRead());
  append(state.ReadyForAudioRead(), sink);
  assert(sink.calls == 3);
  assert(sink.last == (std::array<short,4>{1,2,3,4}));
}
"""
with tempfile.TemporaryDirectory(prefix="encore-r457-audio-") as d:
    src = Path(d) / "test.cpp"
    out = Path(d) / "test"
    src.write_text(source)
    run = subprocess.run([cc, "-std=c++20", "-O1", "-Wall", "-Wextra",
                          "-Werror", str(src), "-o", str(out)],
                         capture_output=True, text=True)
    assert run.returncode == 0, run.stderr
    case = subprocess.run([str(out)], capture_output=True, text=True)
    assert case.returncode == 0, case.stderr
print("R457 NS1 AudioOut readiness: source contract and C++20 oracle PASS")
