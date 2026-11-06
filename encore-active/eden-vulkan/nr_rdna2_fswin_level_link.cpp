// SPDX-License-Identifier: MIT
#include "video_core/renderer_vulkan/nr_rdna2_fswin_level_link.h"
namespace Vulkan {
using Eden::Graphics::NeuralRDNA2::CanFSwinLevelLink;
using Eden::Graphics::NeuralRDNA2::FSwinLevelLink;
using Eden::Graphics::NeuralRDNA2::NeuralModelFile;
bool NrRdna2FSwinLevelLink::Prepare(
    NrRdna2FSwinChain& from,NrRdna2FSwinChain& to,
    const NeuralModelFile& model,VkDescriptorBufferInfo raster,
    std::uint32_t width,std::uint32_t height){
    if(ready||&from==&to||!from.Ready()||!to.Ready()||
       !from.RequiresDownsampleOutput()||!to.RequiresRasterInput()||
       !raster.buffer||raster.offset||raster.range==0u||
       raster.range==VK_WHOLE_SIZE||
       from.SchedulerIdentity()!=to.SchedulerIdentity())
        return false;
    const FSwinLevelLink spec{
        .source_width=width,.source_height=height,
        .source_channels=from.Channels(),
        .destination_channels=to.Channels(),
        .source_grid=from.OutputWindowLayout(),
        .destination_grid=to.InputWindowLayout(),
    };
    if(!CanFSwinLevelLink(spec,raster.range,to.Input().range)||
       raster.buffer==to.Input().buffer||
       raster.buffer==from.Output().buffer)return false;
    if(!from.BindDownsampleOutput(model,raster,width,height))return false;
    if(!to.BindRasterInput(raster,width/2u,
                         height/2u))return false;
    source=&from;
    target=&to;
    ready=true;
    return true;
}
bool NrRdna2FSwinLevelLink::Record(){
    if(!ready||!source||!target)return false;
    // Source stage emits a compute->compute barrier; the destination
    // raster importer does likewise. Never swap this order.
    return source->Record()&&target->Record();
}
} // namespace Vulkan
