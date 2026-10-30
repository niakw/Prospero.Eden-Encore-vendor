#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <sys/types.h>

#include <orbis/VideoOut.h>
#include <orbis/libkernel.h>

#include "gnm_commandbuffer.h"
#include "gnm_drawcommandbuffer.h"
#include "gnmdriver.h"

#ifndef ORBIS_KERNEL_WC_GARLIC
#define ORBIS_KERNEL_WC_GARLIC 3
#endif

#ifndef ORBIS_KERNEL_PROT_CPU_READ
#define ORBIS_KERNEL_PROT_CPU_READ 0x01
#endif

#ifndef ORBIS_KERNEL_PROT_CPU_RW
#define ORBIS_KERNEL_PROT_CPU_RW 0x02
#endif

#ifndef ORBIS_KERNEL_PROT_GPU_READ
#define ORBIS_KERNEL_PROT_GPU_READ 0x10
#endif

#ifndef ORBIS_KERNEL_PROT_GPU_WRITE
#define ORBIS_KERNEL_PROT_GPU_WRITE 0x20
#endif

#ifndef ORBIS_VIDEO_OUT_ASPECT_RATIO_NONE
#define ORBIS_VIDEO_OUT_ASPECT_RATIO_NONE 0
#endif

#ifndef ORBIS_VIDEO_OUT_TILING_MODE_LINEAR
#define ORBIS_VIDEO_OUT_TILING_MODE_LINEAR 1
#endif

#ifndef ORBIS_VIDEO_OUT_FLIP_60HZ
#define ORBIS_VIDEO_OUT_FLIP_60HZ 0
#endif

enum {
    kDirectMemorySize = 2 * 1024 * 1024,
    kDirectMemoryAlignment = 2 * 1024 * 1024,
    kCommandDwords = 4096,
    kVideoBufferCount = 2,
    kVideoBytesPerPixel = 4,
    kVideoAlignment = 64 * 1024,
};

typedef struct {
    int handle;
    OrbisKernelEqueue flipqueue;
    off_t direct_memory;
    void* mapped;
    size_t mapped_size;
    uint8_t* buffers[kVideoBufferCount];
    uint32_t width;
    uint32_t height;
    uint32_t pitch_pixels;
    size_t buffer_stride;
    uint64_t frame;
} VideoStatus;

static VideoStatus* g_video_status = NULL;
static int g_status_code = -1;

static size_t align_up(size_t value, size_t alignment) {
    const size_t remainder = value % alignment;
    return remainder == 0 ? value : value + (alignment - remainder);
}

static uint32_t pack_a8b8g8r8(uint8_t r, uint8_t g, uint8_t b, uint8_t a) {
    return (uint32_t)r | ((uint32_t)g << 8) | ((uint32_t)b << 16) |
           ((uint32_t)a << 24);
}

static void draw_rect(VideoStatus* video, uint8_t* buffer, uint32_t x0, uint32_t y0,
                      uint32_t width, uint32_t height, uint32_t color) {
    const uint32_t x1 = x0 + width > video->width ? video->width : x0 + width;
    const uint32_t y1 = y0 + height > video->height ? video->height : y0 + height;
    for (uint32_t y = y0; y < y1; ++y) {
        uint32_t* row =
            (uint32_t*)(buffer + (size_t)y * video->pitch_pixels * kVideoBytesPerPixel);
        for (uint32_t x = x0; x < x1; ++x) {
            row[x] = color;
        }
    }
}

