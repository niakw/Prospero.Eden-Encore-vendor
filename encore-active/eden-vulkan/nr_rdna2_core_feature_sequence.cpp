// SPDX-License-Identifier: MIT
#include "video_core/renderer_vulkan/nr_rdna2_core_feature_sequence.h"
#include "video_core/renderer_vulkan/nr_rdna2_fswin_upsample_weight_loader.h"
#include <array>
#include <cstdint>
#include <string>
namespace Vulkan {
namespace NR=Eden::Graphics::NeuralRDNA2;
namespace {
[[nodiscard]] bool Good(VkDescriptorBufferInfo d) noexcept {
    return d.buffer!=VK_NULL_HANDLE && d.offset==0u &&
           d.range!=0u && d.range!=VK_WHOLE_SIZE;
}
[[nodiscard]] bool ValidTargets(
    std::array<VkDescriptorBufferInfo,4> up,
    std::array<VkDescriptorBufferInfo,4> post) noexcept {
    std::array<VkDescriptorBufferInfo,8> all{};
    for(std::size_t i=0u;i<4u;++i) {
        all[i]=up[i];
        all[i+4u]=post[i];
    }
    for(std::size_t i=0u;i<all.size();++i) {
        if(!Good(all[i]))return false;
        for(std::size_t j=0u;j<i;++j)
            if(all[i].buffer==all[j].buffer)return false;
    }
    return true;
}
}
NrRdna2CoreFeatureSequence::NrRdna2CoreFeatureSequence(Scheduler& s)
    :scheduler{s},low_link{s}{}
bool NrRdna2CoreFeatureSequence::Record(
    const NR::NeuralModelFile& model,std::string_view plan,
    const NR::NativeUNetSkipMap& graph,
    const NR::NativeSplitRoutes& routes,
    const NR::NativeVitRoute& vit,
    NrRdna2PreblockRun& pre,NrRdna2UNetDownSequence& down,
    NrRdna2SplitSwinRun& first,NrRdna2NeuralBottleneck& core,
    NrRdna2UNetSkipCache& cache,NrRdna2UNetUpSequence& up,
    std::uint32_t split_result_block,
    std::array<NrRdna2FSwinChain*,4> up_chains,
    std::array<VkDescriptorBufferInfo,4> up_outputs,
    NrRdna2PostblockRun& post,
    VkDescriptorBufferInfo post_mixed,
    VkDescriptorBufferInfo post_swin,
    VkDescriptorBufferInfo post_raw,
    VkDescriptorBufferInfo post_features,
    NrRdna2ProxyFeatureStage* proxy,
    NrRdna2NoHistoryCompose* compose,
    NrRdna2HDRProxy* hdr) {
    if(attempted||!model.IsOpen()||plan.empty()||
       !pre.Ready()||!down.Ready()||!first.Ready()||
       !core.Ready()||!cache.Ready()||
       pre.SchedulerIdentity()!=&scheduler||
       down.SchedulerIdentity()!=&scheduler||
       first.SchedulerIdentity()!=&scheduler||
       core.SchedulerIdentity()!=&scheduler||
       !ValidTargets(up_outputs,{post_mixed,post_swin,
                                  post_raw,post_features}))
        return false;
    for(const auto* chain:up_chains)
        if(!chain||!chain->Ready()||
           chain->SchedulerIdentity()!=&scheduler)return false;
    // Optional first-frame input: normalized proxy -> FP16 16 lanes.
    // Refuse equal-size but transposed geometry, or an unrelated buffer.
    if(proxy) {
        const auto source=pre.PreparedInputFeatures();
        const auto shape=pre.FullRasterGeometry();
        const auto produced=proxy->Result();
        const auto raster=proxy->Geometry();
        if(!proxy->Ready()||proxy->SchedulerIdentity()!=&scheduler||
           !Good(source)||!Good(produced)||
           source.buffer!=produced.buffer||
           source.range!=produced.range||
           shape[0]!=raster.full_width||
           shape[1]!=raster.full_height)return false;
    }
    if(hdr) {
        // The HDR proxy must publish exactly the RGBA32F buffer consumed
        // by the no-history feature builder, on the same ordered scheduler.
        if(!proxy||!hdr->Ready()||
           hdr->SchedulerIdentity()!=&scheduler)return false;
        const auto published=hdr->ProxyOutput();
        const auto consumed=proxy->Input();
        const auto shape=hdr->Geometry();
        const auto native=pre.FullRasterGeometry();
        if(!Good(published)||!Good(consumed)||
           published.buffer!=consumed.buffer||
           published.range!=consumed.range||
           shape.width!=native[0]||shape.height!=native[1])
            return false;
    }
    if(compose) {
        // First image always shares the normalized no-history proxy.
        // Never pass an arbitrary image to the RGB residual compositor.
        if(!proxy||!compose->Ready()||
           compose->SchedulerIdentity()!=&scheduler)return false;
        const auto head=compose->FeaturesInput();
        const auto base=compose->ProxyInput();
        const auto target=compose->PreparedOutput();
        const auto ext=compose->Geometry();
        const auto source=proxy->Input();
        const auto pre_shape=pre.FullRasterGeometry();
        if(!Good(head)||!Good(base)||!Good(target)||
           head.buffer!=post_features.buffer||
           head.range!=post_features.range||
           base.buffer!=source.buffer||base.range!=source.range||
           ext.width!=pre_shape[0]||ext.height!=pre_shape[1])
            return false;
    }
    // Validate the genuine learned gains BEFORE any Vulkan command is
    // enqueued. The upstream wide-C extractor produces C-16 gains; without
    // proven reconstruction, reject all incomplete model regions outright.
    // This preflight is intentionally model-dependent, not an optional
    // warning or an invented fallback for the three wide U-Net levels.
    if(graph.count!=4u)return false;
    for(const auto& edge:graph.edges) {
        if(edge.channels!=32u&&edge.channels!=64u&&
           edge.channels!=128u&&edge.channels!=256u)return false;
        const auto prefix=std::string("unpacked/block")+
            std::to_string(edge.up_block)+".layer0.layer";
        NR::NeuralWeightArena validated;
        NR::FSwinUpsampleWeightRanges ranges{};
        if(!NR::PrepareFSwinUpsampleWeights(
              model,prefix,edge.channels,validated,ranges)||
           ranges.channels!=edge.channels)return false;
    }
    // No partially submitted sequence may be replayed after failure.
    attempted=true;
    if((hdr&&!hdr->Record())||
       (proxy&&!proxy->Record())||
       !pre.RecordWithDownSequence(down)||
       !low_link.Bind(graph,routes,vit,down,first,core)||
       !low_link.Record())return false;
    const auto low=low_link.Result();
    if(!Good(low)||
       !up.Prepare(model,plan,graph,cache,split_result_block,
                   low,up_chains,up_outputs)||
       !up.Record())return false;
    const auto final_c32=up.FinalC32Raster();
    const auto full_skip=pre.ResultFullSkip();
    if(!Good(final_c32)||!Good(full_skip)||
       final_c32.buffer!=up_outputs[3].buffer||
       !post.PrepareWithRawProjection(
           model,plan,final_c32,full_skip,post_mixed,
           post_swin,post_raw,post_features)||
       !post.Record())return false;
    features=post.ResultFeatures();
    if(!Good(features)||features.buffer!=post_features.buffer||
       features.range!=post_features.range)return false;
    if(compose) {
        if(!compose->Record())return false;
        first_image=compose->Result();
        if(!Good(first_image))return false;
    }
    recorded=true;
    return true;
}
} // namespace Vulkan
