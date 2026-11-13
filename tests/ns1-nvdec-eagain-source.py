#!/usr/bin/env python3
"""Static PS5-selected NVDEC/FFmpeg source contract. No compilation or game-boot claim."""
from pathlib import Path

root = Path(__file__).resolve().parents[1]
src = (root / "sources/eden/src/video_core/host1x/ffmpeg.cpp").read_text()
header = (root / "sources/eden/src/video_core/host1x/ffmpeg.h").read_text()
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
print("NS1_NVDEC_EAGAIN_SOURCE_CONTRACT_PASS selected=1 retry=1 pts=1 frames=1 ps5_runtime=unverified")
