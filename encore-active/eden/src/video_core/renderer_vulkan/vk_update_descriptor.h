// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

// SPDX-FileCopyrightText: Copyright 2019 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#ifdef PS5_NATIVE
#include <cstring>
#endif
#include <variant>
#include <vector>
#include "video_core/vulkan_common/vulkan_wrapper.h"

namespace Vulkan {

class Device;
class Scheduler;

struct DescriptorAddress {
    VkDeviceAddress address;
    VkDeviceSize range;
    VkFormat format;
};

union DescriptorUpdateEntry {
    DescriptorUpdateEntry() = default;
    DescriptorUpdateEntry(VkDescriptorImageInfo image_) : image{image_} {}
    DescriptorUpdateEntry(VkDescriptorBufferInfo buffer_) : buffer{buffer_} {}
    DescriptorUpdateEntry(VkBufferView texel_buffer_) : texel_buffer{texel_buffer_} {}
    DescriptorUpdateEntry(DescriptorAddress address_) : address{address_} {}
    std::monostate empty{};
    VkDescriptorImageInfo image;
    VkDescriptorBufferInfo buffer;
    VkBufferView texel_buffer;
    DescriptorAddress address;
};

class UpdateDescriptorQueue final {
    // This should be plenty for the vast majority of cases. Most desktop platforms only
    // provide up to 3 swapchain images.
    static constexpr size_t FRAMES_IN_FLIGHT = 8;

public:
    static constexpr size_t GUEST_FRAME_PAYLOAD_SIZE = 0x80000;
    static constexpr size_t COMPUTE_FRAME_PAYLOAD_SIZE = 0x20000;

    explicit UpdateDescriptorQueue(const Device& device_, size_t frame_payload_size_,
                                   bool supports_descriptor_buffer_ = false);
    ~UpdateDescriptorQueue();

    [[nodiscard]] bool UsesDescriptorBuffer() const noexcept {
        return use_descriptor_buffer;
    }

    void TickFrame();
    void Acquire(Scheduler& scheduler, size_t required_entries = 0,
                 bool use_descriptor_buffer_ = false);
#ifdef PS5_NATIVE
    // Exact zero-descriptor graphics pipeline: publish an empty payload
    // without the ordinary 1024-entry fallback reservation or GPU wait.
    // Acquire(0) keeps its legacy semantics for other callers.
    void AcquireKnownEmpty(bool use_descriptor_buffer_) noexcept {
        use_descriptor_buffer = supports_descriptor_buffer && use_descriptor_buffer_;
        upload_start = payload_cursor;
    }
    template <typename WaitWorker>
    [[nodiscard]] bool PreflightGuestDraw(WaitWorker&& wait_worker,
                                          size_t required_entries = 0x400) {
        // Preserve the old default for any other caller. Native draw/compute
        // supply the exact positive descriptor count passed to Acquire().
        const size_t reserve = required_entries != 0 ? required_entries : 0x400;
        if (reserve >= frame_payload_size)
            return false;
        const size_t used = static_cast<size_t>(payload_cursor - payload_start);
        if (used + reserve < frame_payload_size)
            return false;
        wait_worker();
        payload_cursor = payload_start;
        return true;
    }
    template <typename WaitWorker>
    [[nodiscard]] bool PreflightExactGuestDraw(WaitWorker&& wait_worker, size_t entries) {
        // Match Acquire(required_entries) for every nonempty pipeline,
        // including fewer than 1024 descriptors after a worker-triggered
        // shader reselect. Zero-descriptor pipelines must skip this helper.
        if (entries == 0 || entries >= frame_payload_size)
            return false;
        const size_t used = static_cast<size_t>(payload_cursor - payload_start);
        if (used + entries < frame_payload_size)
            return false;
        wait_worker();
        payload_cursor = payload_start;
        return true;
    }
#endif

    const DescriptorUpdateEntry* UpdateData() const noexcept {
        return upload_start;
    }

