#ifdef EDEN_NVNF_DEQUEUE_PROFILE
#include "performance.h"
#endif
// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

// SPDX-FileCopyrightText: Copyright 2021 yuzu Emulator Project
// SPDX-FileCopyrightText: Copyright 2014 The Android Open Source Project
// SPDX-License-Identifier: GPL-3.0-or-later
// Parts of this implementation were based on:
// https://cs.android.com/android/platform/superproject/+/android-5.1.1_r38:frameworks/native/libs/gui/BufferQueueProducer.cpp

#include "common/assert.h"
#include "common/logging.h"
#include "common/settings.h"
#include "common/cpu_features.h"
#include "core/hle/kernel/k_event.h"
#include "core/hle/kernel/k_readable_event.h"
#include "core/hle/kernel/kernel.h"
#include "core/hle/service/kernel_helpers.h"
#include "core/hle/service/nvnflinger/buffer_queue_core.h"
#include "core/hle/service/nvnflinger/buffer_queue_producer.h"
#include "core/hle/service/nvnflinger/consumer_listener.h"
#include "core/hle/service/nvnflinger/parcel.h"
#include "core/hle/service/nvnflinger/producer_listener.h"
#include <unordered_map>
#include "core/hle/service/nvnflinger/ui/graphic_buffer.h"
#include "core/hle/service/nvnflinger/window.h"

namespace Service::android {

// A guest waiting on BufferQueueProducer::GetNativeHandle must also wake
// when the consumer releases an acquired slot. The host dequeue condition
// variable alone cannot wake that guest kernel event.
//
// The listener is held independently of the producer because ReleaseBuffer
// copies the listener out of the queue mutex before invoking its callback.
// Stop() synchronizes with in-flight notifications before CloseEvent(), so
// even a delayed release cannot signal a destroyed kernel event.
class ProducerReleaseWake final : public IProducerListener {
public:
    ProducerReleaseWake(Kernel::KEvent* event_, Kernel::KernelCore& kernel_)
        : event{event_}, kernel{kernel_} {}

    void SetDelegate(const std::shared_ptr<IProducerListener>& listener) {
        std::scoped_lock lock{mutex};
        delegate = listener.get() == this ? nullptr : listener;
    }

    void Stop() {
        std::scoped_lock lock{mutex};
        event = nullptr;
        delegate.reset();
    }

