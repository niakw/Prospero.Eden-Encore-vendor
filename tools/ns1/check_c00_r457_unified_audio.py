#!/usr/bin/env python3
"""PS5 selected audio source ownership: bounded AudioIn plus R457 AudioOut."""
from pathlib import Path
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[2]
src = (ROOT / "encore-active/eden-core/src/audio_core/device/device_session.cpp").read_text()
original = (ROOT / "sources/eden/src/audio_core/device/device_session.cpp").read_text()
assert 'audio_in_silence.h' in src
assert 'audio_in_silence.h' not in original
assert src.count("void DeviceSession::AppendBuffers(") == 1
assert src.count("void DeviceSession::ReleaseBuffer(") == 1
assert src.count('Eden::IsValidAudioInBufferShape(buffer.size, channel_count)') == 2
assert 'stream->AppendBuffer(new_buffer, std::span<s16>{});' in src
assert 'buffer.size / (channel_count * sizeof(s16))' in src
assert 'memory.IsValidVirtualAddressRange(Common::ProcessAddress{buffer.samples}' in src
assert 'samples.size() != buffer.size / sizeof(s16)' in src
assert '!memory.ReadyForAudioRead()' in src
assert 'if (!handle || !handle->GetMemory().ReadyForAudioRead())' in src
assert 'std::fill_n(tmp_samples.data(), buffer.size / sizeof(s16), s16{0})' in src
assert src.index('if (!handle || !handle->GetMemory().ReadyForAudioRead())') < src.index('CpuGuestMemory<s16')
# Verify both maintained memory owners and immutable upstream header.
for suffix in ('a32', 'standard'):
    mem = (ROOT / f'encore-active/eden-core/src/core/memory_native_{suffix}.cpp').read_text()
    assert mem.count('bool Memory::ReadyForAudioRead() const noexcept') == 1
assert 'ReadyForAudioRead' not in (ROOT / "sources/eden/src/core/memory.h").read_text()
subprocess.run([sys.executable, "-B", str(ROOT / "tools/ns1/check_r457_audio_unready_host.py")], check=True)
print('C00_R457_UNIFIED_AUDIO_SOURCE_PASS one_TU=1 readiness=2 bounded_AudioIn=1')