static void draw_digit(VideoStatus* video, uint8_t* buffer, int digit) {
    static const uint8_t segment_masks[10] = {
        0x3f, 0x06, 0x5b, 0x4f, 0x66, 0x6d, 0x7d, 0x07, 0x7f, 0x6f,
    };

    if (digit < 0 || digit > 9) {
        return;
    }

    const uint32_t color = pack_a8b8g8r8(0xff, 0xff, 0xff, 0xff);
    const uint32_t scale = video->height / 18;
    const uint32_t thickness = scale;
    const uint32_t digit_width = scale * 5;
    const uint32_t digit_height = scale * 9;
    const uint32_t x = (video->width - digit_width) / 2;
    const uint32_t y = (video->height - digit_height) / 2;
    const uint8_t mask = segment_masks[digit];

    if (mask & 0x01) {
        draw_rect(video, buffer, x + thickness, y, digit_width - 2 * thickness,
                  thickness, color);
    }
    if (mask & 0x02) {
        draw_rect(video, buffer, x + digit_width - thickness, y + thickness,
                  thickness, digit_height / 2 - thickness, color);
    }
    if (mask & 0x04) {
        draw_rect(video, buffer, x + digit_width - thickness, y + digit_height / 2,
                  thickness, digit_height / 2 - thickness, color);
    }
    if (mask & 0x08) {
        draw_rect(video, buffer, x + thickness, y + digit_height - thickness,
                  digit_width - 2 * thickness, thickness, color);
    }
    if (mask & 0x10) {
        draw_rect(video, buffer, x, y + digit_height / 2, thickness,
                  digit_height / 2 - thickness, color);
    }
    if (mask & 0x20) {
        draw_rect(video, buffer, x, y + thickness, thickness,
                  digit_height / 2 - thickness, color);
    }
    if (mask & 0x40) {
        draw_rect(video, buffer, x + thickness, y + digit_height / 2 - thickness / 2,
                  digit_width - 2 * thickness, thickness, color);
    }
}

static void fill_status(VideoStatus* video, uint32_t color) {
    const unsigned index = (unsigned)(video->frame % kVideoBufferCount);
    uint8_t* buffer = video->buffers[index];
    for (uint32_t y = 0; y < video->height; ++y) {
        uint32_t* row =
            (uint32_t*)(buffer + (size_t)y * video->pitch_pixels * kVideoBytesPerPixel);
        for (uint32_t x = 0; x < video->width; ++x) {
            row[x] = color;
        }
    }

    const uint32_t stripe = pack_a8b8g8r8(0xff, 0xff, 0xff, 0xff);
    const uint32_t stripe_height = video->height / 20;
    const uint32_t x_phase = (uint32_t)((video->frame * 24) % video->width);
    for (uint32_t y = 0; y < stripe_height; ++y) {
        uint32_t* row =
            (uint32_t*)(buffer + (size_t)y * video->pitch_pixels * kVideoBytesPerPixel);
        for (uint32_t x = 0; x < video->width; ++x) {
            if (((x + x_phase) / 48) & 1) {
                row[x] = stripe;
            }
        }
    }

    draw_digit(video, buffer, g_status_code);
}

static void present_status(VideoStatus* video, uint32_t color) {
    if (video == NULL || video->handle < 0 || video->buffers[0] == NULL) {
        return;
    }

    fill_status(video, color);
    const int index = (int)(video->frame % kVideoBufferCount);
    const int res = sceVideoOutSubmitFlip(
        video->handle, index, ORBIS_VIDEO_OUT_FLIP_VSYNC, (int64_t)video->frame
    );
    video->frame += 1;
    if (res != 0 || video->flipqueue == 0) {
        return;
    }

    OrbisKernelEvent event = {0};
    int out = 0;
    (void)sceKernelWaitEqueue(video->flipqueue, &event, 1, &out, 0);
}

static void log_step(const char* text) {
    printf("%s\n", text);
    sceKernelDebugOutText(0, text);
    sceKernelDebugOutText(0, "\n");
}

static int hold(int code) {
    char text[96];
    snprintf(text, sizeof(text), "opengnm hardware smoke holding with code %d", code);
    log_step(text);
    g_status_code = code;
    const uint32_t color =
        code == 0 ? pack_a8b8g8r8(0x20, 0xc8, 0x30, 0xff) :
                    pack_a8b8g8r8(0xc8, 0x20, 0x20, 0xff);
    for (;;) {
        present_status(g_video_status, color);
        for (volatile uint32_t spin = 0; spin < 100000000; ++spin) {
        }
    }
}

