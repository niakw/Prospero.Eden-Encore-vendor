// SPDX-License-Identifier: MIT
#include "video_core/renderer_vulkan/nr_rdna2_proxy_lift_link.h"
namespace Vulkan {
namespace NR=Eden::Graphics::NeuralRDNA2;
namespace {
[[nodiscard]] bool SameResource(
    VkDescriptorBufferInfo a,VkDescriptorBufferInfo b) noexcept {
    return a.buffer&&b.buffer&&a.buffer==b.buffer&&
           !a.offset&&!b.offset&&a.range==b.range&&
           a.range!=0u&&a.range!=VK_WHOLE_SIZE;
}
}
NrRdna2ProxyLiftLink::NrRdna2ProxyLiftLink(
    Scheduler& s):scheduler{s}{}
bool NrRdna2ProxyLiftLink::Bind(
    NrRdna2ProxyFeatureStage& p,NrRdna2ImageFeatureStage& l){
    if(ready||attempted||!p.Ready()||!l.Ready()||
       p.SchedulerIdentity()!=&scheduler||
       l.SchedulerIdentity()!=&scheduler||
       !NR::CanNeuralProxyLiftLink(p.Geometry(),l.Geometry())||
       !SameResource(p.Result(),l.Input())||
       p.Input().buffer==l.Result().buffer)
        return false;
    proxy=&p;
    lift=&l;
    ready=true;
    return true;
}
bool NrRdna2ProxyLiftLink::Record(){
    if(!ready||attempted||!proxy||!lift||
       !proxy->Ready()||!lift->Ready())return false;
    attempted=true;
    successful=proxy->Record()&&lift->Record();
    return successful;
}
} // namespace Vulkan
