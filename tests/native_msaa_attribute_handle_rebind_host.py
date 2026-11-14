#!/usr/bin/env python3
"""Execute the actual selected rasterizer's handle-setup and fallback fragment.
Ordinary desktop C++ mock fixture only; no PS5 build, assets or GPU driver.
"""
from pathlib import Path
from native_msaa_handle_receipt import restore_pre_rebind
import hashlib
import os
import subprocess
import tempfile
root=Path(__file__).resolve().parents[1]
src=(root/"encore-active/eden-vulkan/vulkan_rasterizer.cpp").read_bytes()
def git_blob(raw):
    return hashlib.sha1(b"blob "+str(len(raw)).encode()+b"\0"+raw).hexdigest()
assert git_blob(src)=="efd77028f0710ea9e50cbbe77b04eebf3f306610"
text=src.decode()
assert git_blob(restore_pre_rebind(text).encode())=="5b04321fa45d6b5e6af1d4b17bcf695348d0dd90"
anchor="    const auto prepare_attribute_handles = [&] {"
end="    configure_phase.Stop();"
assert text.count(anchor)==1 and text.count("prepare_attribute_handles();")==2
start=text.index(anchor)
finish=text.index(end,start)
fragment=text[start:finish]
assert "if (!pipeline_ok && pipeline->UsesForcedMsaaShadow())" in fragment
assert "prepared_pipeline = pipeline;" in fragment
assert fragment.index("prepare_attribute_handles();",fragment.index("pipeline = pipeline_cache.CurrentGraphicsPipeline();")) < fragment.index("pipeline_ok = pipeline->Configure(is_indexed);",fragment.index("pipeline = pipeline_cache.CurrentGraphicsPipeline();"))
fixture=r'''
#include <cassert>
#include <vector>
#include <optional>
#include <iostream>
constexpr int VK_SAMPLE_COUNT_1_BIT=1;
struct Run { int handle; };
struct Pipeline {
    bool forced, handles, mesh, succeeds;
    int configured=0, engine=0, bound=-1;
    bool UsesForcedMsaaShadow() const {return forced;}
    bool UsesAttributeHandle() const {return handles;}
    bool IsMeshPipeline() const {return mesh;}
    void SetAttributeHandle(int h){ bound=h; }
    void SetEngine(int,int){ ++engine; }
    bool Configure(bool){ ++configured; return succeeds; }
};
struct Cache { Pipeline* guest; Pipeline* CurrentGraphicsPipeline(){return guest;} };
struct Shadow { unsigned resolve=0; bool EndPrepared(){++resolve;return true;} };
void run(bool shadow_succeeds,bool guest_handles,bool guest_mesh,bool split,bool fallback_exists) {
    Pipeline shadow{true,true,false,shadow_succeeds};
    Pipeline guest{false,guest_handles,guest_mesh,true};
    Cache pipeline_cache{fallback_exists?&guest:nullptr};
    Shadow shadow_pass;
    auto* native_msaa_shadow=&shadow_pass;
    std::optional<int> native_msaa_clear=1;
    int native_samples=4,maxwell3d=0,gpu_memory=0;
    auto* pipeline=&shadow;
    Pipeline* prepared_pipeline=pipeline;
    Pipeline* probe_pipeline=pipeline;
    Pipeline* attribute_handle_pipeline=nullptr;
    std::vector<Run> attribute_handle_runs;
    bool split_attribute_handles=split,is_indexed=true;
    unsigned collect=0;
    auto CollectAttributeHandleRuns=[&](bool indexed){
        assert(indexed); ++collect; attribute_handle_runs={{37},{91}};
    };
''' + fragment + r'''
    assert(shadow.configured==1);
    if(shadow_succeeds){
        assert(pipeline_ok && prepared_pipeline==&shadow && probe_pipeline==&shadow);
        assert(shadow_pass.resolve==0 && guest.configured==0);
        assert(attribute_handle_pipeline==(split?&shadow:nullptr));
        assert(collect==(split?1u:0u));
        return;
    }
    assert(shadow_pass.resolve==1 && !native_msaa_clear && native_samples==1);
    if(!fallback_exists){assert(!pipeline_ok && guest.configured==0);return;}
    assert(pipeline_ok && prepared_pipeline==&guest && probe_pipeline==&guest && guest.engine==1 && guest.configured==1);
    const bool expected=split && guest_handles && !guest_mesh;
    assert(attribute_handle_pipeline==(expected?&guest:nullptr));
    assert(attribute_handle_runs.size()==(expected?2u:0u));
    assert(guest.bound==(expected?37:-1));
    assert(collect==unsigned(split)+unsigned(expected));
    // Simulate the later split run selecting the SAME pipeline as the draw.
    if(expected){attribute_handle_pipeline->SetAttributeHandle(91);assert(guest.bound==91 && shadow.bound==37);}
}
int main(){
    for(bool ok:{false,true})for(bool handles:{false,true})for(bool mesh:{false,true})
    for(bool split:{false,true})for(bool exists:{false,true})run(ok,handles,mesh,split,exists);
    std::cout<<"NATIVE_MSAA_HANDLE_REBIND_HOST_PASS cases=32 original_selected_source=1 native_gpu_unverified=1\n";
}
'''
with tempfile.TemporaryDirectory(prefix="encore-msaa-handle-") as d:
    cpp=Path(d)/"fixture.cpp"
    exe=Path(d)/"fixture"
    cpp.write_text(fixture)
    subprocess.run([os.environ.get("CXX","c++"),"-std=c++20","-Wall","-Wextra","-Werror","-fsanitize=undefined",str(cpp),"-o",str(exe)],check=True)
    subprocess.run([str(exe)],check=True)
