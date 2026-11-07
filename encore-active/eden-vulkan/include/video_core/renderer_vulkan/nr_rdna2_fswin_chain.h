// SPDX-License-Identifier: MIT
// Sequential same-shape FSwin network section (unshifted token windows).
// Each block owns separate VMA resources; output remains a Vulkan buffer.
// Cross-resolution transfers, shifted windows and image preprocessing
// intentionally require explicit adapters not included in this class.
#pragma once
#include "video_core/renderer_vulkan/nr_rdna2_fswin_layer_runtime.h"
#include "video_core/renderer_vulkan/nr_rdna2_fswin_window_repack.h"
#include "video_core/renderer_vulkan/nr_rdna2_fswin_output_view.h"
#include "video_core/renderer_vulkan/nr_rdna2_fswin_downsample_output.h"
#include "video_core/renderer_vulkan/nr_rdna2_fswin_raster_layout.h"
#include "video_core/renderer_vulkan/nr_rdna2_native_plan_bridge.h"
#include "rdna2_nr_fswin_chain_layout_policy.h"
#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>
namespace Vulkan {
class Device;
class Scheduler;
class MemoryAllocator;
struct FSwinLayerSpec {
    std::string prefix;
    std::uint32_t channels{};
    std::uint32_t windows{};
    bool residual_last{};
    // Explicit window raster/shift; legacy all-zero layout means unshifted.
    Eden::Graphics::NeuralRDNA2::FSwinWindowLayout layout{};
    bool input_view{};
    bool output_view{};
    bool downsample{};
};
class NrRdna2FSwinChain final {
public:
    NrRdna2FSwinChain(
        const Device& device,MemoryAllocator& allocator,Scheduler& scheduler);
    ~NrRdna2FSwinChain();
    NrRdna2FSwinChain(const NrRdna2FSwinChain&)=delete;
    NrRdna2FSwinChain& operator=(const NrRdna2FSwinChain&)=delete;
    [[nodiscard]] bool Initialize(
        const Eden::Graphics::NeuralRDNA2::NeuralModelFile& model,
        std::span<const FSwinLayerSpec> layers);
    // Parse the 19/21-column upstream plan, then initialize one
    // consecutive compatible FSwin section by its zero-based index.
    // Other plan families are NOT silently considered executed.
    [[nodiscard]] bool InitializeNativeSegment(
        const Eden::Graphics::NeuralRDNA2::NeuralModelFile& model,
        std::string_view native_plan,std::size_t segment_index);
    // Input is E4M3FN [window,64,C] and must be coherent with layer model.
    [[nodiscard]] bool UploadInput(
        std::span<const std::uint8_t> tokens);
    // Accept an already E4M3FN-encoded 2D feature raster, then pack it
    // losslessly into 8x8 windows for the first FSwin layer. Not RGB input.
    // Requires an explicit, unshifted initial layout and host-side staging.
    [[nodiscard]] bool UploadRasterFeatures(
        std::span<const std::uint8_t> features,
        std::uint32_t width,std::uint32_t height);
    // Connect a producer's E4M3FN raster directly to the first
    // input-view layer via a GPU raster->windows compute pass.
    // Source must be a readable STORAGE buffer and stay alive in-flight.
    [[nodiscard]] bool BindRasterInput(
        VkDescriptorBufferInfo source,std::uint32_t width,
        std::uint32_t height);
    // Full-res feature raster accepted also by a plain FSwin block
    // immediately following an _upsample; shifted first-layer windows
    // are repacked through a GPU-owned scratch buffer.
    [[nodiscard]] bool AllowsGpuRasterInput() const noexcept {
        return ready&&!layers.empty()&&!input_set&&!layers.front()->HasInput();
    }
    // A _outview chain requires an explicit output feature-raster buffer
    // before Record(). This adapter also undoes the final 4x4-tile shift.
    // The buffer must be writable STORAGE and remain alive across execution.
    [[nodiscard]] bool BindRasterOutput(
        VkDescriptorBufferInfo destination,std::uint32_t width,
        std::uint32_t height);
    // Optional unquantized preblock output (FP16 C32, 64 bytes per pixel).
    // It is extracted by a separate dense-3 pass BEFORE E4M3 rounding,
    // then unpacked into the full-resolution raster. Never pass FP8 here.
    [[nodiscard]] bool BindRawRasterOutput(
        VkDescriptorBufferInfo destination,std::uint32_t width,
        std::uint32_t height);
    [[nodiscard]] VkDescriptorBufferInfo ResultRawRaster() const noexcept{
        return ready?raw_raster_destination:VkDescriptorBufferInfo{};
    }
    // Terminal _ds: bind the learned 2x2 pooled [2C,C] output to a
    // caller-owned half-resolution FP8 raster (not an RGB framebuffer).
    [[nodiscard]] bool BindDownsampleOutput(
        const Eden::Graphics::NeuralRDNA2::NeuralModelFile& model,
        VkDescriptorBufferInfo destination,
        std::uint32_t input_width,std::uint32_t input_height);
    // Preserve the pre-downsample C-channel features in an independent
    // full-resolution raster for the U-Net skip path. This does NOT
    // replace the 2C-channel downsample destination.
    [[nodiscard]] bool BindSkipRasterOutput(
        VkDescriptorBufferInfo destination,
        std::uint32_t input_width,std::uint32_t input_height);
    [[nodiscard]] VkDescriptorBufferInfo ResultSkipRaster() const noexcept{
        return ready?skip_destination:VkDescriptorBufferInfo{};
    }
    // Only usable after the corresponding command buffer has completed.
    [[nodiscard]] VkDescriptorBufferInfo ResultRaster() const noexcept{
        return ready?raster_destination:VkDescriptorBufferInfo{};
    }
    [[nodiscard]] bool Record();
    [[nodiscard]] bool Ready() const noexcept{return ready;}
    [[nodiscard]] bool RequiresRasterInput() const noexcept {
        return ready&&input_view;
    }
    [[nodiscard]] bool RequiresRasterOutput() const noexcept {
        return ready&&(output_view||downsample);
    }
    [[nodiscard]] bool RequiresDownsampleOutput() const noexcept {
        return ready&&downsample;
    }
    [[nodiscard]] std::uint32_t Channels() const noexcept {
        return !ready||layers.empty()?0u:
               layers.front()->Plan().dense[0].channels;
    }
    [[nodiscard]] Eden::Graphics::NeuralRDNA2::FSwinWindowLayout
    InputWindowLayout() const noexcept {
        return !ready||layouts.empty()?
            Eden::Graphics::NeuralRDNA2::FSwinWindowLayout{}:
            layouts.front();
    }
    [[nodiscard]] Eden::Graphics::NeuralRDNA2::FSwinWindowLayout
    OutputWindowLayout() const noexcept {
        return !ready||layouts.empty()?
            Eden::Graphics::NeuralRDNA2::FSwinWindowLayout{}:
            layouts.back();
    }
    [[nodiscard]] Scheduler* SchedulerIdentity() const noexcept{
        return &scheduler;
    }
    [[nodiscard]] VkDescriptorBufferInfo Input() const noexcept {
        return !ready||layers.empty()?VkDescriptorBufferInfo{}:
            layers.front()->Input();
    }
    [[nodiscard]] std::size_t LayerCount() const noexcept {
        return layers.size();
    }
    [[nodiscard]] VkDescriptorBufferInfo Output() const noexcept {
        return layers.empty()?VkDescriptorBufferInfo{}:layers.back()->Output();
    }
private:
    const Device& device;
    MemoryAllocator& allocator;
    Scheduler& scheduler;
    std::vector<std::unique_ptr<NrRdna2FSwinLayerRuntime>> layers;
    NrRdna2FSwinWindowRepack window_repack;
    NrRdna2FSwinRasterLayout raster_input;
    std::unique_ptr<NrRdna2FSwinOutputView> raster_output;
    std::unique_ptr<NrRdna2FSwinOutputView> raw_raster_output;
    vk::Buffer raw_window_staging;
    VkDescriptorBufferInfo raw_raster_destination{};
    std::unique_ptr<NrRdna2FSwinDownsampleOutput> downsample_output;
    VkDescriptorBufferInfo raster_destination{};
    VkDescriptorBufferInfo skip_destination{};
    std::string downsample_prefix;
    vk::DescriptorPool input_pool;
    vk::DescriptorSets input_allocations;
    VkDescriptorSet input_set{VK_NULL_HANDLE};
    vk::Buffer input_window_staging;
    vk::DescriptorPool input_shift_pool;
    vk::DescriptorSets input_shift_allocations;
    VkDescriptorSet input_shift_set{VK_NULL_HANDLE};
    Eden::Graphics::NeuralRDNA2::FSwinWindowRepack input_shift_plan{};
    Eden::Graphics::NeuralRDNA2::FSwinRasterLayout input_plan{};
    Eden::Graphics::NeuralRDNA2::FSwinRasterLayoutStorage input_storage{};
    vk::DescriptorPool window_pool;
    vk::DescriptorSets window_allocations;
    std::vector<VkDescriptorSet> window_sets;
    std::vector<Eden::Graphics::NeuralRDNA2::FSwinWindowRepack> window_plans;
    std::vector<Eden::Graphics::NeuralRDNA2::FSwinWindowLayout> layouts;
    bool ready{};
    bool input_view{};
    bool output_view{};
    bool downsample{};
};
} // namespace Vulkan
