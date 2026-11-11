#!/usr/bin/env python3
"""NS1 R23 #48: exact coherent VP9 source/ABI + extracted bitstream method test."""
from pathlib import Path
import hashlib
import os
import subprocess
import tempfile
R=Path(__file__).resolve().parents[1]
paths={
  "video_core/host1x/codec_types.h":("b2f80c822a7fa8cafb86fc395dcded5d517edda0","da9b713b263ae8e742491c256d8ff2c84767c872"),
  "video_core/host1x/codecs/vp9.cpp":("dcc6261e5c79d30f083426d39251d417aa65b017","dd91f7f1b7649211f731db5b177940fbd8880d09"),
}
def blob(p):
 b=p.read_bytes()
 return hashlib.sha1(b"blob "+str(len(b)).encode()+bytes([0])+b).hexdigest()
for rel,(original,maintained) in paths.items():
 assert blob(R/"sources/eden/src"/rel)==original,rel
 assert blob(R/"encore-active/eden-core/src"/rel)==maintained,rel
 print("R23_48_COHERENT_VP9_SOURCE_PASS",rel)
s=(R/"encore-active/eden-core/src/video_core/host1x/codecs/vp9.cpp").read_text()
start="void VP9::WriteSegmentation(VpxBitStreamWriter& writer) {"
end="\nVp9PictureInfo VP9::GetVp9PictureInfo()"
assert s.count(start)==1 and s.count(end)==1
method=start+s.split(start,1)[1].split(end,1)[0]
assert "current_frame_info.seg_tree_probs" in method
assert "current_frame_info.seg_pred_probs" in method
fixture=r"""
#include <array>
#include <cassert>
#include <cstdint>
#include <vector>
using u8=std::uint8_t;
using s16=std::int16_t;
using s32=std::int32_t;
struct Segmentation {
 bool operator==(const Segmentation& rhs) const = default;
 u8 enabled=0,update_map=0,temporal_update=0,abs_delta=0;
 std::array<std::array<u8,4>,8> feature_enabled{};
 std::array<std::array<s16,4>,8> feature_data{};
};
static_assert(sizeof(Segmentation)==0x64);
struct Info {
 Segmentation segmentation{};
 std::array<u8,7> seg_tree_probs{};
 std::array<u8,3> seg_pred_probs{};
};
struct VpxBitStreamWriter {
 std::vector<std::int64_t> bits, uvals, svals;
 void WriteBit(bool b){bits.push_back(b);}
 void WriteU(std::int64_t value,int){uvals.push_back(value);}
 void WriteS(std::int64_t value,int){svals.push_back(value);}
};
struct VP9 {
 Info current_frame_info{};
 Segmentation last_segmentation{};
 void WriteSegmentation(VpxBitStreamWriter&);
};
""" + method + r"""
int main() {
 VP9 decoder;
 VpxBitStreamWriter out;
 decoder.WriteSegmentation(out);
 assert(out.bits.size()==1 && out.bits[0]==0);
 decoder.current_frame_info.segmentation.enabled=1;
 decoder.current_frame_info.segmentation.update_map=1;
 decoder.current_frame_info.segmentation.temporal_update=1;
 decoder.current_frame_info.segmentation.feature_enabled[0][0]=1;
 decoder.current_frame_info.segmentation.feature_data[0][0]=17;
 decoder.current_frame_info.seg_tree_probs.fill(255);
 decoder.current_frame_info.seg_pred_probs.fill(255);
 decoder.current_frame_info.seg_tree_probs[3]=12;
 decoder.current_frame_info.seg_pred_probs[1]=21;
 out={};
 decoder.WriteSegmentation(out);
 assert(out.bits[0]==1 && out.bits[1]==1);
 assert(out.uvals.size()>=2 && out.uvals[0]==12 && out.uvals[1]==21);
 assert(out.svals.size()==1 && out.svals[0]==17);
 out={};
 decoder.WriteSegmentation(out);
 assert(out.bits.back()==0);
 decoder.current_frame_info.segmentation.feature_data[0][0]=22;
 out={};
 decoder.WriteSegmentation(out);
 assert(out.svals.size()==1 && out.svals[0]==22);
}
""";
with tempfile.TemporaryDirectory(prefix="vp9-48-") as tmp:
 d=Path(tmp)
 (d/"vp9.cpp").write_text(fixture)
 subprocess.run([os.environ.get("CXX","clang++"),"-std=c++20","-O1","-g",
                 "-fsanitize=address,undefined","-fno-omit-frame-pointer",
                 str(d/"vp9.cpp"),"-o",str(d/"vp9")],check=True,timeout=60)
 subprocess.run([str(d/"vp9")],check=True,timeout=20,
                env={**os.environ,"ASAN_OPTIONS":"detect_leaks=1:halt_on_error=1"})
print("NS1_R23_48_VP9_SEGMENTATION_HOST_PASS temporal=1 tree=1 feature=1 replay=1")
