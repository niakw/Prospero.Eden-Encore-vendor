// SPDX-License-Identifier: MIT
#include "video_core/renderer_vulkan/nr_rdna2_unet_upsample_link.h"
#include <cstdint>
namespace Vulkan {
using Eden::Graphics::NeuralRDNA2::NativeUNetSkipMap;
using Eden::Graphics::NeuralRDNA2::NeuralModelFile;
NrRdna2UNetUpsampleLink::NrRdna2UNetUpsampleLink(
    const Device& d,MemoryAllocator& a,Scheduler& s)
    :stage{d,a,s},scheduler{s}{}
NrRdna2UNetUpsampleLink::~NrRdna2UNetUpsampleLink()=default;

bool NrRdna2UNetUpsampleLink::Prepare(
    const NativeUNetSkipMap& graph,NrRdna2UNetSkipCache& skips,
    const NeuralModelFile& model,
    std::uint32_t up_block,std::uint32_t low_source_block,
    std::string_view prefix,VkDescriptorBufferInfo low,
    VkDescriptorBufferInfo output,NrRdna2FSwinChain& chain){
    if(ready||!model.IsOpen()||prefix.empty()||graph.count!=4u||
       skips.SchedulerIdentity()!=&scheduler||
       chain.SchedulerIdentity()!=&scheduler||
       !chain.AllowsGpuRasterInput())return false;
    const Eden::Graphics::NeuralRDNA2::NativeUNetSkipEdge* edge=nullptr;
    for(std::uint32_t i=0u;i<graph.count;++i){
        if(graph.edges[i].up_block==up_block)edge=&graph.edges[i];
    }
    if(!edge||edge->up_input_block!=low_source_block)return false;
    // Native plan axes are [height,width], not Vulkan [x,y].
    // This matches the recovered 1920x1080 graph:
    // C256: raster 67x120 and low raster 33x60 (odd edge clamped).
    const auto width=edge->skip_height;
    const auto height=edge->skip_width;
    const auto input_layout=chain.InputWindowLayout();
    if(!width||!height||!edge->channels||
       chain.Channels()!=edge->channels||
       (std::uint64_t(width)+7u)/8u!=edge->skip_windows_x||
       (std::uint64_t(height)+7u)/8u!=edge->skip_windows_y||
       input_layout.windows_x!=edge->skip_windows_x||
       input_layout.windows_y!=edge->skip_windows_y||
       edge->up_input_width!=edge->skip_width/2u||
       edge->up_input_height!=edge->skip_height/2u)
        return false;
    const auto skip=skips.ForUpsample(up_block);
    if(!skip.buffer||skip.buffer==low.buffer||
       skip.buffer==output.buffer||low.buffer==output.buffer)
        return false;
    if(!stage.Initialize(model,prefix,low,skip,output,
                         width,height,edge->channels))return false;
    if(!chain.BindRasterInput(stage.Output(),width,height))
        return false;
    next=&chain;
    ready=true;
    return true;
}
bool NrRdna2UNetUpsampleLink::Record(){
    if(!ready||recorded||!next)return false;
    // The shader and raster-to-windows adapters insert compute barriers.
    // Never reverse this ordering.
    if(!stage.Record()||!next->Record())return false;
    recorded=true;
    return true;
}
} // namespace Vulkan
