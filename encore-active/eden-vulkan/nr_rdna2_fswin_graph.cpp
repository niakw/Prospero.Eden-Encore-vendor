// SPDX-License-Identifier: MIT
#include "video_core/renderer_vulkan/nr_rdna2_fswin_graph.h"
#include "video_core/vulkan_common/vulkan_device.h"
#include "video_core/renderer_vulkan/vk_scheduler.h"
namespace Vulkan {
using Eden::Graphics::NeuralRDNA2::CanFSwinGraph;
using Eden::Graphics::NeuralRDNA2::FSwinGraph;
NrRdna2FSwinGraph::NrRdna2FSwinGraph(
    const Device& device,Scheduler& scheduler)
    :dense{device,scheduler},qkv{device,scheduler},
     attention{device,scheduler}{}
NrRdna2FSwinGraph::~NrRdna2FSwinGraph()=default;

bool NrRdna2FSwinGraph::Record(
    const FSwinGraphDescriptorSets& sets,const FSwinGraph& plan){
    if(!CanFSwinGraph(plan)||!sets.qkv||!sets.attention)
        return false;
    for(const auto& set:sets.dense)if(!set)return false;
    // Each Record queues its own compute->compute storage visibility barrier.
    // Validate every pass BEFORE submitting any command to avoid partial plans.
    for(unsigned i=0;i<3u;++i){
        if(!dense.Record(sets.dense[i],plan.dense[i],plan.dense_storage[i]))
            return false;
    }
    if(!qkv.Record(sets.qkv,plan.qkv,plan.qkv_storage))return false;
    if(!attention.Record(sets.attention,plan.attention,
                          plan.attention_storage))return false;
    if(!dense.Record(sets.dense[3],plan.dense[3],
                     plan.dense_storage[3]))return false;
    return true;
}
} // namespace Vulkan
