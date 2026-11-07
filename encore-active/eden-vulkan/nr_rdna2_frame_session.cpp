// SPDX-License-Identifier: MIT
// One-shot model-driven assembly of every known R09 first-frame stage.
// This is deliberately NOT an on/off UI or a PS5 swapchain hook.
#include "video_core/renderer_vulkan/nr_rdna2_frame_session.h"
#include "video_core/renderer_vulkan/nr_rdna2_core_feature_sequence.h"
#include "video_core/renderer_vulkan/nr_rdna2_neural_bottleneck.h"
#include "video_core/renderer_vulkan/nr_rdna2_native_decoder_link.h"
#include "video_core/renderer_vulkan/nr_rdna2_model_file.h"
#include "video_core/vulkan_common/vulkan_device.h"
#include "video_core/vulkan_common/vulkan_memory_allocator.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <utility>
#include <vector>
namespace Vulkan {
namespace NR=Eden::Graphics::NeuralRDNA2;
struct NrRdna2FrameSession::State {
    const Device& dev;
    MemoryAllocator& alloc;
    Scheduler& scheduler;
    // This declaration comes before every GPU stage owner, so external
    // storage is destroyed LAST. Caller still must retire the GPU fence.
    std::vector<vk::Buffer> tensors{};
    std::uint64_t owned_bytes{};
    NR::NeuralModelFile model{};
    std::string plan{};
    NR::NativeUNetSkipMap graph{};
    NR::NativeSplitRoutes routes{};
    NR::NativeVitRoute vit_route{};
    std::array<std::unique_ptr<NrRdna2FSwinChain>,4> down_chains{};
    std::array<std::unique_ptr<NrRdna2FSwinChain>,4> up_chains{};
    std::array<VkDescriptorBufferInfo,4> up_outputs{};
    VkDescriptorBufferInfo post_mixed{},post_swin{},post_raw{},post_features{};
    NrRdna2TextureProxy source;
    NrRdna2ProxyFeatureStage features;
    NrRdna2PreblockRun pre;
    NrRdna2UNetSkipCache skips;
    NrRdna2UNetDownSequence down;
    NrRdna2SplitSwinRun first;
    NrRdna2VitRun vit;
    NrRdna2NativeDecoderLink decoder;
    NrRdna2SplitSwinRun second;
    NrRdna2NeuralBottleneck bottleneck;
    NrRdna2UNetUpSequence up;
    NrRdna2PostblockRun post;
    NrRdna2NoHistoryCompose compose;
    NrRdna2NoHistoryPresent present;
    NrRdna2CoreFeatureSequence sequence;
    State(const Device& d,MemoryAllocator& a,Scheduler& s)
       :dev{d},alloc{a},scheduler{s},
        source{d,s},features{d,s},pre{d,a,s},skips{d,a},
        down{s},first{d,a,s},vit{d,a,s},decoder{d,a,s},
        second{d,a,s},bottleneck{s},up{d,a,s},post{d,a,s},
        compose{d,s},present{d,s},sequence{s}{
        tensors.reserve(256u);
    }
    [[nodiscard]] VkDescriptorBufferInfo Make(std::uint64_t size){
        constexpr std::uint64_t per=256ull<<20;
        constexpr std::uint64_t total=3ull<<30;
        if(!size||size>per||size>dev.GetMaxStorageBufferRange()||
           size>total||owned_bytes>total-size||(size&3u))return {};
        const VkBufferCreateInfo ci{
            .sType=VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
            .pNext=nullptr,.flags=0u,.size=size,
            .usage=VK_BUFFER_USAGE_STORAGE_BUFFER_BIT|
                   VK_BUFFER_USAGE_TRANSFER_SRC_BIT|
                   VK_BUFFER_USAGE_TRANSFER_DST_BIT,
            .sharingMode=VK_SHARING_MODE_EXCLUSIVE,
            .queueFamilyIndexCount=0u,.pQueueFamilyIndices=nullptr,
        };
        auto buffer=alloc.CreateBuffer(ci,MemoryUsage::DeviceLocal);
        if(!buffer)return {};
        const auto native=*buffer;
        tensors.emplace_back(std::move(buffer));
        owned_bytes+=size;
        return VkDescriptorBufferInfo{
            .buffer=native,.offset=0u,.range=size};
    }
    [[nodiscard]] bool MakeFirstSplitBuffers(
        std::uint32_t run_index,
        VkDescriptorBufferInfo decoder_input,
        std::vector<NrRdna2SplitSwinBlockBuffers>& result) {
        const auto& run=routes.runs[run_index];
        result.clear();
        result.reserve(run.count);
        for(std::uint32_t i=0u;i<run.count;++i){
            const auto& p=routes.blocks[run.start+i].plan;
            const std::uint64_t n=std::uint64_t(p.windows)*64u;
            const auto linear=n*512u;
            NrRdna2SplitSwinBlockBuffers b{};
            b.input=i?result.back().projected:
                     decoder_input.buffer?decoder_input:Make(linear);
            b.ffwd=Make(linear);
            b.ffwd_projected=Make(linear);
            b.context_heads=Make(linear);
            b.context_tokens=Make(linear);
            b.projected=Make(linear);
            b.query=Make(linear);
            b.key=Make(linear);
            b.value=Make(linear);
            b.fp32_scratch=Make(linear*4u);
            if(p.attention_shift_x||p.attention_shift_y){
                b.shifted_input=Make(linear);
                b.unshifted_context=Make(linear);
                if(!b.shifted_input.buffer||!b.unshifted_context.buffer)
                    return false;
            }
            if(p.has_pooled_projection){
                b.pooled=Make(std::uint64_t(p.pool_output_tokens)*512u);
                b.final_fp32=Make(
                    std::uint64_t(p.final_head_tokens)*1024u*4u);
                b.final_output=Make(
                    std::uint64_t(p.final_head_tokens)*1024u);
                if(!b.pooled.buffer||!b.final_fp32.buffer||
                   !b.final_output.buffer)return false;
            }
            if(!b.input.buffer||!b.ffwd.buffer||
               !b.ffwd_projected.buffer||!b.context_heads.buffer||
               !b.context_tokens.buffer||!b.projected.buffer||
               !b.query.buffer||!b.key.buffer||!b.value.buffer||
               !b.fp32_scratch.buffer)return false;
            result.emplace_back(b);
        }
        return true;
    }
    [[nodiscard]] bool MakeVitBuffers(
        std::vector<NrRdna2VitBlockBuffers>& result){
        result.clear();
        result.reserve(vit_route.count);
        const auto tokens=std::uint64_t(vit_route.tokens);
        const auto plane=tokens*1024u;
        for(std::uint32_t i=0u;i<vit_route.count;++i){
            NrRdna2VitBlockBuffers b{};
            b.input=i?result.back().output:Make(plane);
            b.hidden=Make(plane*4u);
            b.ffwd=Make(plane);
            b.attention_context=Make(plane);
            b.output=Make(plane);
            b.scratch=Make(plane*4u);
            b.query=Make(plane*4u);
            b.key=Make(plane*4u);
            b.value=Make(plane*4u);
            if(!b.input.buffer||!b.hidden.buffer||!b.ffwd.buffer||
               !b.attention_context.buffer||!b.output.buffer||
               !b.scratch.buffer||!b.query.buffer||!b.key.buffer||
               !b.value.buffer)return false;
            result.emplace_back(b);
        }
        return true;
    }
};
NrRdna2FrameSession::NrRdna2FrameSession(
    const Device& d,MemoryAllocator& a,Scheduler& s)
    :device{d},allocator{a},scheduler{s}{}
NrRdna2FrameSession::~NrRdna2FrameSession()=default;
bool NrRdna2FrameSession::Initialize(
    std::string_view model_path,std::string_view native_plan,
    VkImageView guest_view,VkSampler guest_sampler,
    VkFormat guest_format,VkExtent2D guest_extent,
    VkImageView target_view,VkFormat target_format,
    VkExtent2D target_extent,float paper_white,float intensity){
    if(attempted)return false;
    attempted=true;
    if(model_path.empty()||native_plan.empty()||
       guest_view==VK_NULL_HANDLE||guest_sampler==VK_NULL_HANDLE||
       target_view==VK_NULL_HANDLE||
       !std::isfinite(paper_white)||paper_white<0.05f||
       !std::isfinite(intensity)||intensity<0.0f||intensity>1.0f)
        return false;
    auto candidate=std::make_unique<State>(
        device,allocator,scheduler);
    auto& q=*candidate;
    if(!q.model.Open(std::string(model_path)))return false;
    q.plan=std::string(native_plan);
    std::vector<NR::NativePlanStep> rows;
    if(NR::ParseNativePlan(q.plan,rows)!=NR::NativePlanError::Ok||
       rows.size()<4u||(rows.size()!=152u&&rows.size()!=76u)||
       NR::BuildNativeUNetSkipMap(rows,q.graph)!=
           NR::NativeUNetSkipError::Ok||
       NR::BuildNativeSplitRoutes(rows,q.routes)!=
           NR::SplitNativeRoutesError::Ok||
       NR::BuildNativeVitRoute(rows,q.vit_route)!=
           NR::NativeVitRouteError::Ok)
        return false;
    const auto width=rows.front().height;
    const auto height=rows.front().width;
    if(!width||!height||width!=guest_extent.height||
       height!=guest_extent.width||
       width!=target_extent.height||
       height!=target_extent.width||
       rows.back().block!=(rows.size()==152u?70u:30u)||
       !q.vit_route.tokens)
        return false;
    const auto n=std::uint64_t(width)*height;
    if(n>16384ull*16384u||n*64u>(256ull<<20))return false;
    const bool ldr=guest_format==VK_FORMAT_R8G8B8A8_UNORM||
                   guest_format==VK_FORMAT_B8G8R8A8_UNORM;
    const bool hdr=guest_format==VK_FORMAT_R16G16B16A16_SFLOAT||
                   guest_format==VK_FORMAT_R32G32B32A32_SFLOAT;
    if(!ldr&&!hdr)return false;
    // Full plan has 8 native FSwin segments: four descending
    // (C32->C256), four ascending (C256->C32).
    const auto segments=NR::ExtractNativeFSwinSegments(rows);
    if(segments.size()!=8u)return false;
    std::array<std::size_t,4> down_idx{},up_idx{};
    std::array<bool,4> seen_down{},seen_up{};
    for(std::size_t i=0u;i<segments.size();++i){
        const auto& seg=segments[i];
        if(seg.layers.empty())return false;
        const auto first=seg.layers.front(),last=seg.layers.back();
        if(last.view==NR::NativeFSwinView::Downsample){
            if(first.channels<32u||first.channels>256u||
               (first.channels&(first.channels-1u)))return false;
            const std::size_t pos=
                first.channels==32u?0u:first.channels==64u?1u:
                first.channels==128u?2u:3u;
            if(seen_down[pos])return false;
            seen_down[pos]=true;down_idx[pos]=i;
        }else if(first.view==NR::NativeFSwinView::Upsample){
            const std::size_t pos=
                first.channels==256u?0u:first.channels==128u?1u:
                first.channels==64u?2u:3u;
            if(seen_up[pos]||first.channels!=(256u>>pos))return false;
            seen_up[pos]=true;up_idx[pos]=i;
        }else return false;
    }
    for(std::size_t i=0u;i<4u;++i){
        if(!seen_down[i]||!seen_up[i])return false;
        q.down_chains[i]=std::make_unique<NrRdna2FSwinChain>(
            device,allocator,scheduler);
        q.up_chains[i]=std::make_unique<NrRdna2FSwinChain>(
            device,allocator,scheduler);
        if(!q.down_chains[i]->InitializeNativeSegment(
               q.model,q.plan,down_idx[i])||
           !q.up_chains[i]->InitializeNativeSegment(
               q.model,q.plan,up_idx[i]))return false;
    }
    // Distinct GPU storage for all independent stage views. No aliases,
    // no readback, and no proprietary model data included in the binary.
    const auto proxy=q.Make(n*16u),feature16=q.Make(n*32u),
               lifted=q.Make(n*32u),full_skip=q.Make(n*32u),
               raw_pre=q.Make(n*64u),
               first_half=q.Make(n*32u/4u);
    if(!proxy.buffer||!feature16.buffer||!lifted.buffer||
       !full_skip.buffer||!raw_pre.buffer||!first_half.buffer)return false;
    const NR::NRTextureProxyShape source_shape{
        width,height,paper_white,hdr?1u:0u};
    if(!q.source.Initialize(source_shape,guest_view,guest_sampler,
            guest_format,guest_extent,proxy)||
       !q.features.Initialize({
           width,height,width,height,width,height,0u,
           0.0f,0.0f,0.0f,0.0f,0.0f},proxy,feature16)||
       !q.pre.PrepareWithPool(
           q.model,q.plan,feature16,lifted,full_skip,raw_pre,
           first_half))return false;
    std::array<NrRdna2SkipRasterDimensions,4> skip_dims{};
    std::array<const NR::NativeUNetSkipEdge*,4> down_edges{},up_edges{};
    for(std::size_t i=0u;i<4u;++i){
        const auto& edge=q.graph.edges[i];
        skip_dims[i]={edge.skip_height,edge.skip_width};
        const auto down_pos=edge.channels==32u?0u:
            edge.channels==64u?1u:edge.channels==128u?2u:3u;
        const auto up_pos=edge.channels==256u?0u:
            edge.channels==128u?1u:edge.channels==64u?2u:3u;
        if(down_edges[down_pos]||up_edges[up_pos])return false;
        down_edges[down_pos]=&edge;
        up_edges[up_pos]=&edge;
    }
    if(!q.skips.Initialize(q.graph,skip_dims))return false;
    std::array<VkDescriptorBufferInfo,4> down_output{};
    for(std::size_t i=0u;i<4u;++i){
        if(!down_edges[i])return false;
        const auto& edge=*down_edges[i];
        const std::uint64_t low=
            std::uint64_t(edge.skip_height/2u)*
            (edge.skip_width/2u)*(2u*edge.channels);
        down_output[i]=q.Make(low);
        if(!down_output[i].buffer)return false;
    }
    std::array<NrRdna2FSwinChain*,4> down_levels{};
    for(std::size_t i=0u;i<4u;++i)
        down_levels[i]=q.down_chains[i].get();
    if(!q.down.Prepare(
           q.model,q.graph,q.skips,down_levels,first_half,down_output))
        return false;

    // Split-Swin run0 receives exactly C512 down output and publishes
    // a padded 1024-channel FinalHead; its other output is decoder skip.
    std::vector<NrRdna2SplitSwinBlockBuffers> split0_buffers;
    if(!q.MakeFirstSplitBuffers(0u,{},split0_buffers)||
       !q.first.InitializeWithModel(
         q.model,q.routes,0u,
         std::span<const NrRdna2SplitSwinBlockBuffers>{
             split0_buffers.data(),split0_buffers.size()},
         down_output[3],{}))return false;
    std::vector<NrRdna2VitBlockBuffers> vit_buffers;
    if(!q.MakeVitBuffers(vit_buffers))return false;
    const auto decoder_tiles=q.Make(
        std::uint64_t(q.vit_route.tokens)*1024u);
    if(!decoder_tiles.buffer||
       !q.vit.InitializeWithModel(
           q.model,q.vit_route,
           std::span<const NrRdna2VitBlockBuffers>{
               vit_buffers.data(),vit_buffers.size()},
           q.first.Result(),decoder_tiles))
        return false;
    // Decoder consumes padded ViT C1024, applies a learned
    // C1024->C512 projection and a native Split skip.
    const std::uint64_t valid=
        std::uint64_t(q.vit_route.raster_width)*
        q.vit_route.raster_height;
    const auto linear=q.Make(valid*1024u),
        fp32=q.Make(valid*512u*4u),
        half=q.Make(valid*512u*2u),
        skip_tiled=q.Make(
            std::uint64_t(q.vit_route.tokens)*512u),
        decoder_output=q.Make(
            std::uint64_t(q.vit_route.tokens)*512u);
    if(!linear.buffer||!fp32.buffer||!half.buffer||
       !skip_tiled.buffer||!decoder_output.buffer||
       !q.decoder.InitializeWithModel(
           q.model,q.vit_route.decoder_block,
           q.vit_route.raster_width,q.vit_route.raster_height,
           decoder_tiles,linear,fp32,half,
           q.first.PrimaryResult(),skip_tiled,decoder_output))
        return false;
    std::vector<NrRdna2SplitSwinBlockBuffers> split1_buffers;
    if(!q.MakeFirstSplitBuffers(1u,decoder_output,split1_buffers))
        return false;
    const auto second_low=q.Make(valid*512u);
    if(!second_low.buffer||!q.second.InitializeWithModel(
           q.model,q.routes,1u,
           std::span<const NrRdna2SplitSwinBlockBuffers>{
               split1_buffers.data(),split1_buffers.size()},
           {},second_low)||
       !q.bottleneck.Bind(
           q.routes,q.vit_route,q.first,q.vit,q.decoder,q.second))
        return false;
    // Four learned upsample edges, with every FSwin chain already
    // initialized before the global orchestrator's transactional preflight.
    for(std::size_t i=0u;i<4u;++i){
        if(!up_edges[i])return false;
        const auto& edge=*up_edges[i];
        q.up_outputs[i]=q.Make(
            std::uint64_t(edge.skip_height)*
            edge.skip_width*edge.channels);
        if(!q.up_outputs[i].buffer)return false;
    }
    q.post_mixed=q.Make(n*32u);
    q.post_swin=q.Make(n*32u);
    q.post_raw=q.Make(n*64u);
    q.post_features=q.Make(n*32u);
    const auto image=q.Make(n*16u);
    if(!q.post_mixed.buffer||!q.post_swin.buffer||
       !q.post_raw.buffer||!q.post_features.buffer||!image.buffer||
       !q.compose.Initialize(
           {width,height,intensity,0u},
           proxy,q.post_features,image)||
       !q.present.Initialize(
           {width,height,0u,0u},
           image,target_view,target_format,target_extent))
        return false;
    state=std::move(candidate);
    output_view=target_view;
    ready=true;
    return true;
}
bool NrRdna2FrameSession::Record(){
    if(!ready||record_attempted||!state)return false;
    record_attempted=true;
    auto& q=*state;
    std::array<NrRdna2FSwinChain*,4> up_levels{};
    for(std::size_t i=0u;i<4u;++i)
        up_levels[i]=q.up_chains[i].get();
    if(!q.sequence.Record(
         q.model,q.plan,q.graph,q.routes,q.vit_route,
         q.pre,q.down,q.first,q.bottleneck,q.skips,q.up,
         q.routes.runs[1u].terminal_block,up_levels,q.up_outputs,
         q.post,q.post_mixed,q.post_swin,q.post_raw,q.post_features,
         &q.features,&q.compose,nullptr,&q.present,&q.source,nullptr))
        return false;
    recorded=q.sequence.ResultPresentView()==output_view;
    return recorded;
}
} // namespace Vulkan
