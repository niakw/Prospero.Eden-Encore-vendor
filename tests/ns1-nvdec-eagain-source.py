#!/usr/bin/env python3
"""Static Maintained PS5 NVDEC/FFmpeg candidate source contract. No compilation or game-boot claim."""
from pathlib import Path

root = Path(__file__).resolve().parents[1]
import hashlib

src_file = root / "encore-active/eden-core/src/video_core/host1x/ffmpeg.cpp"
header_file = root / "encore-active/eden-core/overrides/video_core/host1x/ffmpeg.h"
src = src_file.read_text()
header = header_file.read_text()
def git_blob(raw: bytes) -> str:
    return hashlib.sha1(b"blob " + str(len(raw)).encode() + bytes([0]) + raw).hexdigest()
assert git_blob(src_file.read_bytes()) == "9a3cf0b0a2cf92e467a619352b9345c2b7823d8b"
assert git_blob(header_file.read_bytes()) == "4bca3f84dbc224e925e18c28001286ee0e337448"
# Pinned Eden vendor source tree must remain byte-for-byte unmodified.
assert git_blob((root / "sources/eden/src/video_core/host1x/ffmpeg.cpp").read_bytes()) == "676e5820a592b194d0c72ee61bfa67a0fa29f45a"
assert git_blob((root / "sources/eden/src/video_core/host1x/ffmpeg.h").read_bytes()) == "00cd3f5d4d9571b4c8de944e3aec8988d11ccda5"
cmake = (root / "sources/eden/src/video_core/CMakeLists.txt").read_text()
decoder = (root / "sources/eden/src/video_core/host1x/codecs/decoder.cpp").read_text()

assert "host1x/ffmpeg.cpp" in cmake
assert "host1x/codecs/decoder.cpp" in cmake
assert "decode_api.SendPacket(packet_data, offsets" in decoder
assert "decode_api.ReceiveFrame()" in decoder
assert "int DecoderContext::SendPacket(const Packet& packet)" in src
assert "int SendPacket(const Packet& packet);" in header
assert "ret != AVERROR(EAGAIN)" in src
assert "m_ready_frames;" in header
assert "while (!m_ready_frames.empty())" in src
assert "m_ready_frames.size() >= 32" in src
# R457 diagnostic-only extension: bounded post-decode witness; EAGAIN semantics above stay exact.
assert src.count("EDEN_NVDEC_STAGE stage=decode") == 1
assert src.count("static std::atomic<unsigned> decoded_witness{};") == 1
assert "if (witness <= 3)" in src

submit = src.split("bool DecodeApi::SendPacket(", 1)[1].split(
    "std::optional<DecodeApi::DecodedFrame> DecodeApi::ReceiveFrame(", 1)[0]
assert submit.index("const int ret = m_decoder_context->SendPacket(packet);") < submit.index(
    "if (ret == 0)")
assert submit.index("if (ret == 0)") < submit.index("m_pending_offsets.push(offsets);")
assert submit.index("if (ret == 0)") < submit.index("++m_next_pts;")
assert submit.index("ret != AVERROR(EAGAIN)") < submit.index("m_ready_frames.push(")
assert submit.index("m_decoder_context->ReceiveFrame()") < submit.index(
    "m_ready_frames.push(")

receive = src.split("std::optional<DecodeApi::DecodedFrame> DecodeApi::ReceiveFrame(", 1)[1]
assert receive.index("m_ready_frames.front()") < receive.index(
    "frame = m_decoder_context->ReceiveFrame()")
print("NS1_NVDEC_EAGAIN_SOURCE_CONTRACT_PASS maintained_override=1 original_snapshot_pristine=1 retry=1 pts=1 frames=1 selected_engine=unverified ps5_runtime=unverified")
