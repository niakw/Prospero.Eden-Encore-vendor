// SPDX-License-Identifier: MIT
#include "video_core/renderer_vulkan/nr_rdna2_fswin_layer_runtime.h"
#include "video_core/renderer_vulkan/vk_scheduler.h"
#include "video_core/vulkan_common/vulkan_device.h"
#include "video_core/vulkan_common/vulkan_memory_allocator.h"
#include <utility>
namespace Vulkan {
using Eden::Graphics::NeuralRDNA2::FSwinGraph;
using Eden::Graphics::NeuralRDNA2::FSwinLayerWeightRanges;
using Eden::Graphics::NeuralRDNA2::NeuralWeightArena;
using Eden::Graphics::NeuralRDNA2::NeuralModelFile;
using Eden::Graphics::NeuralRDNA2::MakeFSwinGraphPlan;
using Eden::Graphics::NeuralRDNA2::PrepareFSwinLayerWeights;
using Eden::Graphics::NeuralRDNA2::BindFSwinGraphWeights;
NrRdna2FSwinLayerRuntime::NrRdna2FSwinLayerRuntime(
    const Device& device,MemoryAllocator& allocator,Scheduler& scheduler)
    :scheduler{scheduler},graph{device,scheduler},
     gpu_weights{device,allocator},
     tensors{device,allocator},descriptors{device}{}
NrRdna2FSwinLayerRuntime::~NrRdna2FSwinLayerRuntime()=default;
bool NrRdna2FSwinLayerRuntime::Initialize(
    const NeuralModelFile& model,std::string_view layer_prefix,
    std::uint32_t channels,std::uint32_t windows,bool residual_last){
    if(ready)return false;
    FSwinGraph proposed{};
    if(!MakeFSwinGraphPlan(channels,windows,proposed))return false;
    NeuralWeightArena staging{};
    FSwinLayerWeightRanges ranges{};
    if(!PrepareFSwinLayerWeights(model,layer_prefix,channels,
                                 residual_last,staging,ranges))return false;
    if(!BindFSwinGraphWeights(ranges,staging.Bytes().size(),
                              proposed))return false;
    if(!gpu_weights.Initialize(staging.Bytes()))return false;
    if(!tensors.Initialize(proposed))return false;
    if(!descriptors.Initialize(graph,proposed,
        tensors.Views(gpu_weights.Descriptor())))return false;
    plan=proposed;
    ready=true;
    return true;
}
bool NrRdna2FSwinLayerRuntime::UploadInput(
    std::span<const std::uint8_t> e4m3_window_tokens){
    if(!ready||!tensors.UploadInput(e4m3_window_tokens))return false;
    input_loaded=true;
    output_recorded=false;
    return true;
}
bool NrRdna2FSwinLayerRuntime::LinkInputFrom(
    const NrRdna2FSwinLayerRuntime& previous){
    if(!ready||!previous.ready||!previous.output_recorded||
       &scheduler!=&previous.scheduler||
       plan.dense[0].channels!=previous.plan.dense[0].channels||
       plan.dense[0].windows!=previous.plan.dense[0].windows||
       tensors.InputSize()!=previous.tensors.OutputSize())return false;
    const VkBuffer src=previous.tensors.OutputBuffer();
    const VkBuffer dst=tensors.InputBuffer();
    const VkDeviceSize size=tensors.InputSize();
    if(!src||!dst||src==dst||!size)return false;
    scheduler.RequestOutsideRenderPassOperationContext();
    scheduler.Record([src,dst,size](vk::CommandBuffer cmd){
        const VkBufferMemoryBarrier release{
            .sType=VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER,
            .pNext=nullptr,
            .srcAccessMask=VK_ACCESS_SHADER_WRITE_BIT,
            .dstAccessMask=VK_ACCESS_TRANSFER_READ_BIT,
            .srcQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED,
            .dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED,
            .buffer=src,.offset=0,.size=size};
        cmd.PipelineBarrier(VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                            VK_PIPELINE_STAGE_TRANSFER_BIT,0,release);
        const VkBufferCopy copy{.srcOffset=0,.dstOffset=0,.size=size};
        cmd.CopyBuffer(src,dst,copy);
        const VkBufferMemoryBarrier acquire{
            .sType=VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER,
            .pNext=nullptr,
            .srcAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT,
            .dstAccessMask=VK_ACCESS_SHADER_READ_BIT,
            .srcQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED,
            .dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED,
            .buffer=dst,.offset=0,.size=size};
        cmd.PipelineBarrier(VK_PIPELINE_STAGE_TRANSFER_BIT,
                            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,0,acquire);
    });
    input_loaded=true;
    output_recorded=false;
    return true;
}
bool NrRdna2FSwinLayerRuntime::LinkInputFromShifted(
    const NrRdna2FSwinLayerRuntime& previous,
    NrRdna2FSwinWindowRepack& repacker,VkDescriptorSet validated_set,
    const Eden::Graphics::NeuralRDNA2::FSwinWindowRepack& layout){
    if(!ready||!previous.ready||!previous.output_recorded||
       !validated_set||&scheduler!=&previous.scheduler||
       plan.dense[0].channels!=previous.plan.dense[0].channels||
       plan.dense[0].windows!=previous.plan.dense[0].windows||
       tensors.InputSize()!=previous.tensors.OutputSize()||
       layout.channels!=plan.dense[0].channels||
       std::uint64_t(layout.windows_x)*layout.windows_y!=
           plan.dense[0].windows)return false;
    const auto src=previous.Output();
    const auto dst=Input();
    if(!src.buffer||!dst.buffer||src.buffer==dst.buffer||
       src.offset||dst.offset||src.range!=dst.range||
       !repacker.Record(validated_set,layout,{src.range,dst.range}))
        return false;
    input_loaded=true;
    output_recorded=false;
    return true;
}
bool NrRdna2FSwinLayerRuntime::Record(){
    if(!ready||!input_loaded||
       !graph.Record(descriptors.Sets(),plan))return false;
    output_recorded=true;
    return true;
}
} // namespace Vulkan
