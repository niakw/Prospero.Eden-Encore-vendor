#!/usr/bin/env python3
"""R23 #46: actual cheat parser helper+method under ASan/UBSan and fuzz input."""
from pathlib import Path
import hashlib
import os
import subprocess
import tempfile

root=Path(__file__).resolve().parents[1]
src=root/"encore-active/eden-core/src/core/memory/cheat_engine.cpp"
s=src.read_text()
data=s.encode()
sha=hashlib.sha1(b"blob "+str(len(data)).encode()+bytes([0])+data).hexdigest()
assert sha=="c41b7a1ed82b1e69c93c28b1005d86478152893a",sha
assert "out_name_size = end_index - start_index;" in s
assert "std::string_view::npos" in s
assert "data.size() - i < 8" in s
assert "i += 7; // for-loop increment moves to the next byte" in s
assert "data.back() == '\\n'" in s

helper_start="std::string_view ExtractName("
helper_end="\n} // Anonymous namespace"
parser_start="std::vector<CheatEntry> TextCheatParser::Parse(std::string_view data) const {"
parser_end="\nCheatEngine::CheatEngine("
helper=helper_start+s.split(helper_start,1)[1].split(helper_end,1)[0]
parser=parser_start+s.split(parser_start,1)[1].split(parser_end,1)[0]
fixture=r"""
#include <algorithm>
#include <array>
#include <cassert>
#include <cctype>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <locale>
#include <optional>
#include <random>
#include <string>
#include <string_view>
#include <vector>
namespace Core::Memory {
using u64=std::uint64_t;
using u32=std::uint32_t;
struct CheatDefinition {
 std::array<char,64> readable_name{};
 u32 num_opcodes{};
 std::array<u32,256> opcodes{};
};
struct CheatEntry {
 bool enabled{};
 u32 cheat_id{};
 CheatDefinition definition{};
};
class TextCheatParser {
public:
 std::vector<CheatEntry> Parse(std::string_view data) const;
};
""" + helper + "\n" + parser + r"""
}
int main() {
 Core::Memory::TextCheatParser p;
 auto result=p.Parse("[x]123456789ABCDEF0");
 assert(result.size()==2);
 assert(result[1].definition.num_opcodes==2);
 assert(result[1].definition.opcodes[0]==0x12345678u);
 assert(result[1].definition.opcodes[1]==0x9ABCDEF0u);
 assert(p.Parse("[x]1234567").empty());
 assert(p.Parse("[x]1234567G").empty());
 assert(p.Parse("[unterminated").empty());
 assert(p.Parse("[x]0x123456").empty());
 assert(p.Parse("{master}FFFFFFFF").size()==1);
 auto valid=p.Parse("["+std::string(100,'Z')+"]12345678");
 assert(valid.size()==2);
 assert(valid[1].definition.readable_name[63]=='\0');
 assert(p.Parse(std::string(4, char(255))).empty());
 std::mt19937 random{12345};
 for(int trial=0;trial<15000;trial++) {
  std::string input;
  const int len=int(random()%140);
  input.reserve(len);
  for(int i=0;i<len;i++) input.push_back(char(random()%256));
  auto parsed=p.Parse(input);
  for(const auto& entry:parsed) assert(entry.definition.num_opcodes<=256);
 }
}
""";
with tempfile.TemporaryDirectory(prefix="r23-cheats-") as td:
 p=Path(td)
 (p/"test.cpp").write_text(fixture)
 subprocess.run([os.getenv("CXX","clang++"),"-std=c++20","-O1","-g","-fsanitize=address,undefined",
                 "-fno-omit-frame-pointer",str(p/"test.cpp"),"-o",str(p/"parser")],
                check=True,timeout=90)
 subprocess.run([str(p/"parser")],check=True,timeout=45,
                env={**os.environ,"ASAN_OPTIONS":"detect_leaks=1:halt_on_error=1"})
print("NS1_R23_46_CHEAT_PARSER_ASAN_UBSAN_PASS fuzz=15000 malformed_oob=0 contiguous_opcodes=2")
