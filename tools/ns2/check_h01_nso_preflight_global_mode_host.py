#!/usr/bin/env python3
"""H01: execute actual 13-module ExeFS NSO0 preflight before NS2 global mode.

Source-extract the maintained DeconstructedRomDirectory loader's complete
module-admission loop and execute it with the genuine ns2_nso_preflight.h
validator. A malformed last patched module must reject before GuestCounter or
NCE settings change. Switch 1 retains its previous global selection branch.
Host witness only; KProcess mapping and PS5 execution remain unproven.
"""
from pathlib import Path
import os
import shutil
import subprocess
import tempfile

ROOT=Path(__file__).resolve().parents[2]
OWNER=ROOT/"encore-active/ns2-runtime/metadata/core/loader/deconstructed_rom_directory.cpp"
source=OWNER.read_text()
start="    const std::array static_modules ="
end="\n    std::size_t code_size{};"
assert source.count(start)==source.count(end)==1
owner=start+source.split(start,1)[1].split(end,1)[0]
assert owner.index("PreflightNso(header, module_file->GetSize())") < owner.index(
    "Eden::GuestCounter::SelectProgram(true")
assert owner.index("PreflightNso(header, module_file->GetSize())") < owner.index(
    "Settings::SetNceEnabled(is_39bit")
assert owner.count("return {ResultStatus::ErrorLoadingNSO, {}};")==1
prelude=source.split(start,1)[0]
assert "if (!metadata.IsSwitch2())\n        Eden::GuestCounter::SelectProgram(false," in prelude
assert "if (!metadata.IsSwitch2())\n        Settings::SetNceEnabled(is_39bit" in prelude
assert prelude.index('dir = patch_manager.PatchExeFS(dir);') < prelude.index(
    'metadata.Reload(npdm)')
assert prelude.index('metadata.Reload(npdm)') < prelude.index(
    'if (metadata.IsSwitch2() && !dir->GetFile("main"))')
