#!/usr/bin/env python3
"""R23 #97: real maintained WindowSystem overlay z-order guard (host proof)."""
from pathlib import Path
import hashlib
import os
import subprocess
import tempfile

root=Path(__file__).resolve().parents[1]
p=root/"encore-active/eden-core/src/core/hle/service/am/window_system.cpp"
s=p.read_text()
b=p.read_bytes()
sha=hashlib.sha1(b"blob "+str(len(b)).encode()+bytes([0])+b).hexdigest()
assert sha=="c4852c64bdcd491e9c7293db8a7e2dc055f78f5f",sha
new="        z_index = this->IsOverlayOpenLocked(*applet) ? Overlay : Background;"
assert s.count(new)==1
fixture=r"""
#include <cassert>
struct Applet{bool open{};};
enum Z{Background=0,Foreground=1,ForegroundVisible=2,Overlay=3};
struct WindowSystem {
 bool IsOverlayOpenLocked(const Applet& a) const {return a.open;}
 Z Update(const Applet* applet,bool is_overlay,bool inherited_foreground,bool is_obscured) const {
   Z z_index=Background;
   if(is_overlay){
""" + new + r"""
   } else if(inherited_foreground) {
     z_index=is_obscured?Foreground:ForegroundVisible;
   }
   return z_index;
 }
};
int main() {
 WindowSystem system;Applet a{false};
 assert(system.Update(&a,true,false,false)==Background);
 a.open=true;
 assert(system.Update(&a,true,false,false)==Overlay);
 assert(system.Update(&a,false,true,false)==ForegroundVisible);
 assert(system.Update(&a,false,true,true)==Foreground);
 assert(system.Update(&a,false,false,false)==Background);
}
""".replace("   Z z_index=Background;","   auto z_index=Background;")
with tempfile.TemporaryDirectory(prefix="overlay97-") as tmp:
 t=Path(tmp)
 (t/"test.cpp").write_text(fixture)
 subprocess.run([os.getenv("CXX","clang++"),"-std=c++20","-O1","-g",
 "-fsanitize=address,undefined",str(t/"test.cpp"),"-o",str(t/"test")],check=True,timeout=60)
 subprocess.run([str(t/"test")],check=True,timeout=15,env={**os.environ,"ASAN_OPTIONS":"halt_on_error=1"})
print("R23_97_WINDOW_ZORDER_HOST_PASS open=1 closed=1 foreground=1")