    void AddSampledImage(VkImageView image_view, VkSampler sampler) {
#ifdef PS5_NATIVE
        // Both descriptor-buffer and descriptor-set paths compare snapshots.
        // Initialize the entire union including unused Vulkan image padding.
        auto* const entry = payload_cursor++;
        std::memset(entry, 0, sizeof(*entry));
        entry->image.sampler = sampler;
        entry->image.imageView = image_view;
        entry->image.imageLayout = VK_IMAGE_LAYOUT_GENERAL;
#else
        *(payload_cursor++) = VkDescriptorImageInfo{
            .sampler = sampler,
            .imageView = image_view,
            .imageLayout = VK_IMAGE_LAYOUT_GENERAL,
        };
#endif
    }

    void AddImage(VkImageView image_view) {
#ifdef PS5_NATIVE
        auto* const entry = payload_cursor++;
        std::memset(entry, 0, sizeof(*entry));
        entry->image.sampler = VK_NULL_HANDLE;
        entry->image.imageView = image_view;
        entry->image.imageLayout = VK_IMAGE_LAYOUT_GENERAL;
#else
        *(payload_cursor++) = VkDescriptorImageInfo{
            .sampler = VK_NULL_HANDLE,
            .imageView = image_view,
            .imageLayout = VK_IMAGE_LAYOUT_GENERAL,
        };
#endif
    }

    void AddBuffer(VkBuffer buffer, VkDeviceSize offset, VkDeviceSize size) {
        *(payload_cursor++) = VkDescriptorBufferInfo{
            .buffer = buffer,
            .offset = offset,
            .range = size,
        };
    }

    void AddBuffer(VkBuffer buffer, VkDeviceAddress base_address, VkDeviceSize offset,
                   VkDeviceSize size) {
        if (!use_descriptor_buffer) {
            AddBuffer(buffer, offset, size);
            return;
        }
#ifdef PS5_NATIVE
        auto* const entry = payload_cursor++;
        std::memset(entry, 0, sizeof(*entry));
        entry->address.address = base_address == 0 ? 0 : base_address + offset;
        entry->address.range = base_address == 0 ? VK_WHOLE_SIZE : size;
        entry->address.format = VK_FORMAT_UNDEFINED;
#else
        *(payload_cursor++) = DescriptorAddress{
            .address = base_address == 0 ? 0 : base_address + offset,
            .range = base_address == 0 ? VK_WHOLE_SIZE : size,
            .format = VK_FORMAT_UNDEFINED,
        };
#endif
    }

    void AddTexelBuffer(VkBufferView texel_buffer) {
#ifdef PS5_NATIVE
        // Descriptor snapshots are compared byte-for-byte on later draws.
        // A texel view fills only the first union field: stale padding from
        // a previous descriptor otherwise defeats payload reuse on the HUD.
        auto* const entry = payload_cursor++;
        std::memset(entry, 0, sizeof(*entry));
        entry->texel_buffer = texel_buffer;
#else
        *(payload_cursor++) = texel_buffer;
#endif
    }

    void AddTexelBuffer(VkBufferView texel_buffer, VkDeviceAddress base_address,
                        VkDeviceSize offset, VkDeviceSize size, VkFormat format) {
        if (!use_descriptor_buffer) {
            AddTexelBuffer(texel_buffer);
            return;
        }
#ifdef PS5_NATIVE
        auto* const entry = payload_cursor++;
        std::memset(entry, 0, sizeof(*entry));
        entry->address.address = base_address == 0 ? 0 : base_address + offset;
        entry->address.range = base_address == 0 ? VK_WHOLE_SIZE : size;
        entry->address.format = format;
#else
        *(payload_cursor++) = DescriptorAddress{
            .address = base_address == 0 ? 0 : base_address + offset,
            .range = base_address == 0 ? VK_WHOLE_SIZE : size,
            .format = format,
        };
#endif
    }

private:
    const Device& device;
    const size_t frame_payload_size;
    const bool supports_descriptor_buffer;
    bool use_descriptor_buffer{false};
    size_t frame_index{0};
    DescriptorUpdateEntry* payload_cursor = nullptr;
    DescriptorUpdateEntry* payload_start = nullptr;
    const DescriptorUpdateEntry* upload_start = nullptr;
    std::vector<DescriptorUpdateEntry> payload;
};

// TODO: should these be separate classes instead?
using GuestDescriptorQueue = UpdateDescriptorQueue;
using ComputePassDescriptorQueue = UpdateDescriptorQueue;

} // namespace Vulkan
