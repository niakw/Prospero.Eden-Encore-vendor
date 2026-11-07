// SPDX-License-Identifier: MIT
#include "video_core/renderer_vulkan/nr_rdna2_proxy_fswin_link.h"

namespace Vulkan {
namespace NR=Eden::Graphics::NeuralRDNA2;
namespace {
[[nodiscard]] bool SameBufferView(
    VkDescriptorBufferInfo a,VkDescriptorBufferInfo b) noexcept {
    return a.buffer&&b.buffer&&a.buffer==b.buffer&&
           a.offset==0u&&b.offset==0u&&
           a.range!=0u&&a.range==b.range&&
           a.range!=VK_WHOLE_SIZE;
}
}
NrRdna2ProxyFSwinLink::NrRdna2ProxyFSwinLink(Scheduler& s)
    :scheduler{s},entry{s}{}

bool NrRdna2ProxyFSwinLink::Bind(
    NrRdna2ProxyFeatureStage& proxy,NrRdna2ImageFeatureStage& lift,
    NrRdna2FSwinChain& first_fswin){
    if(attempted||bound)return false;
    attempted=true;
    if(!proxy.Ready()||!lift.Ready()||!first_fswin.Ready()||
       proxy.SchedulerIdentity()!=&scheduler||
       lift.SchedulerIdentity()!=&scheduler||
       first_fswin.SchedulerIdentity()!=&scheduler||
       !first_fswin.RequiresRasterInput()||
       !first_fswin.AllowsGpuRasterInput()||
       !SameBufferView(proxy.Result(),lift.Input())||
       !NR::CanNeuralProxyFSwinLink(
           proxy.Geometry(),lift.Geometry(),
           first_fswin.InputWindowLayout(),first_fswin.Channels(),
           {lift.Result().range,first_fswin.Input().range}))
        return false;

    // No accidental alias between producer resources and the FSwin input
    // owned by the chain, even if their visible ranges have the same size.
    const auto input=first_fswin.Input();
    if(!input.buffer||
       input.buffer==proxy.Input().buffer||
       input.buffer==proxy.Result().buffer||
       input.buffer==lift.Result().buffer||
       proxy.Input().buffer==lift.Result().buffer)
        return false;
    if(!entry.Bind(proxy,lift))return false;
    if(!first_fswin.BindRasterInput(
           lift.Result(),lift.Geometry().width,lift.Geometry().height))
        return false;
    fswin=&first_fswin;
    bound=true;
    return true;
}
bool NrRdna2ProxyFSwinLink::Record(){
    if(!bound||recorded||!fswin||!fswin->Ready())return false;
    recorded=true;
    // Fail before recording the prefix if a terminal output is required.
    if(fswin->RequiresRasterOutput()&&!fswin->ResultRaster().buffer)
        return false;
    if(!entry.Record())return false;
    successful=fswin->Record();
    return successful;
}
} // namespace Vulkan
