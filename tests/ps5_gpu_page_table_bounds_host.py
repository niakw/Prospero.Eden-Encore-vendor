#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Run original negative and maintained real table code under ASan/UBSan."""
from pathlib import Path
import os
import subprocess
import tempfile

ROOT=Path(__file__).resolve().parents[1]
original=ROOT/"sources/eden/src"
fixed=ROOT/"encore-active/eden-core/src/common/multi_level_page_table.cpp"
src=fixed.read_text()
assert "const u64 new_end = (start + size - 1) >> first_level_shift;" in src
assert "if (size == 0)" in src
assert "const u64 new_end = (start + size) >> first_level_shift;" in (original/"common/multi_level_page_table.inc").read_text()
fixture=r"""
#include "common/multi_level_page_table.h"
#include <cassert>
#include <string_view>
template<class T> void check(bool negative) {
    constexpr u64 top=1ULL<<32;
    constexpr size_t page=4096;
    Common::MultiLevelPageTable<T> table(32, 4, 12);
    if (negative) {
        table.ReserveRange(top-page, page);
        return;
    }
    table.ReserveRange(top, 0);
    table.ReserveRange(0,0);
    table.ReserveRange(top-page,page);
    table[(top-page)/page]=T{77};
    assert(table[(top-page)/page]==T{77});
    table.ReserveRange(0,top);
    table[0]=T{91};
    assert(table[0]==T{91});
    table.ReserveRange((1ULL<<28)-page,2*page);
    table[((1ULL<<28)-page)/page]=T{9};
    table[(1ULL<<28)/page]=T{10};
    assert(table[((1ULL<<28)-page)/page]==T{9} && table[(1ULL<<28)/page]==T{10});
    assert(table[(top-page)/page]==T{77});
}
int main(int argc,char** argv) {
    bool negative=argc>1 && std::string_view(argv[1])=="negative";
    check<u32>(negative);
    if (!negative) check<u64>(false);
}
"""
with tempfile.TemporaryDirectory(prefix="gpu-page-table-") as temp:
    work=Path(temp)
    (work/"common").mkdir()
    # Replace only logging/assert reporting, not the real table header or TU.
    (work/"common/assert.h").write_text("#pragma once\n#include <cassert>\n#define ASSERT(x) assert(x)\n")
    (work/"test.cpp").write_text(fixture)
    for label,tu in (("original",original/"common/multi_level_page_table.cpp"),("fixed",fixed)):
        exe=work/label
        subprocess.run([os.getenv("CXX","clang++"),"-std=c++20","-O1","-g",
                        "-fsanitize=address,undefined","-fno-sanitize-recover=all",
                        "-fno-omit-frame-pointer","-I",str(work),"-I",str(original),
                        str(tu),str(work/"test.cpp"),"-o",str(exe)],check=True,timeout=90)
        result=subprocess.run([str(exe)]+(["negative"] if label=="original" else []),
                              capture_output=True,text=True,timeout=25)
        if label=="original":
            assert result.returncode!=0 and "heap-buffer-overflow" in result.stderr,result.stderr
        else:
            assert result.returncode==0,result.stderr
print("GPU_PAGE_TABLE_TOP_RANGE_HOST_PASS original_asan_oob=1 fixed_u32_u64=1 zero=1 whole_range=1 crossing=1")
