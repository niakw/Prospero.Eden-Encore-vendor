#!/usr/bin/env python3
"""NVDEC packet retry and frame FIFO scenario host witness.

The actual selected FFmpeg source is checked separately by
ns1-nvdec-eagain-source.py. This executable fixture validates the EAGAIN
contract with simulated FFmpeg return codes, *not* actual PS5 decoding.
"""
from __future__ import annotations

from pathlib import Path
import shutil
import subprocess
import tempfile

ROOT=Path(__file__).resolve().parents[1]
source=(ROOT/"sources/eden/src/video_core/host1x/ffmpeg.cpp").read_text()
header=(ROOT/"sources/eden/src/video_core/host1x/ffmpeg.h").read_text()
for clause in ("kMaxDrainRetries = 16","m_ready_frames.size() >= 32",
               "ret == 0","ret != AVERROR(EAGAIN)", "m_pending_offsets.push(offsets)",
               "++m_next_pts", "m_ready_frames.push(std::move(drained))"):
    assert clause in source,clause
assert "std::queue<std::shared_ptr<Frame>> m_ready_frames;" in header
src=r"""
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <vector>
struct Decoder {
  std::deque<int> attempts;
  std::deque<int> ready;
  int send(int packet) {
    if(attempts.empty()) return 0;
    int result=attempts.front(); attempts.pop_front();
    return result;
  }
  int receive() {
    if(ready.empty()) return -1;
    int result=ready.front(); ready.pop_front(); return result;
  }
};
struct Api {
  Decoder decoder;
  std::deque<int> queued, offsets;
  int next_pts=0;
  bool send(int packet, int offset, bool hidden=false) {
    for(int retry=0;retry<16;++retry) {
      int ret=decoder.send(packet);
      if(ret==0) {
        if(!hidden) offsets.push_back(offset);
        ++next_pts;
        return true;
      }
      if(ret!=-11) return false; // AVERROR(EAGAIN)
      if(queued.size()>=32) return false;
      int drained=decoder.receive();
      if(drained<0) return false;
      queued.push_back(drained);
    }
    return false;
  }
  int receive() {
    int frame=-1;
    if(!queued.empty()){frame=queued.front();queued.pop_front();}
    else frame=decoder.receive();
    if(frame>=0 && !offsets.empty()) offsets.pop_front();
    return frame;
  }
  void reset(){queued.clear(); offsets.clear(); next_pts=0; decoder.attempts.clear(); decoder.ready.clear();}
};
int main(){
  Api a;
  // Previous packet accepted. The next send is rejected until the
  // previous frame is drained and then *the same* packet must be retried.
  assert(a.send(10,100) && a.next_pts==1 && a.offsets.size()==1);
  a.decoder.attempts={-11,0};a.decoder.ready={10,11};
  assert(a.send(11,110));assert(a.next_pts==2);
  assert(a.queued.size()==1);assert(a.queued.front()==10);
  assert(a.receive()==10);assert(a.receive()==11);
  assert(a.offsets.empty() && a.queued.empty());
  a.reset();
  // No decoded output on EAGAIN: input must NOT count as consumed.
  a.decoder.attempts={-11};assert(!a.send(1,10));
  assert(a.next_pts==0&&a.offsets.empty());
  a.reset();
  // Queue bounded at 32 and PTS advanced only once successful.
  for(int i=0;i<32;i++)a.queued.push_back(i);
  a.decoder.attempts={-11};a.decoder.ready={40};
  assert(!a.send(41,41));
  assert(a.queued.size()==32&&a.next_pts==0);
  a.reset();
  // Real FFmpeg can repeatedly reject packets with EAGAIN.
  for(int i=0;i<16;i++)a.decoder.attempts.push_back(-11);
  for(int i=0;i<16;i++)a.decoder.ready.push_back(i);
  assert(!a.send(17,17));assert(a.next_pts==0 && a.queued.size()==16);
  a.reset();
  // Normal and hidden PTS paths preserve previous contract.
  assert(a.send(1,1,true)&&a.next_pts==1&&a.offsets.empty());
  assert(a.send(2,2)&&a.next_pts==2&&a.offsets.size()==1);
  a.reset();
  assert(a.next_pts==0&&a.offsets.empty()&&a.queued.empty());
}
"""
with tempfile.TemporaryDirectory(prefix="nvdec-eagain-") as td:
    path=Path(td)/"fixture.cpp"
    path.write_text(src)
    compilers=[cc for cc in ("clang++","g++") if shutil.which(cc)]
    assert compilers
    for cc in compilers:
        exe=Path(td)/(cc.replace("+","p")+"-fixture")
        cmd=[cc,"-std=c++20","-Wall","-Wextra","-Werror","-O1",
             "-fsanitize=undefined","-fno-sanitize-recover=all",
             str(path),"-o",str(exe)]
        x=subprocess.run(cmd,capture_output=True,text=True,timeout=60)
        assert x.returncode==0,(cc,x.stderr[-2600:])
        y=subprocess.run([str(exe)],capture_output=True,text=True,timeout=20)
        assert y.returncode==0,(cc,y.stderr[-2500:])
print("NVDEC_EAGAIN_HOST_PASS source_contract=1 retry_identical=1 "
      "bounded_queue=1 output_order=1 pts_on_accept=1 reset=1 "
      "ffmpeg_real=untested ps5_intro=untested")
