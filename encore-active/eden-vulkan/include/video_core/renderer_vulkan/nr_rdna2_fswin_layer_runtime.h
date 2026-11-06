// SPDX-License-Identifier: MIT
// A complete, self-owned FSwin BLOCK backend: CPU model import -> Vulkan
// model buffer -> activation tensors -> descriptors -> ordered dispatches.
// Input remains a preconverted 64-token/window E4M3FN tensor, not an image.
#pragma once
#include "video_core/renderer_vulkan/nr_rdna2_fswin_plan.h"
#include "video_core/renderer_vulkan/nr_rdna2_fswin_bindings.h"
#include "video_core/renderer_vulkan/nr_rdna2_fswin_weight_loader.h"
#include "video_core/renderer_vulkan/nr_rdna2_fswin_gpu_tensors.h"
#include "video_core/renderer_vulkan/nr_rdna2_fswin_descriptors.h"
#include "video_core/renderer_vulkan/nr_rdna2_gpu_weights.h"
#include <cstdint>
#include <span>
#include <string_view>
namespace Vulkan {
class Device;
class MemoryAllocator;
class Scheduler;
class NrRdna2FSwinLayerRuntime final {
public:
    NrRdna2FSwinLayerRuntime(
        const Device& device,MemoryAllocator& allocator,Scheduler& scheduler);
    ~NrRdna2FSwinLayerRuntime();
    NrRdna2FSwinLayerRuntime(const NrRdna2FSwinLayerRuntime&)=delete;
    NrRdna2FSwinLayerRuntime&
        operator=(const NrRdna2FSwinLayerRuntime&)=delete;
    // A failed initialization requires reconstructing this runtime.
    // Model is user-supplied; weights are copied and no longer needed after.
    [[nodiscard]] bool Initialize(
        const Eden::Graphics::NeuralRDNA2::NeuralModelFile& model,
        std::string_view layer_prefix,std::uint32_t channels,
        std::uint32_t windows,bool residual_last);
    // Caller must ensure prior GPU uses of input have finished.
    [[nodiscard]] bool UploadInput(
        std::span<const std::uint8_t> e4m3_window_tokens);
    // GPU-to-GPU copy from the immediately preceding FSwin layer.
    // Both runtimes must share one scheduler, width, channels and layout.
    [[nodiscard]] bool LinkInputFrom(const NrRdna2FSwinLayerRuntime& previous);
    [[nodiscard]] bool Record();
    [[nodiscard]] bool Ready() const noexcept{return ready;}
    [[nodiscard]] const Eden::Graphics::NeuralRDNA2::FSwinGraph&
        Plan() const noexcept{return plan;}
private:
    Scheduler& scheduler;
    NrRdna2FSwinGraph graph;
    NrRdna2GpuWeights gpu_weights;
    NrRdna2FSwinGpuTensors tensors;
    NrRdna2FSwinDescriptors descriptors;
    Eden::Graphics::NeuralRDNA2::FSwinGraph plan{};
    bool ready{};
    bool input_loaded{};
    bool output_recorded{};
};
} // namespace Vulkan
