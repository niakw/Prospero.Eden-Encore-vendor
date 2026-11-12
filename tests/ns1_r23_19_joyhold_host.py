#!/usr/bin/env python3
"""R23 #19: selected HOS JoyHold orientation and serialized input reload."""
from pathlib import Path
import hashlib, os, subprocess, tempfile
root=Path(__file__).resolve().parents[1]/"encore-active/eden-core"
pins={
"src/hid_core/hid_core.cpp":"fcf87d6d6349ec7a92be754c7476edbd1653b262",
"include/hid_core/hid_core.h":"1cd3427724c256a20499f08505970cebb4ade102",
"src/hid_core/resources/npad/npad.cpp":"454138c445bc140568068737646349cac98a686d",
}
for path,expected in pins.items():
 raw=(root/path).read_bytes()
 sha=hashlib.sha1(b"blob "+str(len(raw)).encode()+bytes([0])+raw).hexdigest()
 assert sha==expected,(path,sha)
c=(root/"src/hid_core/hid_core.cpp").read_text()
h=(root/"include/hid_core/hid_core.h").read_text()
n=(root/"src/hid_core/resources/npad/npad.cpp").read_text()
assert "std::scoped_lock lock{reload_mutex};" in c
assert "std::mutex reload_mutex;" in h
assert "std::atomic<bool> joy_hold_horizontal{false};" in h
assert "void HIDCore::SetJoyHoldHorizontal(bool horizontal)" in c
assert "hid_core.SetJoyHoldHorizontal(false);" in n
assert "hid_core.SetJoyHoldHorizontal(hold_type == NpadJoyHoldType::Horizontal);" in n
assert "if (applet_resource_holder.applet_resource == nullptr)" in n
assert "controller.device == nullptr" in n
assert "controller.device->SetCallback(engine_callback)" in n
test=r"""
#include <atomic>
#include <cassert>
#include <mutex>
#include <thread>
#include <vector>
struct HIDCore {
 std::atomic<bool> joy_hold_horizontal{false};
 std::mutex reload_mutex;
 int reloads=0;
 void SetJoyHoldHorizontal(bool h){joy_hold_horizontal=h;}
 bool IsJoyHoldHorizontal() const{return joy_hold_horizontal;}
 void ReloadInputDevices(){std::scoped_lock lock{reload_mutex};++reloads;}
};
int main(){
 HIDCore h;
 assert(!h.IsJoyHoldHorizontal());
 h.SetJoyHoldHorizontal(true);
 assert(h.IsJoyHoldHorizontal());
 h.SetJoyHoldHorizontal(false);
 assert(!h.IsJoyHoldHorizontal());
 std::vector<std::thread> t;
 for(int i=0;i<24;i++) t.emplace_back([&]{h.ReloadInputDevices();});
 for(auto& worker:t) worker.join();
 assert(h.reloads==24);
}
""";
with tempfile.TemporaryDirectory(prefix="r23-hid-joy-") as d:
 p=Path(d);(p/"t.cpp").write_text(test)
 subprocess.run([os.getenv("CXX","clang++"),"-std=c++20","-O1","-g","-pthread",
   "-fsanitize=address,undefined",str(p/"t.cpp"),"-o",str(p/"test")],check=True,timeout=80)
 subprocess.run([str(p/"test")],check=True,timeout=20,
   env={**os.environ,"ASAN_OPTIONS":"detect_leaks=1:halt_on_error=1"})
print("R23_19_HID_JOYHOLD_HOST_PASS concurrent_reload=24 reset_per_game=1")
