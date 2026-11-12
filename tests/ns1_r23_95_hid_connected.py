#!/usr/bin/env python3
"""R23 #95: selected maintained HID source and header matched controller flags."""
from pathlib import Path
import hashlib, os, subprocess, tempfile
r=Path(__file__).resolve().parents[1]
cpp=r/"encore-active/eden-core/src/hid_core/frontend/emulated_controller.cpp"
header=r/"encore-active/eden-core/include/hid_core/frontend/emulated_controller.h"
def gitsha(f):
 d=f.read_bytes()
 return hashlib.sha1(b"blob "+str(len(d)).encode()+bytes([0])+d).hexdigest()
assert gitsha(cpp)=="9ce2506de27fceb18567ad9be12e8da4430fe140"
assert gitsha(header)=="80ea633c636eee4c9822434b4e033c91c5681197"
s=cpp.read_text();h=header.read_text()
start="void EmulatedController::SetButton("
end="\nvoid EmulatedController::SetStick("
method=s.split(start,1)[1].split(end,1)[0]
assert method.count("if (player.connected) {")==1
assert method.count("Connect();")==1
assert "controller_connected[player_index]" not in method
assert "controller_connected" not in h
assert "#include \"hid_core/hid_core.h\"" not in h
assert "TriggerOnChange(ControllerTriggerType::Button, true);" in method
test=r"""
#include <cassert>
struct Player { bool connected=false; };
struct Controller {
 int connect_calls=0;
 void Connect(){++connect_calls;}
 void SetButton(const Player& player){
  if (player.connected) {
   Connect();
  }
 }
};
int main(){
 Controller c;
 c.SetButton(Player{false});
 assert(c.connect_calls==0);
 c.SetButton(Player{true});
 assert(c.connect_calls==1);
 c.SetButton(Player{false});
 assert(c.connect_calls==1);
}
""";
with tempfile.TemporaryDirectory(prefix="r23-hid-") as t:
 p=Path(t)
 (p/"t.cpp").write_text(test)
 subprocess.run([os.getenv("CXX","clang++"),"-std=c++20","-O1",
   "-fsanitize=address,undefined",str(p/"t.cpp"),"-o",str(p/"test")],check=True,timeout=60)
 subprocess.run([str(p/"test")],check=True,timeout=15,
   env={**os.environ,"ASAN_OPTIONS":"detect_leaks=1:halt_on_error=1"})
print("R23_95_HID_CONNECT_SOURCE_HOST_PASS settings_connected_gate=1 stale_flags=0")
