// SPDX-License-Identifier: MIT
#include "video_core/renderer_vulkan/nr_rdna2_unet_bottleneck_link.h"
namespace Vulkan {
namespace NR=Eden::Graphics::NeuralRDNA2;
namespace {
[[nodiscard]] bool SameView(VkDescriptorBufferInfo a,
                            VkDescriptorBufferInfo b) noexcept {
    return a.buffer&&b.buffer&&a.buffer==b.buffer&&
           a.offset==0u&&b.offset==0u&&
           a.range>0u&&a.range==b.range&&
           a.range!=VK_WHOLE_SIZE;
}
}
bool NrRdna2UNetBottleneckLink::Bind(
    const NR::NativeUNetSkipMap& graph,
    const NR::NativeSplitRoutes& routes,
    const NR::NativeVitRoute& vit,
    NrRdna2UNetDownSequence& down,
    NrRdna2SplitSwinRun& first,
    NrRdna2NeuralBottleneck& bottleneck){
    if(bind_attempted)return false;
    bind_attempted=true;
    if(!down.Ready()||!first.Ready()||!bottleneck.Ready()||
       down.SchedulerIdentity()!=&scheduler||
       first.SchedulerIdentity()!=&scheduler||
       bottleneck.SchedulerIdentity()!=&scheduler)return false;
    // Bind a prepared output before any GPU command is enqueued. The
    // required recorded producer check moves to Record() below.
    const auto low=down.PreparedLowRaster();
    const auto source=first.SourceRaster();
    const auto core_source=bottleneck.PreparedInputRaster();
    const auto output=bottleneck.PreparedResult();
    if(!SameView(low,source)||!SameView(source,core_source)||
       !output.buffer||output.offset||!output.range||
       output.range==VK_WHOLE_SIZE||output.buffer==low.buffer||
       !NR::CanNativeUNetBottleneckLink(
           graph,routes,vit,low.range,source.range,output.range))
        return false;
    producer=&down;
    this->source=low;
    core=&bottleneck;
    result=output;
    ready=true;
    return true;
}
bool NrRdna2UNetBottleneckLink::Record(){
    if(!ready||record_attempted||!core||!producer||
       producer->SchedulerIdentity()!=&scheduler||
       !SameView(producer->LowRaster(),source))return false;
    record_attempted=true;
    if(!core->Record()||!SameView(core->Result(),result))return false;
    recorded=true;
    return true;
}
} // namespace Vulkan