    void OnBufferReleased() override {
        std::shared_ptr<IProducerListener> forward;
        {
            std::scoped_lock lock{mutex};
            if (!event) return;
            event->Signal(kernel);
            forward = delegate;
        }
        if (forward) forward->OnBufferReleased();
    }

private:
    std::mutex mutex;
    Kernel::KEvent* event;
    Kernel::KernelCore& kernel;
    std::shared_ptr<IProducerListener> delegate;
};

// Keep the pinned BufferQueueProducer declaration unchanged. Registrations
// exist only for active producers and are removed on destruction; the queue
// may retain a callback copy past that point, safely deactivated by Stop().
namespace {
std::mutex release_wakes_mutex;
std::unordered_map<const BufferQueueProducer*, std::shared_ptr<ProducerReleaseWake>> release_wakes;

std::shared_ptr<ProducerReleaseWake> GetReleaseWake(const BufferQueueProducer* producer) {
    std::scoped_lock lock{release_wakes_mutex};
    const auto it = release_wakes.find(producer);
    return it == release_wakes.end() ? nullptr : it->second;
}
} // namespace

BufferQueueProducer::BufferQueueProducer(Service::KernelHelpers::ServiceContext& service_context_,
                                         std::shared_ptr<BufferQueueCore> buffer_queue_core_,
                                         Service::Nvidia::NvCore::NvMap& nvmap_)
    : service_context{service_context_}, core{std::move(buffer_queue_core_)}
    , slots(core->slots)
    , nvmap(nvmap_)
{
    buffer_wait_event = service_context.CreateEvent("BufferQueue:WaitEvent");
    auto wake = std::make_shared<ProducerReleaseWake>(buffer_wait_event, service_context.kernel);
    std::scoped_lock lock{release_wakes_mutex};
    release_wakes.emplace(this, std::move(wake));
}

BufferQueueProducer::~BufferQueueProducer() {
    std::shared_ptr<ProducerReleaseWake> wake;
    {
        std::scoped_lock lock{release_wakes_mutex};
        const auto it = release_wakes.find(this);
        if (it != release_wakes.end()) {
            wake = std::move(it->second);
            release_wakes.erase(it);
        }
    }
    if (wake) wake->Stop();
    service_context.CloseEvent(buffer_wait_event);
}

Status BufferQueueProducer::RequestBuffer(s32 slot, std::shared_ptr<GraphicBuffer>* buf) {
    LOG_DEBUG(Service_Nvnflinger, "slot {}", slot);

    std::scoped_lock lock{core->mutex};

    if (core->is_abandoned) {
        LOG_ERROR(Service_Nvnflinger, "BufferQueue has been abandoned");
        return Status::NoInit;
    }
    if (slot < 0 || slot >= BufferQueueDefs::NUM_BUFFER_SLOTS) {
        LOG_ERROR(Service_Nvnflinger, "slot index {} out of range [0, {})", slot,
                  BufferQueueDefs::NUM_BUFFER_SLOTS);
        return Status::BadValue;
    } else if (slots[slot].buffer_state != BufferState::Dequeued) {
        LOG_ERROR(Service_Nvnflinger, "slot {} is not owned by the producer (state = {})", slot,
                  slots[slot].buffer_state);
        return Status::BadValue;
    }

    slots[slot].request_buffer_called = true;
    *buf = slots[slot].graphic_buffer;

    return Status::NoError;
}

Status BufferQueueProducer::SetBufferCount(s32 buffer_count) {
    LOG_DEBUG(Service_Nvnflinger, "count = {}", buffer_count);

    std::shared_ptr<IConsumerListener> listener;
    {
        std::unique_lock lock{core->mutex};
        core->WaitWhileAllocatingLocked();

        if (core->is_abandoned) {
            LOG_ERROR(Service_Nvnflinger, "BufferQueue has been abandoned");
            return Status::NoInit;
        }

        if (buffer_count > BufferQueueDefs::NUM_BUFFER_SLOTS) {
            LOG_ERROR(Service_Nvnflinger, "buffer_count {} too large (max {})", buffer_count,
                      BufferQueueDefs::NUM_BUFFER_SLOTS);
            return Status::BadValue;
        }

        // There must be no dequeued buffers when changing the buffer count.
        for (s32 s{}; s < BufferQueueDefs::NUM_BUFFER_SLOTS; ++s) {
            if (slots[s].buffer_state == BufferState::Dequeued) {
                LOG_ERROR(Service_Nvnflinger, "buffer owned by producer");
                return Status::BadValue;
            }
        }

        if (buffer_count == 0) {
            core->override_max_buffer_count = 0;
            core->SignalDequeueCondition();
            // Publish only after releasing the queue mutex: kernel event
            // signaling acquires the scheduler lock.
            lock.unlock();
            buffer_wait_event->Signal(service_context.kernel);
            return Status::NoError;
        }

        const s32 min_buffer_slots = core->GetMinMaxBufferCountLocked(false);
        if (buffer_count < min_buffer_slots) {
            LOG_ERROR(Service_Nvnflinger, "requested buffer count {} is less than minimum {}",
                      buffer_count, min_buffer_slots);
            return Status::BadValue;
        }

        // Here we are guaranteed that the producer doesn't have any dequeued buffers and will
        // release all of its buffer references.
        if (core->GetPreallocatedBufferCountLocked() <= 0) {
            core->FreeAllBuffersLocked();
        }

        core->override_max_buffer_count = buffer_count;
        core->SignalDequeueCondition();
        listener = core->consumer_listener;
    }
    buffer_wait_event->Signal(service_context.kernel);

    // Call back without lock held
    if (listener != nullptr) {
        listener->OnBuffersReleased();
    }

    return Status::NoError;
}

Status BufferQueueProducer::WaitForFreeSlotThenRelock(bool async, s32* found, Status* return_flags,
                                                      std::unique_lock<std::mutex>& lk) const {
    bool try_again = true;

    while (try_again) {
        if (core->is_abandoned) {
            LOG_ERROR(Service_Nvnflinger, "BufferQueue has been abandoned");
            return Status::NoInit;
        }

        const s32 max_buffer_count = core->GetMaxBufferCountLocked(async);
        if (async && core->override_max_buffer_count) {
            if (core->override_max_buffer_count < max_buffer_count) {
                *found = BufferQueueCore::INVALID_BUFFER_SLOT;
                return Status::BadValue;
            }
        }

        // Free up any buffers that are in slots beyond the max buffer count
        for (s32 s = max_buffer_count; s < BufferQueueDefs::NUM_BUFFER_SLOTS; ++s) {
            ASSERT(slots[s].buffer_state == BufferState::Free);
            if (slots[s].graphic_buffer != nullptr && slots[s].buffer_state == BufferState::Free &&
                !slots[s].is_preallocated) {
                core->FreeBufferLocked(s);
                *return_flags |= Status::ReleaseAllBuffers;
            }
        }

        // Look for a free buffer to give to the client
        *found = BufferQueueCore::INVALID_BUFFER_SLOT;
        s32 dequeued_count{};
        s32 acquired_count{};
        for (s32 s{}; s < max_buffer_count; ++s) {
            switch (slots[s].buffer_state) {
            case BufferState::Dequeued:
                ++dequeued_count;
                break;
            case BufferState::Acquired:
                ++acquired_count;
                break;
            case BufferState::Free:
                // We return the oldest of the free buffers to avoid stalling the producer if
                // possible, since the consumer may still have pending reads of in-flight buffers
                if (*found == BufferQueueCore::INVALID_BUFFER_SLOT ||
                    slots[s].frame_number < slots[*found].frame_number) {
                    *found = s;
                }
                break;
            default:
                break;
            }
        }

        // Producers are not allowed to dequeue more than one buffer if they did not set a buffer
        // count
        if (!core->override_max_buffer_count && dequeued_count) {
            LOG_ERROR(Service_Nvnflinger,
                      "can't dequeue multiple buffers without setting the buffer count");
            return Status::InvalidOperation;
        }

        // See whether a buffer has been queued since the last SetBufferCount so we know whether to
        // perform the min undequeued buffers check below
        if (core->buffer_has_been_queued) {
            // Make sure the producer is not trying to dequeue more buffers than allowed
            const s32 new_undequeued_count = max_buffer_count - (dequeued_count + 1);
            const s32 min_undequeued_count = core->GetMinUndequeuedBufferCountLocked(async);
            if (new_undequeued_count < min_undequeued_count) {
                LOG_ERROR(Service_Nvnflinger,
                          "min undequeued buffer count({}) exceeded (dequeued={} undequeued={})",
                          min_undequeued_count, dequeued_count, new_undequeued_count);
                return Status::InvalidOperation;
            }
        }

        // If we disconnect and reconnect quickly, we can be in a state where our slots are empty
        // but we have many buffers in the queue. This can cause us to run out of memory if we
        // outrun the consumer. Wait here if it looks like we have too many buffers queued up.
        const bool too_many_buffers = core->queue.size() > static_cast<size_t>(max_buffer_count);
        if (too_many_buffers) {
            // Backpressure is an expected queue state, not a service failure.
            // Avoid per-wakeup ERROR formatting/output on the producer hot path.
            LOG_DEBUG(Service_Nvnflinger, "queue size is {}, waiting", core->queue.size());
        }

        // If no buffer is found, or if the queue has too many buffers outstanding, wait for a
        // buffer to be acquired or released, or for the max buffer count to change.
        try_again = (*found == BufferQueueCore::INVALID_BUFFER_SLOT) || too_many_buffers;
        if (try_again) {
            // Return an error if we're in non-blocking mode (producer and consumer are controlled
            // by the application).
            if (core->dequeue_buffer_cannot_block &&
                (acquired_count <= core->max_acquired_buffer_count)) {
                return Status::WouldBlock;
            }

#ifdef EDEN_NVNF_DEQUEUE_PROFILE
            // F05: a producer stuck inside this wait never contributes to
            // completed DequeueBuffer timings. Publish in-flight waiters only
            // in the opt-in profile, without changing the blocking predicate.
            ::Eden::Performance::GuestDequeueBlockedScope blocking_scope;
#endif
            if (!core->WaitForDequeueCondition(lk)) {
                // We are no longer running
                return Status::NoError;
            }
        }
    }

    return Status::NoError;
}

Status BufferQueueProducer::DequeueBuffer(s32* out_slot, Fence* out_fence, bool async, u32 width,
                                          u32 height, PixelFormat format, u32 usage) {
    LOG_DEBUG(Service_Nvnflinger, "async={} w={} h={} format={}, usage={}",
              async ? "true" : "false", width, height, format, usage);

    if ((width != 0 && height == 0) || (width == 0 && height != 0)) {
        LOG_ERROR(Service_Nvnflinger, "invalid size: w={} h={}", width, height);
        return Status::BadValue;
    }

    Status return_flags = Status::NoError;
    bool attached_by_consumer = false;
    u64 returned_frame_number{};
    {
        std::unique_lock lock{core->mutex};
        core->WaitWhileAllocatingLocked();

        if (format == PixelFormat::NoFormat) {
            format = core->default_buffer_format;
        }

        // Enable the usage bits the consumer requested
        usage |= core->consumer_usage_bit;

        s32 found{};
#ifdef EDEN_NVNF_DEQUEUE_PROFILE
        const auto dequeue_start = ::Eden::Performance::detailed_gpu_profile.load(std::memory_order_relaxed) ? ::Eden::Performance::NowNs() : 0;
#endif
        Status status = WaitForFreeSlotThenRelock(async, &found, &return_flags, lock);
#ifdef EDEN_NVNF_DEQUEUE_PROFILE
        if (dequeue_start) ::Eden::Performance::AddSince(::Eden::Performance::guest_dequeue_wait, dequeue_start);
#endif
        if (status != Status::NoError) {
            return status;
        }

        // This should not happen
        if (found == BufferQueueCore::INVALID_BUFFER_SLOT) {
            LOG_ERROR(Service_Nvnflinger, "no available buffer slots");
            return Status::Busy;
        }

        *out_slot = found;

        attached_by_consumer = slots[found].attached_by_consumer;

        const bool use_default_size = !width && !height;
        if (use_default_size) {
            width = core->default_width;
            height = core->default_height;
        }

        slots[found].buffer_state = BufferState::Dequeued;

        const std::shared_ptr<GraphicBuffer>& buffer(slots[found].graphic_buffer);
        if ((buffer == nullptr) || (buffer->Width() != width) || (buffer->Height() != height) ||
            (buffer->Format() != format) || ((buffer->Usage() & usage) != usage)) {
            slots[found].acquire_called = false;
            slots[found].graphic_buffer = nullptr;
            slots[found].request_buffer_called = false;
            slots[found].fence = Fence::NoFence();

            return_flags |= Status::BufferNeedsReallocation;
        }

        *out_fence = slots[found].fence;
        slots[found].fence = Fence::NoFence();
        // Snapshot the slot metadata while the queue mutex is still owned.
        returned_frame_number = slots[found].frame_number;
    }

    if ((return_flags & Status::BufferNeedsReallocation) != Status::None) {
        LOG_DEBUG(Service_Nvnflinger, "allocating a new buffer for slot {}", *out_slot);

        auto graphic_buffer = std::make_shared<GraphicBuffer>(width, height, format, usage);
        if (graphic_buffer == nullptr) {
            LOG_ERROR(Service_Nvnflinger, "creating GraphicBuffer failed");
            return Status::NoMemory;
        }

        {
            std::scoped_lock lock{core->mutex};

            if (core->is_abandoned) {
                LOG_ERROR(Service_Nvnflinger, "BufferQueue has been abandoned");
                return Status::NoInit;
            }

            slots[*out_slot].frame_number = UINT32_MAX;
            returned_frame_number = UINT32_MAX;
            slots[*out_slot].graphic_buffer = graphic_buffer;
        }
    }

    if (attached_by_consumer) {
        return_flags |= Status::BufferNeedsReallocation;
    }

    LOG_DEBUG(Service_Nvnflinger, "returning slot={} frame={}, flags={}", *out_slot,
              returned_frame_number, return_flags);

    return return_flags;
}

Status BufferQueueProducer::DetachBuffer(s32 slot) {
    LOG_DEBUG(Service_Nvnflinger, "slot {}", slot);

    std::unique_lock lock{core->mutex};

    if (core->is_abandoned) {
        LOG_ERROR(Service_Nvnflinger, "BufferQueue has been abandoned");
        return Status::NoInit;
    }

    if (slot < 0 || slot >= BufferQueueDefs::NUM_BUFFER_SLOTS) {
        LOG_ERROR(Service_Nvnflinger, "slot {} out of range [0, {})", slot,
                  BufferQueueDefs::NUM_BUFFER_SLOTS);
        return Status::BadValue;
    } else if (slots[slot].buffer_state != BufferState::Dequeued) {
        LOG_ERROR(Service_Nvnflinger, "slot {} is not owned by the producer (state = {})", slot,
                  slots[slot].buffer_state);
        return Status::BadValue;
    } else if (!slots[slot].request_buffer_called) {
        LOG_ERROR(Service_Nvnflinger, "buffer in slot {} has not been requested", slot);
        return Status::BadValue;
    }

    core->FreeBufferLocked(slot);
    core->SignalDequeueCondition();
    // Host waiter publication stays under the queue lock; guest event
    // publication must be outside it to avoid scheduler lock inversion.
    lock.unlock();
    buffer_wait_event->Signal(service_context.kernel);

    return Status::NoError;
}

Status BufferQueueProducer::DetachNextBuffer(std::shared_ptr<GraphicBuffer>* out_buffer,
                                             Fence* out_fence) {
    if (out_buffer == nullptr) {
        LOG_ERROR(Service_Nvnflinger, "out_buffer must not be nullptr");
        return Status::BadValue;
    } else if (out_fence == nullptr) {
        LOG_ERROR(Service_Nvnflinger, "out_fence must not be nullptr");
        return Status::BadValue;
    }

    std::unique_lock lock{core->mutex};
    core->WaitWhileAllocatingLocked();

    if (core->is_abandoned) {
        LOG_ERROR(Service_Nvnflinger, "BufferQueue has been abandoned");
        return Status::NoInit;
    }

    // Find the oldest valid slot
    int found = BufferQueueCore::INVALID_BUFFER_SLOT;
    for (int s = 0; s < BufferQueueDefs::NUM_BUFFER_SLOTS; ++s) {
        if (slots[s].buffer_state == BufferState::Free && slots[s].graphic_buffer != nullptr) {
            if (found == BufferQueueCore::INVALID_BUFFER_SLOT ||
                slots[s].frame_number < slots[found].frame_number) {
                found = s;
            }
        }
    }

    if (found == BufferQueueCore::INVALID_BUFFER_SLOT) {
        return Status::NoMemory;
    }

    LOG_DEBUG(Service_Nvnflinger, "Detached slot {}", found);

    *out_buffer = slots[found].graphic_buffer;
    *out_fence = slots[found].fence;

    core->FreeBufferLocked(found);
    // Publish host dequeue-generation transition under the queue lock,
    // then signal the Binder guest event only after dropping that lock.
    core->SignalDequeueCondition();
    lock.unlock();
    buffer_wait_event->Signal(service_context.kernel);

    return Status::NoError;
}

Status BufferQueueProducer::AttachBuffer(s32* out_slot,
                                         const std::shared_ptr<GraphicBuffer>& buffer) {
    if (out_slot == nullptr) {
        LOG_ERROR(Service_Nvnflinger, "out_slot must not be nullptr");
        return Status::BadValue;
    } else if (buffer == nullptr) {
        LOG_ERROR(Service_Nvnflinger, "Cannot attach nullptr buffer");
        return Status::BadValue;
    }

    std::unique_lock lock{core->mutex};
    core->WaitWhileAllocatingLocked();

    Status return_flags = Status::NoError;
    s32 found{};

    const auto status = WaitForFreeSlotThenRelock(false, &found, &return_flags, lock);
    if (status != Status::NoError) {
        return status;
    }

    // This should not happen
    if (found == BufferQueueCore::INVALID_BUFFER_SLOT) {
        LOG_ERROR(Service_Nvnflinger, "No available buffer slots");
        return Status::Busy;
    }

    *out_slot = found;

    LOG_DEBUG(Service_Nvnflinger, "Returning slot {} flags={}", *out_slot, return_flags);

    slots[*out_slot].graphic_buffer = buffer;
    slots[*out_slot].buffer_state = BufferState::Dequeued;
    slots[*out_slot].fence = Fence::NoFence();
    slots[*out_slot].request_buffer_called = true;

    return return_flags;
}

Status BufferQueueProducer::QueueBuffer(s32 slot, const QueueBufferInput& input, QueueBufferOutput* output) {
    s64 timestamp{};
    bool is_auto_timestamp{};
    Common::Rectangle<s32> crop;
    NativeWindowScalingMode scaling_mode{};
    NativeWindowTransform transform;
    u32 sticky_transform_{};
    bool async{};
    s32 swap_interval{};
    Fence fence{};

    input.Deflate(&timestamp, &is_auto_timestamp, &crop, &scaling_mode, &transform, &sticky_transform_, &async, &swap_interval, &fence);

    switch (scaling_mode) {
    case NativeWindowScalingMode::Freeze:
    case NativeWindowScalingMode::ScaleToWindow:
    case NativeWindowScalingMode::ScaleCrop:
    case NativeWindowScalingMode::NoScaleCrop:
    case NativeWindowScalingMode::PreserveAspectRatio:
        break;
    default:
        LOG_ERROR(Service_Nvnflinger, "unknown scaling mode {}", scaling_mode);
        return Status::BadValue;
    }

    BufferItem item;
    std::shared_ptr<IConsumerListener> listener_available;
    std::shared_ptr<IConsumerListener> listener_replaced;
    bool recycled_queued_slot = false;

    {
        std::scoped_lock lock{core->mutex};

        if (core->is_abandoned) {
            LOG_ERROR(Service_Nvnflinger, "BufferQueue has been abandoned");
            return Status::NoInit;
        }

        const s32 max_buffer_count = core->GetMaxBufferCountLocked(async);

        if (slot < 0 || slot >= max_buffer_count) {
            LOG_ERROR(Service_Nvnflinger, "slot {} out of range [0, {})", slot, max_buffer_count);
            return Status::BadValue;
        }
        if (slots[slot].buffer_state != BufferState::Dequeued) {
            LOG_ERROR(Service_Nvnflinger, "slot {} is not owned by producer", slot);
            return Status::BadValue;
        }
        if (!slots[slot].request_buffer_called) {
            LOG_ERROR(Service_Nvnflinger, "slot {} was queued without request", slot);
            return Status::BadValue;
        }

        // History is normally disabled: avoid unconditional wall-clock work.
        const bool record_buffer_history = Settings::values.enable_buffer_history.GetValue();
        ++core->frame_counter;
        slots[slot].buffer_state = BufferState::Queued;
        slots[slot].frame_number = core->frame_counter;
        slots[slot].queue_time = timestamp;
        slots[slot].presentation_time = record_buffer_history
                                            ? Common::g_wall_clock.GetTimeNS().count() : 0;
        slots[slot].fence = fence;

        item.slot = slot;
        item.graphic_buffer = slots[slot].graphic_buffer;
        item.frame_number = core->frame_counter;
        item.timestamp = timestamp;
        item.is_auto_timestamp = is_auto_timestamp;
        item.crop = crop;
        item.transform = transform & ~NativeWindowTransform::InverseDisplay;
        item.transform_to_display_inverse = (transform & NativeWindowTransform::InverseDisplay) != NativeWindowTransform::None;
        item.scaling_mode = static_cast<u32>(scaling_mode);
        item.fence = fence;
        item.is_droppable = core->dequeue_buffer_cannot_block || async;
        item.swap_interval = swap_interval;
        item.acquire_called = slots[slot].acquire_called;

        sticky_transform = sticky_transform_;

        if (core->queue.empty()) {
            core->queue.push_back(item);
            listener_available = core->consumer_listener;
        } else {
            auto front = core->queue.begin();
            if (front->is_droppable && core->StillTracking(*front)) {
                slots[front->slot].buffer_state = BufferState::Free;
                recycled_queued_slot = true;
                if (record_buffer_history) {
                    core->UpdateHistory(front->frame_number, BufferState::Free);
                }
                slots[front->slot].frame_number = 0;
            }

            if (front->is_droppable) {
                *front = item;
                listener_replaced = core->consumer_listener;
            } else {
                core->queue.push_back(item);
                listener_available = core->consumer_listener;
            }
        }

        if (record_buffer_history) {
            core->PushHistory(core->frame_counter, slots[slot].queue_time, slots[slot].presentation_time, BufferState::Queued);
        }

        core->buffer_has_been_queued = true;
        core->SignalDequeueCondition();

        output->Inflate(core->default_width, core->default_height, core->transform_hint, static_cast<u32>(core->queue.size()));
    }

    if (recycled_queued_slot) {
        // KEvent::Signal takes the guest scheduler lock. Never acquire it
        // while holding the BufferQueue mutex: an unblocked guest may
        // reenter Binder/BufferQueue and reverse that lock order.
        buffer_wait_event->Signal(service_context.kernel);
    }

    // Notify consumers with the actual queued item: keep its buffer and slot
    // alive until the callback completes, outside the BufferQueue mutex.
    if (listener_available) {
        listener_available->OnFrameAvailable(item);
    } else if (listener_replaced) {
        listener_replaced->OnFrameReplaced(item);
    }

    return Status::NoError;
}

void BufferQueueProducer::CancelBuffer(s32 slot, const Fence& fence) {
    LOG_DEBUG(Service_Nvnflinger, "slot {}", slot);

    std::unique_lock lock{core->mutex};

    if (core->is_abandoned) {
        LOG_ERROR(Service_Nvnflinger, "BufferQueue has been abandoned");
        return;
    }

    if (slot < 0 || slot >= BufferQueueDefs::NUM_BUFFER_SLOTS) {
        LOG_ERROR(Service_Nvnflinger, "slot index {} out of range [0, {})", slot,
                  BufferQueueDefs::NUM_BUFFER_SLOTS);
        return;
    } else if (slots[slot].buffer_state != BufferState::Dequeued) {
        LOG_ERROR(Service_Nvnflinger, "slot {} is not owned by the producer (state = {})", slot,
                  slots[slot].buffer_state);
        return;
    }

    slots[slot].buffer_state = BufferState::Free;
    slots[slot].frame_number = 0;
    slots[slot].fence = fence;

    core->SignalDequeueCondition();
    lock.unlock();
    buffer_wait_event->Signal(service_context.kernel);
}

Status BufferQueueProducer::Query(NativeWindow what, s32* out_value) {
    std::scoped_lock lock{core->mutex};

    if (out_value == nullptr) {
        LOG_ERROR(Service_Nvnflinger, "outValue was nullptr");
        return Status::BadValue;
    }

    if (core->is_abandoned) {
        LOG_ERROR(Service_Nvnflinger, "BufferQueue has been abandoned");
        return Status::NoInit;
    }

    u32 value{};
    switch (what) {
    case NativeWindow::Width:
        value = core->default_width;
        break;
    case NativeWindow::Height:
        value = core->default_height;
        break;
    case NativeWindow::Format:
        value = static_cast<u32>(core->default_buffer_format);
        break;
    case NativeWindow::MinUndequeedBuffers:
        value = core->GetMinUndequeuedBufferCountLocked(false);
        break;
    case NativeWindow::StickyTransform:
        value = sticky_transform;
        break;
    case NativeWindow::ConsumerRunningBehind:
        value = (core->queue.size() > 1);
        break;
    case NativeWindow::ConsumerUsageBits:
        value = core->consumer_usage_bit;
        break;
    case NativeWindow::DefaultDataSpace:
        value = core->GetMaxBufferCountLocked(false);
        break;
    default:
        ASSERT(false);
        return Status::BadValue;
    }

    LOG_DEBUG(Service_Nvnflinger, "what = {}, value = {}", what, value);

    *out_value = static_cast<s32>(value);

    return Status::NoError;
}

Status BufferQueueProducer::Connect(const std::shared_ptr<IProducerListener>& listener,
                                    NativeWindowApi api, bool producer_controlled_by_app,
                                    QueueBufferOutput* output) {
    std::scoped_lock lock{core->mutex};

    LOG_DEBUG(Service_Nvnflinger, "api = {} producer_controlled_by_app = {}", api,
              producer_controlled_by_app);

    if (core->is_abandoned) {
        LOG_ERROR(Service_Nvnflinger, "BufferQueue has been abandoned");
        return Status::NoInit;
    }

    if (core->consumer_listener == nullptr) {
        LOG_ERROR(Service_Nvnflinger, "BufferQueue has no consumer");
        return Status::NoInit;
    }

    if (output == nullptr) {
        LOG_ERROR(Service_Nvnflinger, "output was nullptr");
        return Status::BadValue;
    }

    if (core->connected_api != NativeWindowApi::NoConnectedApi) {
        LOG_ERROR(Service_Nvnflinger, "already connected (cur = {} req = {})", core->connected_api,
                  api);
        return Status::BadValue;
    }

    Status status = Status::NoError;
    switch (api) {
    case NativeWindowApi::Egl:
    case NativeWindowApi::Cpu:
    case NativeWindowApi::Media:
    case NativeWindowApi::Camera:
        core->connected_api = api;
        output->Inflate(core->default_width, core->default_height, core->transform_hint,
                        static_cast<u32>(core->queue.size()));
        // HOS IPC connects with no guest listener. Always keep a local wake
        // callback so the kernel wait handle is signalled on consumer release.
        // Preserve an explicit listener by forwarding its notification.
        if (const auto wake = GetReleaseWake(this)) {
            wake->SetDelegate(listener);
            core->connected_producer_listener = wake;
        } else {
            core->connected_producer_listener = listener;
        }
        break;
    default:
        LOG_ERROR(Service_Nvnflinger, "unknown api = {}", api);
        status = Status::BadValue;
        break;
    }

    core->buffer_has_been_queued = false;
    core->dequeue_buffer_cannot_block =
        core->consumer_controlled_by_app && producer_controlled_by_app;

    return status;
}

// https://android.googlesource.com/platform/frameworks/native/%2B/master/libs/gui/BufferQueueProducer.cpp#1457
Status BufferQueueProducer::Disconnect(NativeWindowApi api) {
    LOG_DEBUG(Service_Nvnflinger, "disconnect api = {}", api);

    std::shared_ptr<IConsumerListener> listener;
    Status status = Status::NoError;
    bool released_queue = false;

    {
        std::scoped_lock lock{core->mutex};
        core->WaitWhileAllocatingLocked();

        if (core->is_abandoned) {
            return Status::NoError;
        }

        if (core->connected_api == NativeWindowApi::NoConnectedApi) {
            LOG_DEBUG(Service_Nvnflinger, "disconnect: not connected (req = {})", api);
            return Status::NoInit;
        }

        switch (api) {
        case NativeWindowApi::Egl:
        case NativeWindowApi::Cpu:
        case NativeWindowApi::Media:
        case NativeWindowApi::Camera:
            if (core->connected_api == api) {
                core->queue.clear();
                core->FreeAllBuffersLocked();
                core->connected_producer_listener = nullptr;
                core->connected_api = NativeWindowApi::NoConnectedApi;
                core->SignalDequeueCondition();
                released_queue = true;
                listener = core->consumer_listener;
            } else {
                LOG_ERROR(Service_Nvnflinger,
                          "disconnect: still connected to another api (cur = {} req = {})",
                          core->connected_api, api);
                status = Status::BadValue;
            }
            break;
        default:
            LOG_ERROR(Service_Nvnflinger, "disconnect: unknown api = {}", api);
            status = Status::BadValue;
            break;
        }
    }

    if (released_queue) {
        buffer_wait_event->Signal(service_context.kernel);
    }
    if (listener) {
        listener->OnBuffersReleased();
    }

    return status;
}

Status BufferQueueProducer::SetPreallocatedBuffer(s32 slot,
                                                  const std::shared_ptr<NvGraphicBuffer>& buffer) {
    LOG_DEBUG(Service_Nvnflinger, "slot {}", slot);

    if (slot < 0 || slot >= BufferQueueDefs::NUM_BUFFER_SLOTS) {
        return Status::BadValue;
    }

    std::unique_lock lock{core->mutex};

    slots[slot] = {};
    slots[slot].fence = Fence::NoFence();
    slots[slot].graphic_buffer = std::make_shared<GraphicBuffer>(nvmap, buffer);
    slots[slot].frame_number = 0;

    // Most games preallocate a buffer and pass a valid buffer here. However, it is possible for
    // this to be called with an empty buffer, Naruto Ultimate Ninja Storm is a game that does this.
    if (buffer) {
        slots[slot].is_preallocated = true;

        core->override_max_buffer_count = core->GetPreallocatedBufferCountLocked();
        core->default_width = buffer->Width();
        core->default_height = buffer->Height();
        core->default_buffer_format = buffer->Format();
    }

    core->SignalDequeueCondition();
    lock.unlock();
    buffer_wait_event->Signal(service_context.kernel);

    return Status::NoError;
}

Kernel::KReadableEvent* BufferQueueProducer::GetNativeHandle(u32 type_id) {
    return &buffer_wait_event->GetReadableEvent();
}

void BufferQueueProducer::Transact(u32 code, std::span<const u8> parcel_data,
                                   std::span<u8> parcel_reply, u32 flags) {
    // Values used by BnGraphicBufferProducer onTransact
    enum class TransactionId {
        RequestBuffer = 1,
        SetBufferCount = 2,
        DequeueBuffer = 3,
        DetachBuffer = 4,
        DetachNextBuffer = 5,
        AttachBuffer = 6,
        QueueBuffer = 7,
        CancelBuffer = 8,
        Query = 9,
        Connect = 10,
        Disconnect = 11,
        AllocateBuffers = 13,
        SetPreallocatedBuffer = 14,
        GetBufferHistory = 17,
    };

    Status status{Status::NoError};
    InputParcel parcel_in{parcel_data};
    OutputParcel parcel_out{};

    switch (static_cast<TransactionId>(code)) {
    case TransactionId::Connect: {
        const auto enable_listener = parcel_in.Read<bool>();
        const auto api = parcel_in.Read<NativeWindowApi>();
        const auto producer_controlled_by_app = parcel_in.Read<bool>();

        UNIMPLEMENTED_IF_MSG(enable_listener, "Listener is unimplemented!");

        std::shared_ptr<IProducerListener> listener;
        QueueBufferOutput output{};

        status = Connect(listener, api, producer_controlled_by_app, &output);

        parcel_out.Write(output);
        break;
    }
    case TransactionId::SetPreallocatedBuffer: {
        const auto slot = parcel_in.Read<s32>();
        const auto buffer = parcel_in.ReadObject<NvGraphicBuffer>();

        status = SetPreallocatedBuffer(slot, buffer);
        break;
    }
    case TransactionId::DequeueBuffer: {
        const auto is_async = parcel_in.Read<bool>();
        const auto width = parcel_in.Read<u32>();
        const auto height = parcel_in.Read<u32>();
        const auto pixel_format = parcel_in.Read<PixelFormat>();
        const auto usage = parcel_in.Read<u32>();

        s32 slot{};
        Fence fence{};

        status = DequeueBuffer(&slot, &fence, is_async, width, height, pixel_format, usage);

        parcel_out.Write(slot);
        parcel_out.WriteFlattenedObject(&fence);
        break;
    }
    case TransactionId::RequestBuffer: {
        const auto slot = parcel_in.Read<s32>();

        std::shared_ptr<GraphicBuffer> buf;

        status = RequestBuffer(slot, &buf);

        parcel_out.WriteFlattenedObject<NvGraphicBuffer>(buf.get());
        break;
    }
    case TransactionId::QueueBuffer: {
        const auto slot = parcel_in.Read<s32>();

        QueueBufferInput input{parcel_in};
        QueueBufferOutput output;

        status = QueueBuffer(slot, input, &output);

        parcel_out.Write(output);
        break;
    }
    case TransactionId::Query: {
        const auto what = parcel_in.Read<NativeWindow>();

        s32 value{};

        status = Query(what, &value);

        parcel_out.Write(value);
        break;
    }
    case TransactionId::CancelBuffer: {
        const auto slot = parcel_in.Read<s32>();
        const auto fence = parcel_in.ReadFlattened<Fence>();

        CancelBuffer(slot, fence);
        break;
    }
    case TransactionId::Disconnect: {
        const auto api = parcel_in.Read<NativeWindowApi>();

        status = Disconnect(api);
        break;
    }
    case TransactionId::DetachBuffer: {
        const auto slot = parcel_in.Read<s32>();

        status = DetachBuffer(slot);
        break;
    }
    case TransactionId::SetBufferCount: {
        const auto buffer_count = parcel_in.Read<s32>();

        status = SetBufferCount(buffer_count);
        break;
    }
    case TransactionId::GetBufferHistory: {
        if (!Settings::values.enable_buffer_history.GetValue()) {
            LOG_DEBUG(Service_Nvnflinger, "(STUBBED) called");
            break;
        }

        LOG_DEBUG(Service_Nvnflinger, "called, transaction=GetBufferHistory");

        const s32 request = parcel_in.Read<s32>();
        if (request <= 0) {
            parcel_out.Write(Status::BadValue);
            parcel_out.Write<s32>(0);
            break;
        }

        std::vector<BufferHistoryInfo> snapshot;

        {
            std::scoped_lock lk(core->buffer_history_mutex);
            for (auto& [frame, info] : core->buffer_history_map) {
                snapshot.push_back(info);
            }
        }

        std::sort(snapshot.begin(), snapshot.end(), [](auto& a, auto& b){
            return a.frame_number > b.frame_number;
        });

        const s32 limit = std::min(request, (s32)snapshot.size());
        parcel_out.Write(Status::NoError);
        parcel_out.Write<s32>(limit);
        for (s32 i = 0; i < limit; ++i) {
            parcel_out.Write(snapshot[i]);
        }
        break;
    }
    default:
        ASSERT_MSG(false, "Unimplemented TransactionId {}", code);
        break;
    }

    parcel_out.Write(status);

    const auto serialized = parcel_out.Serialize();
    std::memcpy(parcel_reply.data(), serialized.data(),
                (std::min)(parcel_reply.size(), serialized.size()));
}

} // namespace Service::android
