// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

// SPDX-FileCopyrightText: Copyright 2024 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <algorithm>

#include "core/core.h"
#include "core/hle/service/nvdrv/devices/nvdisp_disp0.h"
#include "core/hle/service/nvdrv/nvdrv_interface.h"
#include "core/hle/service/nvnflinger/display.h"
#include "core/hle/service/nvnflinger/hos_binder_driver_server.h"
#include "core/hle/service/nvnflinger/surface_flinger.h"
#include "core/hle/service/sm/sm.h"

#include "core/hle/service/nvnflinger/buffer_queue_consumer.h"
#include "core/hle/service/nvnflinger/buffer_queue_core.h"
#include "core/hle/service/nvnflinger/buffer_queue_producer.h"

namespace Service::Nvnflinger {

SurfaceFlinger::SurfaceFlinger(Core::System& system, HosBinderDriverServer& server)
    : m_system(system), m_server(server), m_context(m_system, "SurfaceFlinger") {
    nvdrv = m_system.ServiceManager().GetService<Nvidia::NVDRV>("nvdrv:s", true)->GetModule();
    disp_fd = nvdrv->Open("/dev/nvdisp_disp0", {});
}

SurfaceFlinger::~SurfaceFlinger() {
    nvdrv->Close(disp_fd);
}

void SurfaceFlinger::AddDisplay(u64 display_id) {
    // A display ID must identify exactly one stack and buffer owner.
    if (FindDisplay(display_id) != nullptr) {
        return;
    }
    m_displays.emplace_back(display_id);
}

void SurfaceFlinger::RemoveDisplay(u64 display_id) {
    // Closing a display must release the previously acquired BufferQueue slots.
    // A bare erase leaves the compositor holding them forever. Wait for the
    // prior GPU composite first: freeing a live image would corrupt its fence.
    for (auto& display : m_displays) {
        if (display.id != display_id) {
            continue;
        }
        if (!display.stack.layers.empty()) {
            nvdrv->GetDevice<Nvidia::Devices::nvdisp_disp0>(disp_fd)->WaitForComposite();
            for (const auto& layer : display.stack.layers) {
                m_composer.RemoveLayerLocked(display, layer->consumer_id);
            }
            display.stack.layers.clear();
        }
        break;
    }
    std::erase_if(m_displays, [&](auto& display) { return display.id == display_id; });
}

bool SurfaceFlinger::ComposeDisplay(s32* out_swap_interval, f32* out_compose_speed_scale,
                                    u64 display_id) {
    auto* const display = this->FindDisplay(display_id);
    if (!display || !display->stack.HasLayers()) {
        return false;
    }

    *out_swap_interval =
        m_composer.ComposeLocked(out_compose_speed_scale, *display,
                                 *nvdrv->GetDevice<Nvidia::Devices::nvdisp_disp0>(disp_fd));
    return true;
}

void SurfaceFlinger::CreateLayer(s32 consumer_binder_id) {
    auto binder = std::static_pointer_cast<android::BufferQueueConsumer>(
        m_server.TryGetBinder(consumer_binder_id));
    if (!binder) {
        return;
    }

    auto buffer_item_consumer = std::make_shared<android::BufferItemConsumer>(std::move(binder));
    buffer_item_consumer->Connect(false);

    m_layers.layers.emplace_back(
        std::make_shared<Layer>(std::move(buffer_item_consumer), consumer_binder_id));
}

void SurfaceFlinger::DestroyLayer(s32 consumer_binder_id) {
    // Container::OnTerminate destroys layers without CloseLayerLocked.
    // Remove any such still-stacked layer before dropping the master owner;
    // otherwise a display retains the shared_ptr AND an acquired image slot.
    for (auto& display : m_displays) {
        const auto attached = std::find_if(display.stack.layers.begin(),
                                           display.stack.layers.end(),
                                           [&](const auto& layer) {
                                               return layer->consumer_id == consumer_binder_id;
                                           });
        if (attached != display.stack.layers.end()) {
            RemoveLayerFromDisplayStack(display.id, consumer_binder_id);
        }
    }
    std::erase_if(m_layers.layers,
                  [&](auto& layer) { return layer->consumer_id == consumer_binder_id; });
}

void SurfaceFlinger::AddLayerToDisplayStack(u64 display_id, s32 consumer_binder_id) {
    auto* const display = this->FindDisplay(display_id);
    auto layer = this->FindLayer(consumer_binder_id);

    if (!display || !layer) {
        return;
    }

    // A layer is identified by its consumer binder within a display stack.
    // Duplicate insertion would compose the same acquired BufferItem twice on
    // each refresh and could needlessly retain an otherwise released slot.
    if (std::any_of(display->stack.layers.begin(), display->stack.layers.end(),
                    [consumer_binder_id](const auto& attached) {
                        return attached->consumer_id == consumer_binder_id;
                    })) {
        return;
    }

    display->stack.layers.emplace_back(std::move(layer));
}

void SurfaceFlinger::RemoveLayerFromDisplayStack(u64 display_id, s32 consumer_binder_id) {
    auto* const display = this->FindDisplay(display_id);
    if (!display) {
        return;
    }

    const auto attached = std::find_if(display->stack.layers.begin(),
                                       display->stack.layers.end(),
                                       [&](const auto& layer) {
                                           return layer->consumer_id == consumer_binder_id;
                                       });
    if (attached == display->stack.layers.end()) {
        return;
    }
    // Releasing an acquired buffer before the pending GPU composite completes
    // can reuse GPU memory still being read by Vulkan/RADV.
    nvdrv->GetDevice<Nvidia::Devices::nvdisp_disp0>(disp_fd)->WaitForComposite();
    m_composer.RemoveLayerLocked(*display, consumer_binder_id);
    std::erase_if(display->stack.layers,
                  [&](auto& layer) { return layer->consumer_id == consumer_binder_id; });
}

void SurfaceFlinger::SetLayerVisibility(s32 consumer_binder_id, bool visible) {
    if (const auto layer = this->FindLayer(consumer_binder_id); layer != nullptr) {
        layer->visible = visible;
        return;
    }
}

void SurfaceFlinger::SetLayerBlending(s32 consumer_binder_id, LayerBlending blending) {
    if (const auto layer = this->FindLayer(consumer_binder_id); layer != nullptr) {
        layer->blending = blending;
        return;
    }
}

void SurfaceFlinger::SetLayerStackMask(s32 consumer_binder_id, u32 layer_stack_mask) {
    if (const auto layer = this->FindLayer(consumer_binder_id); layer != nullptr) {
        layer->layer_stack_mask = layer_stack_mask;
        LOG_DEBUG(Service_VI, "Layer {} stack mask set to {:#x}", consumer_binder_id,
                  layer_stack_mask);
    }
}

void SurfaceFlinger::SetLayerIsOverlay(s32 consumer_binder_id, bool is_overlay) {
    if (const auto layer = this->FindLayer(consumer_binder_id); layer != nullptr) {
        layer->is_overlay = is_overlay;
        LOG_DEBUG(Service_VI, "Layer {} marked as overlay: {}", consumer_binder_id, is_overlay);
    }
}

Display* SurfaceFlinger::FindDisplay(u64 display_id) {
    for (auto& display : m_displays) {
        if (display.id == display_id) {
            return &display;
        }
    }

    return nullptr;
}

std::shared_ptr<Layer> SurfaceFlinger::FindLayer(s32 consumer_binder_id) {
    for (auto& layer : m_layers.layers) {
        if (layer->consumer_id == consumer_binder_id) {
            return layer;
        }
    }

    return nullptr;
}

void SurfaceFlinger::CreateBufferQueue(s32* out_consumer_binder_id, s32* out_producer_binder_id) {
    auto& nvmap = nvdrv->GetContainer().GetNvMapFile();
    auto core = std::make_shared<android::BufferQueueCore>();
    auto producer = std::make_shared<android::BufferQueueProducer>(m_context, core, nvmap);
    auto consumer = std::make_shared<android::BufferQueueConsumer>(core);

    *out_consumer_binder_id = m_server.RegisterBinder(std::move(consumer));
    *out_producer_binder_id = m_server.RegisterBinder(std::move(producer));
}

void SurfaceFlinger::DestroyBufferQueue(s32 consumer_binder_id, s32 producer_binder_id) {
    m_server.UnregisterBinder(producer_binder_id);
    m_server.UnregisterBinder(consumer_binder_id);
}

} // namespace Service::Nvnflinger