static bool allocate_video_buffers(VideoStatus* video) {
    const size_t buffer_size =
        (size_t)video->pitch_pixels * video->height * kVideoBytesPerPixel;
    video->buffer_stride = align_up(buffer_size, kVideoAlignment);
    video->mapped_size = video->buffer_stride * kVideoBufferCount;

    int res = sceKernelAllocateDirectMemory(0, (off_t)sceKernelGetDirectMemorySize(),
                                            video->mapped_size, kVideoAlignment,
                                            ORBIS_KERNEL_WC_GARLIC, &video->direct_memory);
    if (res != 0) {
        printf("video sceKernelAllocateDirectMemory failed: 0x%x\n", res);
        return false;
    }

    const int protection = ORBIS_KERNEL_PROT_CPU_READ | ORBIS_KERNEL_PROT_CPU_RW |
                           ORBIS_KERNEL_PROT_GPU_READ;
    res = sceKernelMapDirectMemory(&video->mapped, video->mapped_size, protection, 0,
                                   video->direct_memory, kVideoAlignment);
    if (res != 0) {
        printf("video sceKernelMapDirectMemory failed: 0x%x\n", res);
        return false;
    }

    for (unsigned i = 0; i < kVideoBufferCount; ++i) {
        video->buffers[i] = (uint8_t*)video->mapped + i * video->buffer_stride;
    }

    return true;
}

static bool init_video(VideoStatus* video) {
    video->handle = -1;
    video->direct_memory = -1;

    video->handle = sceVideoOutOpen(0, ORBIS_VIDEO_OUT_BUS_MAIN, 0, NULL);
    if (video->handle < 0) {
        printf("sceVideoOutOpen failed: 0x%x\n", video->handle);
        return false;
    }

    OrbisVideoOutResolutionStatus status = {0};
    int res = sceVideoOutGetResolutionStatus(video->handle, &status);
    if (res != 0) {
        printf("sceVideoOutGetResolutionStatus failed: 0x%x\n", res);
        return false;
    }

    video->width = status.width ? status.width : 1920;
    video->height = status.height ? status.height : 1080;
    video->pitch_pixels = video->width;

    if (!allocate_video_buffers(video)) {
        return false;
    }

    OrbisVideoOutBufferAttribute attr = {0};
    sceVideoOutSetBufferAttribute(&attr, ORBIS_VIDEO_OUT_PIXEL_FORMAT_A8B8G8R8_SRGB,
                                  ORBIS_VIDEO_OUT_TILING_MODE_LINEAR,
                                  ORBIS_VIDEO_OUT_ASPECT_RATIO_NONE, video->width,
                                  video->height, video->pitch_pixels);

    void* addresses[kVideoBufferCount] = {video->buffers[0], video->buffers[1]};
    res = sceVideoOutRegisterBuffers(video->handle, 0, addresses, kVideoBufferCount, &attr);
    if (res < 0) {
        printf("sceVideoOutRegisterBuffers failed: 0x%x\n", res);
        return false;
    }

    res = sceKernelCreateEqueue(&video->flipqueue, "opengnm smoke flips");
    if (res != 0) {
        printf("sceKernelCreateEqueue failed: 0x%x\n", res);
        return false;
    }

    res = sceVideoOutAddFlipEvent(video->flipqueue, video->handle, NULL);
    if (res != 0) {
        printf("sceVideoOutAddFlipEvent failed: 0x%x\n", res);
        return false;
    }

    sceVideoOutSetFlipRate(video->handle, ORBIS_VIDEO_OUT_FLIP_60HZ);
    return true;
}