helper=ROOT/"encore-active/ns2-helpers/include"
cpp=r"""
#include <algorithm>
#include <array>
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <map>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include "core/loader/ns2_nso_preflight.h"
using u8=std::uint8_t;
using u32=std::uint32_t;
using u64=std::uint64_t;
namespace Common {
struct Caps{u64 tsc_frequency{100};};
inline Caps g_cpu_caps{};
}
namespace Eden {
struct GuestCounter {
    static inline unsigned calls{};
    static inline bool mode{};
    static void SelectProgram(bool switch2,u64 hz) {
        assert(hz==100);++calls;mode=switch2;
    }
};
}
namespace Settings {
inline unsigned calls{};
inline bool enabled{};
void SetNceEnabled(bool on){++calls;enabled=on;}
}
namespace FileSys {
struct VfsFile {
    std::array<u8,Eden::NS2::ExeFS::NsoHeaderBytes> header{};
    std::size_t bytes{0x140};
    std::size_t readable{0x100};
    std::size_t ReadBytes(u8* out,std::size_t n,std::size_t pos) const {
        assert(pos==0&&n==header.size());
        std::size_t take=std::min(n,readable);
        std::copy_n(header.data(),take,out);
        return take;
    }
    std::size_t GetSize()const{return bytes;}
};
using VirtualFile=std::shared_ptr<VfsFile>;
struct VfsDir {
    std::map<std::string,VirtualFile> files;
    unsigned gets{};
    VirtualFile GetFile(std::string_view name) {
        ++gets;
        const auto it=files.find(std::string(name));
        return it==files.end()?VirtualFile{}:it->second;
    }
};
using VirtualDir=std::shared_ptr<VfsDir>;
}
#define LOG_ERROR(...) ((void)0)
namespace Loader {
enum class ResultStatus {Success,ErrorLoadingNSO};
struct Metadata {
    bool native{true};
    bool IsSwitch2()const{return native;}
};
struct PreflightHarness {
    FileSys::VirtualDir dir=std::make_shared<FileSys::VfsDir>();
    Metadata metadata{};
    using LoadResult=std::pair<ResultStatus,std::optional<int>>;
    LoadResult PreflightAll(){
        const bool is_39bit=true;
        (void)is_39bit;
        __REAL_OWNER__
        return {ResultStatus::Success,{}};
    }
};
}
static void Word(std::array<u8,0x100>& data,std::size_t off,u32 n){
    for(unsigned b=0;b<4;++b)data[off+b]=static_cast<u8>(n>>(8*b));
}
static FileSys::VirtualFile Valid(){
    auto f=std::make_shared<FileSys::VfsFile>();
    Word(f->header,0,0x304f534eU);
    Word(f->header,0x10,0x100);   // text stored at end of header
    Word(f->header,0x18,0x40);    // text virtual bytes
    Word(f->header,0x24,0x40);    // rodata starts after text
    Word(f->header,0x34,0x40);    // data starts after text
    Word(f->header,0x60,0x40);    // text stored bytes
    assert(Eden::NS2::ExeFS::PreflightNso(f->header,f->bytes).Ready());
    return f;
}
static void Clear(){
    Eden::GuestCounter::calls=0;
    Eden::GuestCounter::mode=false;
    Settings::calls=0;
    Settings::enabled=false;
}
int main(){
    using Harness=Loader::PreflightHarness;
    using Loader::ResultStatus;
    Harness all;
    // Every optional module is present: fail the LAST one.
    for(auto name:{"rtld","main","subsdk0","subsdk1","subsdk2",
                   "subsdk3","subsdk4","subsdk5","subsdk6","subsdk7",
                   "subsdk8","subsdk9","sdk"}){
        all.dir->files.emplace(name,Valid());
    }
    Clear();
    auto sdk=all.dir->files.at("sdk");
    sdk->header[0]=0x12;
    assert(all.PreflightAll().first==ResultStatus::ErrorLoadingNSO);
    assert(all.dir->gets==13);
    assert(Eden::GuestCounter::calls==0&&Settings::calls==0);
    sdk->header[0]='N';
    Clear();
    all.dir->gets=0;
    auto main=all.dir->files.at("main");
    main->readable=0x80;
    assert(all.PreflightAll().first==ResultStatus::ErrorLoadingNSO);
    assert(all.dir->gets==2 && Eden::GuestCounter::calls==0);
    main->readable=main->header.size();
    Clear();
    all.dir->gets=0;
    Word(sdk->header,0x60,0x100); // text extends beyond real file
    assert(all.PreflightAll().first==ResultStatus::ErrorLoadingNSO);
    assert(Settings::calls==0 && Eden::GuestCounter::calls==0);
    Word(sdk->header,0x60,0x40);
    Clear();
    all.dir->gets=0;
    assert(all.PreflightAll().first==ResultStatus::Success);
    assert(all.dir->gets==13 && Settings::calls==1 && Settings::enabled);
    assert(Eden::GuestCounter::calls==1 && Eden::GuestCounter::mode);
    Clear();
    Harness missing;
    missing.dir->files["main"]=Valid();
    assert(missing.PreflightAll().first==ResultStatus::Success);
    assert(Settings::calls==1 && Eden::GuestCounter::calls==1);
    Clear();
    Harness legacy;
    legacy.metadata.native=false;
    legacy.dir->files["main"]=Valid();
    legacy.dir->files["main"]->header[0]=0;
    assert(legacy.PreflightAll().first==ResultStatus::Success);
    assert(legacy.dir->gets==0 && Settings::calls==0 && Eden::GuestCounter::calls==0);
    std::puts("H01_REAL_13_NSO_PREFLIGHT_ATOMIC_MODE_PASS"
              " all_13=admitted malformed_last=rejected"
              " truncated_second=rejected bad_file_extent=rejected"
              " no_global_mode_on_failure=1 ns1_prelude_unchanged=1"
              " KProcess_map=not_exercised");
}
""".replace("__REAL_OWNER__",owner)
compilers=list(dict.fromkeys(x for x in
    (shutil.which("clang++"),shutil.which("g++")) if x))
assert compilers
with tempfile.TemporaryDirectory(prefix="eden-h01-nso-preflight-") as directory:
    file=Path(directory)/"owner.cpp";file.write_text(cpp)
    for cc in compilers:
        for opt in ("-O0","-O2"):
            exe=Path(directory)/(Path(cc).name.replace("+","p")+opt)
            cmd=[cc,"-std=c++20",opt,"-Wall","-Wextra","-Werror",
                 "-DEDEN_NS2_GUEST_COUNTER",
                 "-fsanitize=undefined","-fno-sanitize-recover=all",
                 "-I"+str(helper),str(file),"-o",str(exe)]
            built=subprocess.run(cmd,env=os.environ.copy(),capture_output=True,
                                 text=True,timeout=55)
            assert built.returncode==0,(cc,built.stderr[-6000:])
            got=subprocess.run([str(exe)],env=os.environ.copy(),capture_output=True,
                               text=True,timeout=15)
            assert got.returncode==0,(cc,got.stderr[-5000:])
            print(Path(cc).name,opt,got.stdout.strip(),flush=True)
