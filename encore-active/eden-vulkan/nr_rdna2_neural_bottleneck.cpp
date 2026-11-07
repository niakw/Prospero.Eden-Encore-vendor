// SPDX-License-Identifier: MIT
#include "video_core/renderer_vulkan/nr_rdna2_neural_bottleneck.h"
namespace Vulkan {
namespace NR=Eden::Graphics::NeuralRDNA2;
namespace {
[[nodiscard]] bool SameBuffer(
    VkDescriptorBufferInfo a,VkDescriptorBufferInfo b) noexcept {
    return a.buffer&&b.buffer&&a.buffer==b.buffer&&
           a.offset==b.offset&&a.offset==0u&&
           a.range>0u&&a.range!=VK_WHOLE_SIZE&&
           a.range==b.range;
}
}
NrRdna2NeuralBottleneck::NrRdna2NeuralBottleneck(
    Scheduler& s):scheduler{s}{}
bool NrRdna2NeuralBottleneck::Bind(
    const NR::NativeSplitRoutes& routes,
    const NR::NativeVitRoute& route,
    NrRdna2SplitSwinRun& a,NrRdna2VitRun& b,
    NrRdna2NativeDecoderLink& c,NrRdna2SplitSwinRun& d){
    if(ready||recorded||
       !NR::CanLinkNativeNrBottleneckRoutes(routes,route)||
       !a.Ready()||!b.Ready()||!c.Ready()||!d.Ready()||
       a.SchedulerIdentity()!=&scheduler||
       b.SchedulerIdentity()!=&scheduler||
       c.SchedulerIdentity()!=&scheduler||
       d.SchedulerIdentity()!=&scheduler||
       a.BlockCount()!=routes.runs[0].count||
       d.BlockCount()!=routes.runs[1].count||
       b.BlockCount()!=route.count||
       !a.RequiresInputView()||a.RequiresOutputView()||
       d.RequiresInputView()||!d.RequiresOutputView()||
       !SameBuffer(a.Result(),b.SourceSplitTiles())||
       !SameBuffer(a.PrimaryResult(),c.RawSplitSkip())||
       !SameBuffer(b.DecoderTiles(),c.Input())||
       !SameBuffer(c.Result(),d.Input()))
        return false;
    first=&a;
    vit=&b;
    decoder=&c;
    second=&d;
    ready=true;
    return true;
}
bool NrRdna2NeuralBottleneck::Record(){
    if(!ready||recorded||!first||!vit||!decoder||!second||
       !first->Ready()||!vit->Ready()||
       !decoder->Ready()||!second->Ready())
        return false;
    // An exception or command failure must not silently re-record a
    // partially enqueued neural graph. The embedding renderer owns
    // the fence/resource lifetime and must recreate this graph.
    recorded=true;
    return first->Record()&&vit->Record()&&
           decoder->Record()&&second->Record();
}
} // namespace Vulkan
