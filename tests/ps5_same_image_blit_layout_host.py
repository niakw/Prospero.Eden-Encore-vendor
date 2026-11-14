#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Compile the actual recorded blit command block against a layout-tracking mock."""
import hashlib
from pathlib import Path
import subprocess
import tempfile
root=Path(__file__).resolve().parents[1]
source=(root/"encore-active/eden-vulkan/vk_texture_cache_cost.cpp").read_text()
lo=source.index("void TextureCacheRuntime::BlitImage(")
hi=source.index("\nvoid TextureCacheRuntime::ConvertImage(",lo)
current=source[lo:hi]
previous=current
for new,old in [["    // Lichii #1466: same-image mip blits must keep the shared image in GENERAL.\n    const VkImageLayout dst_layout =\n        src_image == dst_image ? VK_IMAGE_LAYOUT_GENERAL : VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;\n    scheduler.RequestOutsideRenderPassOperationContext();","    scheduler.RequestOutsideRenderPassOperationContext();"],["                      aspect_mask, is_resolve, dst_layout](vk::CommandBuffer cmdbuf) {","                      aspect_mask, is_resolve](vk::CommandBuffer cmdbuf) {"],[".newLayout = dst_layout,",".newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,"],[".oldLayout = dst_layout,",".oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,"],["                                dst_layout,\n                                MakeImageResolve","                                VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,\n                                MakeImageResolve"],["src_image, VK_IMAGE_LAYOUT_GENERAL, dst_image, dst_layout,","src_image, VK_IMAGE_LAYOUT_GENERAL, dst_image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,"]]:
    assert previous.count(new)==1
    previous=previous.replace(new,old,1)
prior=(source[:lo]+previous+source[hi:]).encode()
assert hashlib.sha1(b"blob "+str(len(prior)).encode()+bytes([0])+prior).hexdigest()=="2b344e2c9adce18a7037194cc4d5f81a5c6df144"
def block(s):
    a=s.index("    const VkImage dst_image = dst.ImageHandle();")
    b=s.index("    });",a)+len("    });")
    return s[a:b]