static int allocate_garlic(void** out_addr, off_t* out_direct_memory) {
    log_step("opengnm smoke: allocate garlic direct memory");
    int res = sceKernelAllocateDirectMemory(0, (off_t)sceKernelGetDirectMemorySize(),
                                            kDirectMemorySize,
                                            kDirectMemoryAlignment,
                                            ORBIS_KERNEL_WC_GARLIC, out_direct_memory);
    if (res < 0) {
        printf("sceKernelAllocateDirectMemory failed: 0x%x\n", res);
        return res;
    }

    const int protection = ORBIS_KERNEL_PROT_CPU_READ | ORBIS_KERNEL_PROT_CPU_RW |
                           ORBIS_KERNEL_PROT_GPU_READ | ORBIS_KERNEL_PROT_GPU_WRITE;
    log_step("opengnm smoke: map garlic direct memory");
    res = sceKernelMapDirectMemory(out_addr, kDirectMemorySize, protection, 0,
                                   *out_direct_memory, kDirectMemoryAlignment);
    if (res < 0) {
        printf("sceKernelMapDirectMemory failed: 0x%x\n", res);
        return -1000 + res;
    }

    return 0;
}

int main(void) {
    log_step("opengnm hardware smoke started");

    VideoStatus video = {0};
    if (init_video(&video)) {
        g_video_status = &video;
        present_status(&video, pack_a8b8g8r8(0x20, 0x60, 0xc8, 0xff));
    } else {
        log_step("opengnm smoke: VideoOut init failed");
    }

    void* mapped = NULL;
    off_t direct_memory = 0;
    int res = allocate_garlic(&mapped, &direct_memory);
    if (res < 0) {
        return hold(res <= -1000 ? 5 : 1);
    }

    log_step("opengnm smoke: initialize command buffer");
    present_status(g_video_status, pack_a8b8g8r8(0x20, 0xb0, 0xc8, 0xff));
    volatile uint64_t* eop_label =
        (volatile uint64_t*)((uint8_t*)mapped + kDirectMemorySize - sizeof(uint64_t));
    *eop_label = 0;

    GnmCommandBuffer cmd =
        sceGnmCmdInit(mapped, kCommandDwords, NULL, NULL);
    log_step("opengnm smoke: emit default state");
    sceGnmDrawCmdInitDefaultHardwareState(&cmd);
    log_step("opengnm smoke: emit zero-count draw");
    sceGnmDrawCmdDrawIndexAuto(&cmd, 0);
    log_step("opengnm smoke: emit EOP write");
    sceGnmDrawCmdEventWriteEop(&cmd, GNM_CACHE_FLUSH_AND_INV_TS_EVENT,
                               (uint64_t)(uintptr_t)eop_label,
                               GNM_DATA_SEL_SEND_DATA64, 0x4f50474e534d4b45ULL);

    const uint32_t dcb_size = (uint32_t)((uintptr_t)cmd.cmdptr - (uintptr_t)cmd.beginptr);
    void* dcb_addrs[1] = {cmd.beginptr};
    uint32_t dcb_sizes[1] = {dcb_size};

    log_step("opengnm smoke: submit command buffer");
    present_status(g_video_status, pack_a8b8g8r8(0xd8, 0xb8, 0x20, 0xff));
    res = sceGnmSubmitCommandBuffers(1, dcb_addrs, dcb_sizes, NULL, NULL);
    if (res < 0) {
        printf("sceGnmSubmitCommandBuffers failed: 0x%x\n", res);
        return hold(2);
    }

    log_step("opengnm smoke: submit done");
    res = sceGnmSubmitDone();
    if (res < 0) {
        printf("sceGnmSubmitDone failed: 0x%x\n", res);
        return hold(3);
    }

    log_step("opengnm smoke: wait for EOP label");
    for (uint32_t spin = 0; spin < 100000000; ++spin) {
        if (*eop_label == 0x4f50474e534d4b45ULL) {
            printf("opengnm hardware smoke passed: dcb_size=%u\n", dcb_size);
            return hold(0);
        }
    }

    printf("EOP label was not written; last value=0x%llx\n",
           (unsigned long long)*eop_label);
    return hold(4);
}
