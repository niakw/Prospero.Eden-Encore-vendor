#!/usr/bin/env python3
"""NS1 R23 #95: exact paired maintained HID frontend source and reconnect fixture."""
from pathlib import Path
import hashlib
import os
import subprocess
import tempfile
r=Path(__file__).resolve().parents[1]/"encore-active/eden-core"
sources={
  "src/hid_core/frontend/emulated_controller.cpp":"9ce2506de27fceb18567ad9be12e8da4430fe140",
  "include/hid_core/frontend/emulated_controller.h":"80ea633c636eee4c9822434b4e033c91c5681197"
}
for name,expected in sources.items():
 b=(r/name).read_bytes()
 sha=hashlib.sha1(b"blob "+str(len(b)).encode()+bytes([0])+b).hexdigest()
 assert sha==expected,(name,sha)
cpp=(r/"src/hid_core/frontend/emulated_controller.cpp").read_text()
h=(r/"include/hid_core/frontend/emulated_controller.h").read_text()
assert "controller_connected" not in cpp+h
snippet="    const auto player_index = Service::HID::NpadIdTypeToIndex(npad_id_type);\n    const auto& player = Settings::values.players.GetValue()[player_index];\n    if (player.connected) {\n        Connect();\n    }"
assert cpp.count(snippet)==1
assert "if (is_connected) {\n        trigger_guard.Cancel();" in cpp
source=r"""
#include <array>
#include <cassert>
#include <cstddef>
struct Player{bool connected;};
struct Settings {
 struct Values {struct Players {
   std::array<Player,2> data{};
   const auto& GetValue() const {return data;}
 } players;} ;
 static Values values;
};
Settings::Values Settings::values{};
namespace Service::HID {std::size_t NpadIdTypeToIndex(std::size_t i){return i;}}
struct EmulatedController {
 std::size_t npad_id_type{};
 bool is_connected{};
 int connected_events{};
 void Connect(){if(is_connected)return;is_connected=true;++connected_events;}
 void Disconnect(){is_connected=false;}
 void OnButton(){
""" + snippet + r"""
 }
};
int main(){
 EmulatedController p{0};
 Settings::values.players.data[0].connected=false;
 p.OnButton();assert(!p.is_connected);
 Settings::values.players.data[0].connected=true;
 p.OnButton();p.OnButton();
 assert(p.is_connected && p.connected_events==1);
 p.Disconnect();p.OnButton();assert(p.connected_events==2);
 Settings::values.players.data[0].connected=false;
 p.Disconnect();p.OnButton();assert(!p.is_connected);
}
""";
with tempfile.TemporaryDirectory(prefix="ns1-hid95-") as t:
 p=Path(t)
 (p/"fixture.cpp").write_text(source)
 subprocess.run([os.getenv("CXX","clang++"),"-std=c++20","-O1","-g",
 "-fsanitize=address,undefined",str(p/"fixture.cpp"),"-o",str(p/"test")],check=True,timeout=60)
 subprocess.run([str(p/"test")],check=True,timeout=20,env={**os.environ,"ASAN_OPTIONS":"halt_on_error=1"})
print("R23_95_HID_CONNECT_HOST_PASS disabled=1 reconnect=1 duplicate_callback=0")