prefix=r"""
#include <array>
#include <cassert>
#include <cstdint>
#include <cstdio>
using VkImage=int;
using VkImageLayout=int;
using VkFilter=int;
enum { VK_IMAGE_LAYOUT_GENERAL=1, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL=2,
       VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER=3, VK_QUEUE_FAMILY_IGNORED=4,
       VK_REMAINING_MIP_LEVELS=5,VK_REMAINING_ARRAY_LAYERS=6,
       VK_FILTER_LINEAR=7,VK_FILTER_NEAREST=8 };
enum { VK_ACCESS_SHADER_WRITE_BIT=1,VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT=2,
       VK_ACCESS_TRANSFER_WRITE_BIT=4,VK_ACCESS_TRANSFER_READ_BIT=8,
       VK_ACCESS_SHADER_READ_BIT=16,VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT=32 };
struct Range{int aspectMask,baseMipLevel,levelCount,baseArrayLayer,layerCount;};
struct VkImageMemoryBarrier{
    int sType; const void* pNext; int srcAccessMask,dstAccessMask;
    int oldLayout,newLayout,srcQueueFamilyIndex,dstQueueFamilyIndex;
    int image;Range subresourceRange;
};
struct VkImageSubresourceLayers{int level;};
struct View{int id,level;int ImageHandle()const{return id;}};
VkImageSubresourceLayers MakeSubresourceLayers(View* v){return {v->level};}
struct Region2D{int width;};
struct Copy{int src,dst;};
Copy MakeImageBlit(Region2D,Region2D,VkImageSubresourceLayers d,VkImageSubresourceLayers s){return {s.level,d.level};}
Copy MakeImageResolve(Region2D d,Region2D s,VkImageSubresourceLayers dl,VkImageSubresourceLayers sl){return MakeImageBlit(d,s,dl,sl);}
namespace Fermi2D {enum class Filter{Nearest,Bilinear};}
struct State{std::array<int,3> layout{1,1,1};int errors=0,read=0,write=0,blits=0,resolves=0,filter=0;};
namespace vk{
enum{PIPELINE_STAGE_GRAPHICS_COMPUTE_TRANSFER=10,PIPELINE_STAGE_TRANSFER_BIT=11,PIPELINE_STAGE_GRAPHICS_COMPUTE=12};
struct CommandBuffer{
    State* s;
    void barrier(const VkImageMemoryBarrier& b){
        assert(b.subresourceRange.baseMipLevel==0&&b.subresourceRange.levelCount==VK_REMAINING_MIP_LEVELS);
        assert(b.subresourceRange.baseArrayLayer==0&&b.subresourceRange.layerCount==VK_REMAINING_ARRAY_LAYERS);
        if(s->layout[b.image]!=b.oldLayout)++s->errors;
        s->layout[b.image]=b.newLayout;
    }
    void PipelineBarrier(int a,int b,int,decltype(nullptr),decltype(nullptr),const std::array<VkImageMemoryBarrier,2>& bs){
        assert(a==PIPELINE_STAGE_GRAPHICS_COMPUTE_TRANSFER&&b==PIPELINE_STAGE_TRANSFER_BIT);
        assert(bs[0].dstAccessMask==VK_ACCESS_TRANSFER_READ_BIT&&bs[1].dstAccessMask==VK_ACCESS_TRANSFER_WRITE_BIT);
        for(auto& x:bs)barrier(x);
        ++s->read;
    }
    void PipelineBarrier(int a,int b,int,const VkImageMemoryBarrier& x){
        assert(a==PIPELINE_STAGE_TRANSFER_BIT&&b==PIPELINE_STAGE_GRAPHICS_COMPUTE);
        assert(x.srcAccessMask==VK_ACCESS_TRANSFER_WRITE_BIT);
        assert(x.dstAccessMask&VK_ACCESS_SHADER_READ_BIT);
        barrier(x);++s->write;
    }
    void BlitImage(int src,int sl,int dst,int dl,Copy c,int f){
        if(s->layout[src]!=sl||s->layout[dst]!=dl)++s->errors;
        if(src==dst)assert(c.src!=c.dst);
        ++s->blits;s->filter=f;
    }
    void ResolveImage(int src,int sl,int dst,int dl,Copy){
        assert(src!=dst);
        if(s->layout[src]!=sl||s->layout[dst]!=dl)++s->errors;
        ++s->resolves;
    }
};}
constexpr int VK_PIPELINE_STAGE_TRANSFER_BIT=vk::PIPELINE_STAGE_TRANSFER_BIT;
struct Scheduler{State state;bool outside=false;
    void RequestOutsideRenderPassOperationContext(){outside=true;}
    template<class F>void Record(F fn){assert(outside);fn(vk::CommandBuffer{&state});}
};
State replay(bool same,bool is_resolve,Fermi2D::Filter filter){
    View src{1,0},dst{same?1:2,1};
    Region2D src_region{4},dst_region{2};int aspect_mask=1;Scheduler scheduler;
"""
suffix=r"""
    return scheduler.state;
}
int main(int argc,char** argv){
    assert(argc==2);bool fixed=argv[1][0]=='1';
    for(bool same:{false,true})for(auto filter:{Fermi2D::Filter::Nearest,Fermi2D::Filter::Bilinear}){
        auto v=replay(same,false,filter);
        assert((v.errors==0)==(!same||fixed));
        assert(v.read==1&&v.write==1&&v.blits==1&&v.resolves==0);
        assert(v.filter==(filter==Fermi2D::Filter::Bilinear?VK_FILTER_LINEAR:VK_FILTER_NEAREST));
        assert(v.layout[1]==VK_IMAGE_LAYOUT_GENERAL&&v.layout[2]==VK_IMAGE_LAYOUT_GENERAL);
    }
    auto v=replay(false,true,Fermi2D::Filter::Nearest);
    assert(v.errors==0&&v.read==1&&v.write==1&&v.resolves==1&&v.blits==0);
    std::printf("BLIT_LAYOUT_WITNESS same_image_valid=%d distinct_blits=2 resolve=1\n",fixed);
}
"""
with tempfile.TemporaryDirectory() as d:
    d=Path(d)
    for label,code,value in (("historic",previous,"0"),("candidate",current,"1")):
        (d/"test.cpp").write_text(prefix+block(code)+suffix)
        for opt in ("-O0","-O2"):
            subprocess.run(["c++","-std=c++20",opt,"-Wall","-Wextra","-Werror",
                            "-fsanitize=undefined","-fno-sanitize-recover=all",
                            str(d/"test.cpp"),"-o",str(d/"test")],check=True,timeout=60)
            subprocess.run([str(d/"test"),value],check=True,timeout=10)
            print("BLIT_LAYOUT_CASE",label,opt)
print("BLIT_LAYOUT_HOST_ONLY actual recorded block, mock Vulkan layout state, no GPU execution")
