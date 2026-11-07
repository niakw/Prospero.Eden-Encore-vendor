// SPDX-License-Identifier: MIT
// Same-resolution FSwin chain; supports explicit 4x4-tile shifts between
// adjacent blocks. Unknown layouts and resolution changes fail closed.
#include "video_core/renderer_vulkan/nr_rdna2_fswin_chain.h"
#include "video_core/renderer_vulkan/nr_rdna2_fswin_raster_host.h"
#include "video_core/renderer_vulkan/nr_rdna2_native_plan_bridge.h"
#include "video_core/renderer_vulkan/present/util.h"
#include "video_core/renderer_vulkan/vk_scheduler.h"
#include "video_core/vulkan_common/vulkan_device.h"
#include "video_core/vulkan_common/vulkan_memory_allocator.h"
#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <utility>
#include <vector>
namespace Vulkan {
using Eden::Graphics::NeuralRDNA2::NeuralModelFile;
using Eden::Graphics::NeuralRDNA2::FSwinWindowLayout;
using Eden::Graphics::NeuralRDNA2::FSwinWindowRepack;
using Eden::Graphics::NeuralRDNA2::FSwinNeedsWindowRepack;
using Eden::Graphics::NeuralRDNA2::CanFSwinWindowRepack;
using Eden::Graphics::NeuralRDNA2::CanFSwinChainWindowLayouts;

NrRdna2FSwinChain::NrRdna2FSwinChain(
    const Device& d,MemoryAllocator& a,Scheduler& s)
    :device{d},allocator{a},scheduler{s},
     window_repack{d,s},raster_input{d,s}{}
NrRdna2FSwinChain::~NrRdna2FSwinChain()=default;

bool NrRdna2FSwinChain::Initialize(
    const NeuralModelFile& model,
    std::span<const FSwinLayerSpec> specs){
    if(ready||!model.IsOpen()||specs.empty()||specs.size()>160u)
        return false;
    const auto channels=specs.front().channels;
    const auto windows=specs.front().windows;
    std::vector<FSwinWindowLayout> proposed_layouts;
    proposed_layouts.reserve(specs.size());
    for(std::size_t i=0u;i<specs.size();++i){
        const auto& spec=specs[i];
        if(spec.prefix.empty()||spec.channels!=channels||
           spec.windows!=windows||
           (spec.input_view&&i!=0u)||
           (spec.output_view&&i+1u!=specs.size())||
           (spec.downsample&&i+1u!=specs.size())||
           (spec.output_view&&spec.downsample)||
           (spec.input_view&&(spec.output_view||spec.downsample)))return false;
        proposed_layouts.emplace_back(spec.layout);
    }
    if(!CanFSwinChainWindowLayouts(
        std::span<const FSwinWindowLayout>{
            proposed_layouts.data(),proposed_layouts.size()},windows))
        return false;

    // Prepare all independent weight buffers, tensors and FSwin passes.
    std::vector<std::unique_ptr<NrRdna2FSwinLayerRuntime>> prepared;
    prepared.reserve(specs.size());
    for(const auto& spec:specs){
        auto runtime=std::make_unique<NrRdna2FSwinLayerRuntime>(
            device,allocator,scheduler);
        if(!runtime->Initialize(model,spec.prefix,spec.channels,
                                spec.windows,spec.residual_last))
            return false;
        prepared.emplace_back(std::move(runtime));
    }

    std::vector<VkDescriptorSet> proposed_window_sets(
        specs.size(),VK_NULL_HANDLE);
    std::vector<FSwinWindowRepack> proposed_window_plans(specs.size());
    std::size_t shift_count=0u;
    for(std::size_t i=1u;i<specs.size();++i)
        if(FSwinNeedsWindowRepack(
            proposed_layouts[i-1u],proposed_layouts[i]))
            ++shift_count;

    vk::DescriptorPool new_pool;
    vk::DescriptorSets new_allocations;
    if(shift_count){
        new_pool=CreateWrappedDescriptorPool(
            device,static_cast<std::uint32_t>(2u*shift_count),
            static_cast<std::uint32_t>(shift_count),
            {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER});
        std::vector<VkDescriptorSetLayout> ds_layouts(
            shift_count,window_repack.DescriptorLayout());
        new_allocations=CreateWrappedDescriptorSets(
            new_pool,vk::Span<VkDescriptorSetLayout>{
                ds_layouts.data(),ds_layouts.size()});
        if(new_allocations.IsOutOfPoolMemory()||
           new_allocations.size()!=shift_count)return false;

        // Keep backing VkDescriptorBufferInfo storage stable until update.
        std::vector<VkDescriptorBufferInfo> infos(shift_count*2u);
        std::vector<VkWriteDescriptorSet> writes(shift_count*2u);
        std::size_t slot=0u;
        for(std::size_t i=1u;i<specs.size();++i){
            const auto& from=proposed_layouts[i-1u];
            const auto& to=proposed_layouts[i];
            if(!FSwinNeedsWindowRepack(from,to))continue;
            const FSwinWindowRepack plan{
                .windows_x=to.windows_x,
                .windows_y=to.windows_y,
                .channels=channels,
                .src_shift_x=from.shift_x,
                .src_shift_y=from.shift_y,
                .dst_shift_x=to.shift_x,
                .dst_shift_y=to.shift_y,
            };
            const auto source=prepared[i-1u]->Output();
            const auto destination=prepared[i]->Input();
            if(!source.buffer||!destination.buffer||
               source.buffer==destination.buffer||
               source.offset||destination.offset||
               !CanFSwinWindowRepack(
                   plan,{source.range,destination.range}))
                return false;
            proposed_window_plans[i]=plan;
            proposed_window_sets[i]=new_allocations[slot];
            infos[2u*slot]=source;
            infos[2u*slot+1u]=destination;
            for(std::uint32_t binding=0u;binding<2u;++binding){
                const auto index=2u*slot+binding;
                writes[index]=VkWriteDescriptorSet{
                    .sType=VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
                    .pNext=nullptr,
                    .dstSet=new_allocations[slot],
                    .dstBinding=binding,.dstArrayElement=0,
                    .descriptorCount=1,
                    .descriptorType=VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
                    .pImageInfo=nullptr,
                    .pBufferInfo=&infos[index],
                    .pTexelBufferView=nullptr};
            }
            ++slot;
        }
        if(slot!=shift_count)return false;
        device.GetLogical().UpdateDescriptorSets(
            vk::Span<VkWriteDescriptorSet>{writes.data(),writes.size()},{});
    }
    window_pool=std::move(new_pool);
    window_allocations=std::move(new_allocations);
    window_sets=std::move(proposed_window_sets);
    window_plans=std::move(proposed_window_plans);
    layouts=std::move(proposed_layouts);
    layers=std::move(prepared);
    input_view=specs.front().input_view;
    output_view=specs.back().output_view;
    downsample=specs.back().downsample;
    if(downsample)downsample_prefix=specs.back().prefix;
    ready=true;
    return true;
}

bool NrRdna2FSwinChain::InitializeNativeSegment(
    const NeuralModelFile& model,std::string_view native_plan,
    std::size_t segment_index){
    if(ready || !model.IsOpen())return false;
    using namespace Eden::Graphics::NeuralRDNA2;
    std::vector<NativePlanStep> rows;
    if(ParseNativePlan(native_plan,rows)!=NativePlanError::Ok)return false;
    auto segments=ExtractNativeFSwinSegments(rows);
    if(segment_index>=segments.size())return false;
    const auto& source=segments[segment_index].layers;
    if(source.empty())return false;
    std::vector<FSwinLayerSpec> specs;
    specs.reserve(source.size());
    for(const auto& layer:source){
        specs.push_back(FSwinLayerSpec{
            layer.weight_prefix,layer.channels,layer.windows,
            false,layer.layout,
            layer.view==NativeFSwinView::InputView||
                layer.view==NativeFSwinView::Upsample,
            layer.view==NativeFSwinView::OutputView,
            layer.view==NativeFSwinView::Downsample});
    }
    return Initialize(model,std::span<const FSwinLayerSpec>{
        specs.data(),specs.size()});
}

bool NrRdna2FSwinChain::UploadInput(
    std::span<const std::uint8_t> tokens){
    // View entry must be packed from an explicitly sized raster.
    return ready&&!input_view&&!input_set&&
           layers.front()->UploadInput(tokens);
}
bool NrRdna2FSwinChain::UploadRasterFeatures(
    std::span<const std::uint8_t> features,
    std::uint32_t width,std::uint32_t height){
    if(!ready||layers.empty()||layouts.empty()||input_set)return false;
    const auto& layout=layouts.front();
    if(!layout.windows_x||!layout.windows_y||
       layout.shift_x||layout.shift_y)return false;
    const Eden::Graphics::NeuralRDNA2::FSwinRasterLayout plan{
        width,height,layout.windows_x,layout.windows_y,
        layers.front()->Plan().dense[0].channels,0u};
    std::vector<std::uint8_t> tokens;
    if(!Eden::Graphics::NeuralRDNA2::PackFSwinFeatureRaster(
           features,plan,tokens))return false;
    return layers.front()->UploadInput(tokens);
}
bool NrRdna2FSwinChain::BindRasterInput(
    VkDescriptorBufferInfo source,std::uint32_t width,
    std::uint32_t height){
    if(!AllowsGpuRasterInput()||layouts.empty())return false;
    const auto target=layers.front()->Input();
    const auto& layout=layouts.front();
    if(!source.buffer||!target.buffer||
       source.buffer==target.buffer||source.offset||target.offset||
       !source.range||!target.range||
       source.range==VK_WHOLE_SIZE||target.range==VK_WHOLE_SIZE||
       !layout.windows_x||!layout.windows_y||
       source.range>device.GetMaxStorageBufferRange()||
       target.range>device.GetMaxStorageBufferRange())return false;
    const FSwinWindowLayout canonical{
        layout.windows_x,layout.windows_y,0,0};
    const bool shifted=FSwinNeedsWindowRepack(canonical,layout);
    vk::Buffer scratch;
    VkDescriptorBufferInfo packing_target=target;
    if(shifted){
        const VkBufferCreateInfo ci{
            .sType=VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
            .pNext=nullptr,.flags=0,.size=target.range,
            .usage=VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
            .sharingMode=VK_SHARING_MODE_EXCLUSIVE,
            .queueFamilyIndexCount=0,.pQueueFamilyIndices=nullptr,
        };
        scratch=allocator.CreateBuffer(ci,MemoryUsage::DeviceLocal);
        if(!scratch)return false;
        packing_target={
            .buffer=*scratch,.offset=0,.range=target.range};
    }
    const Eden::Graphics::NeuralRDNA2::FSwinRasterLayout plan{
        width,height,layout.windows_x,layout.windows_y,
        layers.front()->Plan().dense[0].channels,0u};
    const Eden::Graphics::NeuralRDNA2::FSwinRasterLayoutStorage storage{
        source.range,packing_target.range};
    if(!Eden::Graphics::NeuralRDNA2::CanFSwinRasterLayout(
           plan,storage))return false;
    auto new_pool=CreateWrappedDescriptorPool(
        device,2u,1u,{VK_DESCRIPTOR_TYPE_STORAGE_BUFFER});
    VkDescriptorSetLayout descriptor_layout=raster_input.DescriptorLayout();
    auto new_sets=CreateWrappedDescriptorSets(
        new_pool,vk::Span<VkDescriptorSetLayout>{&descriptor_layout,1u});
    if(new_sets.IsOutOfPoolMemory()||new_sets.size()!=1u)return false;
    const std::array<VkDescriptorBufferInfo,2> infos{
        source,packing_target};
    std::array<VkWriteDescriptorSet,2> writes{};
    for(std::uint32_t i=0u;i<2u;++i){
        writes[i]=VkWriteDescriptorSet{
            .sType=VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
            .pNext=nullptr,.dstSet=new_sets[0],
            .dstBinding=i,.dstArrayElement=0,
            .descriptorCount=1,
            .descriptorType=VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
            .pImageInfo=nullptr,.pBufferInfo=&infos[i],
            .pTexelBufferView=nullptr,
        };
    }
    device.GetLogical().UpdateDescriptorSets(
        vk::Span<VkWriteDescriptorSet>{writes.data(),writes.size()},{});
    vk::DescriptorPool shift_pool;
    vk::DescriptorSets shift_sets;
    FSwinWindowRepack shifted_plan{};
    if(shifted){
        shifted_plan={
            .windows_x=layout.windows_x,.windows_y=layout.windows_y,
            .channels=layers.front()->Plan().dense[0].channels,
            .src_shift_x=0,.src_shift_y=0,
            .dst_shift_x=layout.shift_x,.dst_shift_y=layout.shift_y,
        };
        if(!CanFSwinWindowRepack(
           shifted_plan,{packing_target.range,target.range}))return false;
        shift_pool=CreateWrappedDescriptorPool(
            device,2u,1u,{VK_DESCRIPTOR_TYPE_STORAGE_BUFFER});
        VkDescriptorSetLayout shifted_layout=window_repack.DescriptorLayout();
        shift_sets=CreateWrappedDescriptorSets(
            shift_pool,vk::Span<VkDescriptorSetLayout>{&shifted_layout,1u});
        if(shift_sets.IsOutOfPoolMemory()||shift_sets.size()!=1u)
            return false;
        const std::array<VkDescriptorBufferInfo,2> shifted_infos{
            packing_target,target};
        std::array<VkWriteDescriptorSet,2> shift_writes{};
        for(std::uint32_t i=0u;i<2u;++i){
            shift_writes[i]=VkWriteDescriptorSet{
                .sType=VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
                .pNext=nullptr,.dstSet=shift_sets[0],
                .dstBinding=i,.dstArrayElement=0,
                .descriptorCount=1,
                .descriptorType=VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
                .pImageInfo=nullptr,.pBufferInfo=&shifted_infos[i],
                .pTexelBufferView=nullptr,
            };
        }
        device.GetLogical().UpdateDescriptorSets(
            vk::Span<VkWriteDescriptorSet>{
                shift_writes.data(),shift_writes.size()},{});
    }
    input_window_staging=std::move(scratch);
    input_shift_pool=std::move(shift_pool);
    input_shift_allocations=std::move(shift_sets);
    input_shift_set=shifted?input_shift_allocations[0]:VK_NULL_HANDLE;
    input_shift_plan=shifted_plan;
    input_pool=std::move(new_pool);
    input_allocations=std::move(new_sets);
    input_set=input_allocations[0];
    input_plan=plan;
    input_storage=storage;
    return true;
}

bool NrRdna2FSwinChain::BindRasterOutput(
    VkDescriptorBufferInfo destination,std::uint32_t width,
    std::uint32_t height){
    if(!ready||!output_view||raster_output||
       layers.empty()||layouts.empty())return false;
    auto exporter=std::make_unique<NrRdna2FSwinOutputView>(
        device,allocator,scheduler);
    if(!exporter->Initialize(
            layers.back()->Output(),destination,layouts.back(),
            layers.back()->Plan().dense[0].channels,width,height))
        return false;
    raster_output=std::move(exporter);
    raster_destination=destination;
    return true;
}

bool NrRdna2FSwinChain::BindRawRasterOutput(
    VkDescriptorBufferInfo destination,std::uint32_t width,
    std::uint32_t height){
    if(!ready||!output_view||raw_raster_output||
       layers.empty()||layouts.empty()||
       layers.back()->Plan().dense[0].channels!=32u||
       !destination.buffer||destination.offset||
       !destination.range||destination.range==VK_WHOLE_SIZE||
       (raster_destination.buffer&&
        destination.buffer==raster_destination.buffer))
        return false;
    const auto raw_bytes=
        layers.back()->Plan().dense_storage[3].output_bytes*2u;
    if(!raw_bytes||raw_bytes>(256ull<<20)||
       raw_bytes>device.GetMaxStorageBufferRange()||
       destination.range>device.GetMaxStorageBufferRange())
        return false;
    const VkBufferCreateInfo ci{
        .sType=VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        .pNext=nullptr,.flags=0,.size=raw_bytes,
        .usage=VK_BUFFER_USAGE_STORAGE_BUFFER_BIT|
               VK_BUFFER_USAGE_TRANSFER_SRC_BIT|
               VK_BUFFER_USAGE_TRANSFER_DST_BIT,
        .sharingMode=VK_SHARING_MODE_EXCLUSIVE,
        .queueFamilyIndexCount=0,.pQueueFamilyIndices=nullptr,
    };
    auto scratch=allocator.CreateBuffer(ci,MemoryUsage::DeviceLocal);
    if(!scratch)return false;
    const VkDescriptorBufferInfo raw_windows{
        .buffer=*scratch,.offset=0,.range=raw_bytes};
    auto exporter=std::make_unique<NrRdna2FSwinOutputView>(
        device,allocator,scheduler);
    // Treat each FP16 byte as a raster component: exactly 64 bytes
    // per pixel for 32 FP16 channels. Repacking is byte-preserving.
    if(!exporter->Initialize(raw_windows,destination,layouts.back(),
                             64u,width,height)||
       !layers.back()->BindRawC32Output(raw_windows))
        return false;
    raw_window_staging=std::move(scratch);
    raw_raster_output=std::move(exporter);
    raw_raster_destination=destination;
    return true;
}

bool NrRdna2FSwinChain::BindDownsampleOutput(
    const NeuralModelFile& model,VkDescriptorBufferInfo destination,
    std::uint32_t input_width,std::uint32_t input_height){
    if(!ready||!downsample||downsample_output||!model.IsOpen()||
       layers.empty()||layouts.empty()||downsample_prefix.empty()||
       !destination.buffer||
       (skip_destination.buffer&&
        destination.buffer==skip_destination.buffer))
        return false;
    const auto& layout=layouts.back();
    auto endpoint=std::make_unique<NrRdna2FSwinDownsampleOutput>(
        device,allocator,scheduler);
    if(!endpoint->Initialize(
           model,downsample_prefix,layers.back()->Output(),destination,
           input_width,input_height,
           layers.back()->Plan().dense[0].channels,
           layout.shift_x,layout.shift_y))
        return false;
    downsample_output=std::move(endpoint);
    raster_destination=destination;
    return true;
}

bool NrRdna2FSwinChain::BindSkipRasterOutput(
    VkDescriptorBufferInfo destination,
    std::uint32_t input_width,std::uint32_t input_height){
    if(!ready||!downsample||raster_output||
       layers.empty()||layouts.empty()||!destination.buffer||
       (raster_destination.buffer&&
        destination.buffer==raster_destination.buffer))
        return false;
    auto exporter=std::make_unique<NrRdna2FSwinOutputView>(
        device,allocator,scheduler);
    if(!exporter->Initialize(
           layers.back()->Output(),destination,layouts.back(),
           layers.back()->Plan().dense[0].channels,
           input_width,input_height))
        return false;
    raster_output=std::move(exporter);
    skip_destination=destination;
    return true;
}

bool NrRdna2FSwinChain::Record(){
    // Validate terminal adapters before submitting ANY chain commands.
    if(!ready||(output_view&&!raster_output)||
       (downsample&&!downsample_output))return false;
    if(input_set){
        if(!raster_input.Record(input_set,input_plan,input_storage))
            return false;
        if(input_shift_set&&!window_repack.Record(
            input_shift_set,input_shift_plan,
            {input_storage.output_bytes,layers.front()->Input().range}))
            return false;
        if(!layers.front()->MarkGpuInputReady())return false;
    }
    for(std::size_t i=0u;i<layers.size();++i){
        if(i>0u){
            if(FSwinNeedsWindowRepack(layouts[i-1u],layouts[i])){
                if(!layers[i]->LinkInputFromShifted(
                       *layers[i-1u],window_repack,
                       window_sets[i],window_plans[i]))
                    return false;
            }else if(!layers[i]->LinkInputFrom(*layers[i-1u])){
                return false;
            }
        }
        if(!layers[i]->Record())return false;
    }
    if(raster_output&&!raster_output->Record())return false;
    if(raw_raster_output&&!raw_raster_output->Record())return false;
    if(downsample_output&&!downsample_output->Record())return false;
    return true;
}
} // namespace Vulkan
