#!/usr/bin/env python3
"""R23 #49: complete source parity and real selected header method tests."""
from pathlib import Path
import hashlib
import os
import subprocess
import tempfile

p=Path(__file__).resolve().parents[1]/"encore-active/eden-vulkan/include/video_core/renderer_vulkan/vk_buffer_cache.h"
s=p.read_text()
old="""    void BindTextureBuffer(Buffer& buffer, u32 offset, u32 size,
                           VideoCore::Surface::PixelFormat format) {
        guest_descriptor_queue.AddTexelBuffer(buffer.View(offset, size, format),
                                              buffer.DeviceAddress(), offset, size,
                                              TexelBufferFormat(format));
    }"""
start="    void BindTextureBuffer(Buffer& buffer, u32 offset, u32 size,"
end="\n    bool ShouldLimitDynamicStorageBuffers() const {"
new=start+s.split(start,1)[1].split(end,1)[0].rstrip()
assert s.count(new)==1
raw=s.replace(new,old,1).encode()
sha=hashlib.sha1(b"blob "+str(len(raw)).encode()+bytes([0])+raw).hexdigest()
assert sha=="08b0ad3a99c1509eb23bd6317fdbd9c4b3251b52",sha
assert "guest_descriptor_queue.UsesDescriptorBuffer()" in new
assert "address == 0 ? VK_FORMAT_UNDEFINED" in new
mock=r"""
#include <cassert>
#include <cstdint>
using u32=std::uint32_t;
using VkDeviceAddress=std::uint64_t;
using VkFormat=std::uint32_t;
using VkBufferView=std::uint64_t;
constexpr VkFormat VK_FORMAT_UNDEFINED=0;
constexpr VkBufferView VK_NULL_HANDLE=0;
namespace VideoCore::Surface {
enum class PixelFormat {Normal=1,Invalid=255};
}
struct Buffer {
 VkDeviceAddress addr=0;
 int views=0;
 VkDeviceAddress DeviceAddress() const {return addr;}
 VkBufferView View(u32,u32,VideoCore::Surface::PixelFormat) {++views;return 500;}
};
struct Queue {
 bool use=false;
 int calls=0;
 VkBufferView last_view=999;
 VkFormat last_format=999;
 VkDeviceAddress last_addr=999;
 bool UsesDescriptorBuffer() const {return use;}
 void AddTexelBuffer(VkBufferView view,VkDeviceAddress addr,u32,u32,VkFormat format){
   ++calls;last_view=view;last_addr=addr;last_format=format;
 }
 void AddTexelBuffer(VkBufferView view) {++calls;last_view=view;}
};
struct Runtime {
 Queue guest_descriptor_queue;
 int format_calls=0;
 VkFormat TexelBufferFormat(VideoCore::Surface::PixelFormat f){
  ++format_calls;
  assert(f!=VideoCore::Surface::PixelFormat::Invalid);
  return 37;
 }
""" + new + r"""
};
int main(){
 Runtime r;
 Buffer b;
 r.guest_descriptor_queue.use=true;
 r.BindTextureBuffer(b,0,32,VideoCore::Surface::PixelFormat::Invalid);
 assert(b.views==0 && r.format_calls==0);
 assert(r.guest_descriptor_queue.last_view==VK_NULL_HANDLE);
 assert(r.guest_descriptor_queue.last_format==VK_FORMAT_UNDEFINED);
 assert(r.guest_descriptor_queue.last_addr==0);
 b.addr=123456;
 r.BindTextureBuffer(b,8,32,VideoCore::Surface::PixelFormat::Normal);
 assert(b.views==0 && r.format_calls==1);
 assert(r.guest_descriptor_queue.last_format==37);
 r.guest_descriptor_queue.use=false;
 r.BindTextureBuffer(b,8,32,VideoCore::Surface::PixelFormat::Normal);
 assert(b.views==1 && r.format_calls==1);
 assert(r.guest_descriptor_queue.last_view==500);
}
""";
with tempfile.TemporaryDirectory(prefix="r23-texel-") as td:
 folder=Path(td)
 (folder/"t.cpp").write_text(mock)
 subprocess.run([os.getenv("CXX","clang++"),"-std=c++20","-O1","-g","-fsanitize=address,undefined",str(folder/"t.cpp"),"-o",str(folder/"test")],check=True,timeout=60)
 subprocess.run([str(folder/"test")],check=True,timeout=20,env={**os.environ,"ASAN_OPTIONS":"detect_leaks=1:halt_on_error=1"})
print("R23_49_TEXEL_SOURCE_HOST_PASS descriptor_invalid=1 descriptor_valid=1 legacy_view=1")
