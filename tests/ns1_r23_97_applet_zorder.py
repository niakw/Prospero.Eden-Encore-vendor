#!/usr/bin/env python3
"""R23 #97 native WindowSystem overlay foreground eligibility source regression."""
from pathlib import Path
import hashlib, os, subprocess, tempfile
r=Path(__file__).resolve().parents[1]/"encore-active/eden-core/src/core/hle/service/am/window_system.cpp"
s=r.read_text()
raw=r.read_bytes()
assert hashlib.sha1(b"blob "+str(len(raw)).encode()+bytes([0])+raw).hexdigest()=="c4852c64bdcd491e9c7293db8a7e2dc055f78f5f"
line="z_index = this->IsOverlayOpenLocked(*applet) ? Overlay : Background;"
assert s.count(line)==1
assert "applet->display_layer_manager.SetOverlayZIndex(z_index);" in s
assert "this->UpdateAppletStateLocked(child_applet.get()" in s
test=r"""
#include <cassert>
enum Z {Background=0, Foreground=1, ForegroundVisible=2,Overlay=3};
struct Applet {bool opened=false;};
struct WindowSystem {
 bool IsOverlayOpenLocked(const Applet& applet) {return applet.opened;}
 int Compute(Applet* applet,bool is_overlay,bool inherited_foreground,bool is_obscured) {
  int z_index=Background;
  if (is_overlay) {
   z_index = this->IsOverlayOpenLocked(*applet) ? Overlay : Background;
  } else if (inherited_foreground) {
   z_index = is_obscured ? Foreground : ForegroundVisible;
  }
  return z_index;
 }
};
int main(){
 WindowSystem w;Applet a;
 assert(w.Compute(&a,true,false,false)==Background);
 a.opened=true;
 assert(w.Compute(&a,true,false,false)==Overlay);
 assert(w.Compute(&a,false,true,true)==Foreground);
 assert(w.Compute(&a,false,true,false)==ForegroundVisible);
}
""";
with tempfile.TemporaryDirectory(prefix="ns1-zorder-") as d:
 p=Path(d);(p/"t.cpp").write_text(test)
 subprocess.run([os.getenv("CXX","clang++"),"-std=c++20","-O1",
   "-fsanitize=address,undefined",str(p/"t.cpp"),"-o",str(p/"test")],check=True,timeout=60)
 subprocess.run([str(p/"test")],check=True,timeout=15,env={**os.environ,"ASAN_OPTIONS":"halt_on_error=1"})
print("R23_97_APPLET_ZORDER_HOST_PASS closed_overlay=background open_overlay=overlay")
