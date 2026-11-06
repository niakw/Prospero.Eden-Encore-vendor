// SPDX-License-Identifier: MIT
// Explicit Vulkan output boundary for FSwin _outview: shifted window
// tokens -> canonical unshifted windows -> packed 2D FP8 feature raster.
// Does NOT convert these features into RGB.
#pragma once
#include "rdna2_nr_fswin_output_view_policy.h"
#include "video_core/renderer_vulkan/nr_rdna2_fswin_raster_layout.h"
#include "video_core/renderer_vulkan/nr_rdna2_fswin_window_repack.h"
#include "video_core/vulkan_common/vulkan_wrapper.h"
namespace Vulkan {
class Device;
class Scheduler;
class MemoryAllocator;
class NrRdna2FSwinOutputView final {
public:
    NrRdna2FSwinOutputView(const Device&,MemoryAllocator&,Scheduler&);
    ~NrRdna2FSwinOutputView();
    NrRdna2FSwinOutputView(const NrRdna2FSwinOutputView&)=delete;
    NrRdna2FSwinOutputView& operator=(const NrRdna2FSwinOutputView&)=delete;
    // The user-supplied destination must be a writable STORAGE buffer,
    // valid for the entire submission. Source & destination never alias.
    [[nodiscard]] bool Initialize(
        VkDescriptorBufferInfo source,VkDescriptorBufferInfo destination,
        Eden::Graphics::NeuralRDNA2::FSwinWindowLayout layout,
        std::uint32_t channels,std::uint32_t width,std::uint32_t height);
    [[nodiscard]] bool Record();
    [[nodiscard]] bool Ready() const noexcept{return ready;}
private:
    const Device& device;
    MemoryAllocator& allocator;
    NrRdna2FSwinWindowRepack repacker;
    NrRdna2FSwinRasterLayout raster;
    vk::Buffer scratch;
    vk::DescriptorPool descriptor_pool;
    vk::DescriptorSets descriptor_sets;
    Eden::Graphics::NeuralRDNA2::FSwinOutputViewPlan plan{};
    bool ready{};
};
} // namespace Vulkan
