#!/usr/bin/env python3
"""NS1 R23 #64: hidden HWC producer never acquired; last composite retired before release."""
from pathlib import Path
import hashlib
import os
import subprocess
import tempfile
p=Path(__file__).resolve().parents[1]/"encore-active/eden-core/src/core/hle/service/nvnflinger/hardware_composer.cpp"
s=p.read_text()
raw=p.read_bytes()
sha=hashlib.sha1(b"blob "+str(len(raw)).encode()+bytes([0])+raw).hexdigest()
assert sha=="2202af6879b4c38c3d9322031c4b49a6ceb58603",sha
assert "std::none_of(display.stack.layers.begin(), display.stack.layers.end()" in s
assert "if (!layer->visible) {\n            continue;\n        }" in s
assert s.index("nvdisp.WaitForComposite();") < s.index("this->ReleaseFramebuffersLocked(display);")
assert s.index("this->ReleaseFramebuffersLocked(display);") < s.index("this->CacheFramebufferLocked(*layer, consumer_id)")
assert not s.split("m_frame_number += 1;",1)[1].split("return 1;",1)[0].count("ReleaseFramebuffersLocked")
code=r"""
#include <algorithm>
#include <cassert>
#include <vector>
struct Layer{bool visible;};
struct Composer{
 int waits=0,releases=0,acquires=0;
 std::vector<Layer> layers;
 int Compose(){
    if (std::none_of(layers.begin(),layers.end(),
                     [](const auto& layer){ return layer.visible; })) return 1;
    ++waits;
    ++releases;
    for(auto& layer:layers) {
      if (!layer.visible) continue;
      ++acquires;
    }
    return 1;
 }
};
int main(){
 Composer c;
 c.layers={{false},{false}};
 c.Compose();assert(c.waits==0 && c.acquires==0);
 c.layers={{false},{true},{false}};
 c.Compose();assert(c.waits==1 && c.releases==1 && c.acquires==1);
 c.layers={{true},{true}};
 c.Compose();assert(c.waits==2 && c.releases==2 && c.acquires==3);
}
""";
with tempfile.TemporaryDirectory(prefix="hwc-64-") as tmp:
 d=Path(tmp)
 (d/"x.cpp").write_text(code)
 subprocess.run([os.environ.get("CXX","clang++"),"-std=c++20","-O1","-g","-fsanitize=address,undefined",
                 str(d/"x.cpp"),"-o",str(d/"test")],check=True,timeout=60)
 subprocess.run([str(d/"test")],check=True,timeout=20,
                env={**os.environ,"ASAN_OPTIONS":"halt_on_error=1"})
print("NS1_R23_HWC_VISIBILITY_SOURCE_HOST_PASS hidden=0 visible=1 waited_release=1")
