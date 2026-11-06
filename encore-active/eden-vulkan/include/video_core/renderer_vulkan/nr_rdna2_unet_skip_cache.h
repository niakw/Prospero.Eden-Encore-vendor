// SPDX-License-Identifier: MIT
// Four GPU-owned U-Net skip rasters. Their identities are driven by the
// recovered native skip map, never title-specific assumptions.
#pragma once
#include "video_core/renderer_vulkan/nr_rdna2_fswin_chain.h"
#include "video_core/renderer_vulkan/nr_rdna2_unet_skip_map.h"
#include "video_core/renderer_vulkan/nr_rdna2_unet_skip_cache_policy.h"
#include "video_core/vulkan_common/vulkan_wrapper.h"
#include <array>
#include <cstdint>
namespace Vulkan {
class Device;
class MemoryAllocator;
class Scheduler;
using NrRdna2SkipRasterDimensions =
    Eden::Graphics::NeuralRDNA2::NativeUNetSkipSurface;
class NrRdna2UNetSkipCache final {
public:
    NrRdna2UNetSkipCache(const Device&,MemoryAllocator&);
    ~NrRdna2UNetSkipCache();
    NrRdna2UNetSkipCache(const NrRdna2UNetSkipCache&)=delete;
    NrRdna2UNetSkipCache& operator=(const NrRdna2UNetSkipCache&)=delete;
    // Dimensions are canonical Vulkan raster axes, NOT the native
    // plan's W/H. Both geometry and total pixel count are validated.
    [[nodiscard]] bool Initialize(
        const Eden::Graphics::NeuralRDNA2::NativeUNetSkipMap& map,
        std::array<NrRdna2SkipRasterDimensions,4> geometry);
    // Call BEFORE the producer's Record; the producer writes this buffer
    // as its additional pre-downsample skip output.
    [[nodiscard]] bool BindProducer(
        std::uint32_t down_block,NrRdna2FSwinChain& producer);
    // Call only after the corresponding producer's GPU work has been
    // enqueued on the SAME scheduler, before consumer _upsample work.
    // Buffer is retained by this cache until in-flight work finishes.
    // Enqueue producer (FSwin + skip export + learned DS) exactly once.
    // Returned upsample inputs are made available ONLY after successful
    // recording, and still require in-order submission on this scheduler.
    [[nodiscard]] bool RecordProducer(
        std::uint32_t down_block,NrRdna2FSwinChain& producer);
    [[nodiscard]] VkDescriptorBufferInfo ForUpsample(
        std::uint32_t up_block) const noexcept;
    [[nodiscard]] Scheduler* SchedulerIdentity() const noexcept {
        return scheduler;
    }
    [[nodiscard]] bool Ready() const noexcept{return ready;}
private:
    [[nodiscard]] VkDescriptorBufferInfo Describe(std::size_t i) const noexcept;
    const Device& device;
    MemoryAllocator& allocator;
    Eden::Graphics::NeuralRDNA2::NativeUNetSkipMap map{};
    std::array<NrRdna2SkipRasterDimensions,4> dimensions{};
    std::array<vk::Buffer,4> buffers{};
    std::array<VkDeviceSize,4> byte_sizes{};
    std::array<bool,4> bound{};
    std::array<bool,4> recorded{};
    Scheduler* scheduler{};
    bool ready{};
};
} // namespace Vulkan
