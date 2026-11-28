// SPDX-License-Identifier: GPL-3.0-or-later
// Adapted from ProsperoGallery src/jxr.c, commit ff728c83746d045fc3611e511ac2acb4a2b43e38.
// The decoder's pixel-format/HDR transfer tables are copyright their upstream authors.
// JPEG XR third-party codec is separately stored under sources/jxrlib (BSD license).
// Called ONLY after the user selects one validated wallpaper, never during enumeration.
#include "jxr_decoder.h"

#include <fcntl.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#ifndef __ANSI__
#define __ANSI__
#endif
#include <JXRGlue.h>
#include <webp/decode.h>
#include <webp/encode.h>

// --- Reading pixels ----------------------------------------------------------
typedef enum { KIND_HDR_LINEAR, KIND_HDR_PQ, KIND_SDR } Kind;

typedef enum {
    F_RGBA_HALF, F_RGB_HALF_PADDED, F_RGB_HALF,        // 16-bit floats
    F_RGBA_FLOAT, F_RGB_FLOAT_PADDED, F_RGB_FLOAT,     // 32-bit floats
    F_RGBA_FIXED16, F_RGB_FIXED16_PADDED, F_RGB_FIXED16,
    F_RGBA_FIXED32, F_RGB_FIXED32_PADDED, F_RGB_FIXED32,
    F_RGB101010,                                       // 10 bits per channel, HDR10
    F_RGB24, F_BGR24, F_BGR32, F_BGRA32, F_RGBA32,     // plain 8-bit
    F_RGB48, F_RGBA64,                                 // plain 16-bit
} Format;

typedef struct {
    const GUID *guid;
    Format format;
    int bytes;   // per pixel
    Kind kind;
} FormatInfo;

static const FormatInfo FORMATS[] = {
    { &GUID_PKPixelFormat64bppRGBAHalf,         F_RGBA_HALF,          8,  KIND_HDR_LINEAR },
    { &GUID_PKPixelFormat64bppRGBHalf,          F_RGB_HALF_PADDED,    8,  KIND_HDR_LINEAR },
    { &GUID_PKPixelFormat48bppRGBHalf,          F_RGB_HALF,           6,  KIND_HDR_LINEAR },
    { &GUID_PKPixelFormat128bppRGBAFloat,       F_RGBA_FLOAT,         16, KIND_HDR_LINEAR },
    { &GUID_PKPixelFormat128bppRGBFloat,        F_RGB_FLOAT_PADDED,   16, KIND_HDR_LINEAR },
    { &GUID_PKPixelFormat96bppRGBFloat,         F_RGB_FLOAT,          12, KIND_HDR_LINEAR },
    { &GUID_PKPixelFormat64bppRGBAFixedPoint,   F_RGBA_FIXED16,       8,  KIND_HDR_LINEAR },
    { &GUID_PKPixelFormat64bppRGBFixedPoint,    F_RGB_FIXED16_PADDED, 8,  KIND_HDR_LINEAR },
    { &GUID_PKPixelFormat48bppRGBFixedPoint,    F_RGB_FIXED16,        6,  KIND_HDR_LINEAR },
    { &GUID_PKPixelFormat128bppRGBAFixedPoint,  F_RGBA_FIXED32,       16, KIND_HDR_LINEAR },
    { &GUID_PKPixelFormat128bppRGBFixedPoint,   F_RGB_FIXED32_PADDED, 16, KIND_HDR_LINEAR },
    { &GUID_PKPixelFormat96bppRGBFixedPoint,    F_RGB_FIXED32,        12, KIND_HDR_LINEAR },
    { &GUID_PKPixelFormat32bppRGB101010,        F_RGB101010,          4,  KIND_HDR_PQ },
    { &GUID_PKPixelFormat24bppRGB,              F_RGB24,              3,  KIND_SDR },
    { &GUID_PKPixelFormat24bppBGR,              F_BGR24,              3,  KIND_SDR },
    { &GUID_PKPixelFormat32bppBGR,              F_BGR32,              4,  KIND_SDR },
    { &GUID_PKPixelFormat32bppBGRA,             F_BGRA32,             4,  KIND_SDR },
    { &GUID_PKPixelFormat32bppRGBA,             F_RGBA32,             4,  KIND_SDR },
    { &GUID_PKPixelFormat48bppRGB,              F_RGB48,              6,  KIND_SDR },
    { &GUID_PKPixelFormat64bppRGBA,             F_RGBA64,             8,  KIND_SDR },
};

static float half_to_float(uint16_t h)
{
    uint32_t sign = (uint32_t)(h >> 15) << 31;
    uint32_t exp = (h >> 10) & 0x1f;
    uint32_t mant = h & 0x3ff;
    uint32_t bits;

    if (exp == 0) {
        if (mant == 0) {
            bits = sign;
        } else {                       // subnormal: normalize it
            exp = 113;
            while (!(mant & 0x400)) { mant <<= 1; exp--; }
            bits = sign | (exp << 23) | ((mant & 0x3ff) << 13);
        }
    } else if (exp == 31) {
        bits = sign | 0x7f800000u | (mant << 13);
    } else {
        bits = sign | ((exp + 112) << 23) | (mant << 13);
    }
    float f;
    memcpy(&f, &bits, sizeof(f));
    return f;
}

// HDR10 "PQ" curve: turns a 0..1 signal into brightness in nits.
static float pq_to_nits(float e)
{
    const float m1 = 0.1593017578125f, m2 = 78.84375f;
    const float c1 = 0.8359375f, c2 = 18.8515625f, c3 = 18.6875f;
    float p = powf(e, 1.0f / m2);
    float num = p - c1 > 0 ? p - c1 : 0;
    return 10000.0f * powf(num / (c2 - c3 * p), 1.0f / m1);
}

static void read_pixel(const uint8_t *p, Format f, float rgb[3])
{
    const uint16_t *u16 = (const uint16_t *)p;
    const int16_t *i16 = (const int16_t *)p;
    const int32_t *i32 = (const int32_t *)p;
    const float *f32 = (const float *)p;

    switch (f) {
    case F_RGBA_HALF: case F_RGB_HALF_PADDED: case F_RGB_HALF:
        for (int c = 0; c < 3; c++) rgb[c] = half_to_float(u16[c]);
        break;
    case F_RGBA_FLOAT: case F_RGB_FLOAT_PADDED: case F_RGB_FLOAT:
        for (int c = 0; c < 3; c++) rgb[c] = f32[c];
        break;
    case F_RGBA_FIXED16: case F_RGB_FIXED16_PADDED: case F_RGB_FIXED16:
        for (int c = 0; c < 3; c++) rgb[c] = i16[c] / 8192.0f;        // s2.13
        break;
    case F_RGBA_FIXED32: case F_RGB_FIXED32_PADDED: case F_RGB_FIXED32:
        for (int c = 0; c < 3; c++) rgb[c] = i32[c] / 16777216.0f;    // s7.24
        break;
    case F_RGB101010: {
        uint32_t v;
        memcpy(&v, p, 4);
        // PQ-encoded BT.2020 -> linear BT.709 (the colors normal screens use).
        float r = pq_to_nits(((v >> 20) & 0x3ff) / 1023.0f) / 80.0f;
        float g = pq_to_nits(((v >> 10) & 0x3ff) / 1023.0f) / 80.0f;
        float b = pq_to_nits((v & 0x3ff) / 1023.0f) / 80.0f;
        rgb[0] =  1.6605f * r - 0.5876f * g - 0.0728f * b;
        rgb[1] = -0.1246f * r + 1.1329f * g - 0.0083f * b;
        rgb[2] = -0.0182f * r - 0.1006f * g + 1.1187f * b;
        break;
    }
    case F_RGB24: case F_RGBA32:
        for (int c = 0; c < 3; c++) rgb[c] = p[c] / 255.0f;
        break;
    case F_BGR24: case F_BGR32: case F_BGRA32:
        for (int c = 0; c < 3; c++) rgb[c] = p[2 - c] / 255.0f;
        break;
    case F_RGB48: case F_RGBA64:
        for (int c = 0; c < 3; c++) rgb[c] = u16[c] / 65535.0f;
        break;
    }
}

// --- Tone mapping ------------------------------------------------------------

static float luminance(const float rgb[3])
{
    return 0.2126f * rgb[0] + 0.7152f * rgb[1] + 0.0722f * rgb[2];
}

// Linear light -> the sRGB curve screens expect, as a 0..255 byte.
static uint8_t to_srgb_byte(float v)
{
    if (!(v > 0)) return 0;          // also catches NaN
    if (v >= 1) return 255;
    float s = v <= 0.0031308f ? v * 12.92f : 1.055f * powf(v, 1.0f / 2.4f) - 0.055f;
    return (uint8_t)(s * 255.0f + 0.5f);
}

static uint8_t to_byte(float v)
{
    if (!(v > 0)) return 0;
    if (v >= 1) return 255;
    return (uint8_t)(v * 255.0f + 0.5f);
}

#define HIST_BINS 1024
#define HIST_MIN_LOG2 (-12.0f)
#define HIST_MAX_LOG2 (8.0f)

// Brightness of the brightest 0.1% of the image (ignoring stray hot pixels),
// used as the point that becomes pure white.
static float bright_point(const uint8_t *pixels, size_t count, const FormatInfo *fi)
{
    static const float range = HIST_MAX_LOG2 - HIST_MIN_LOG2;
    uint32_t *hist = calloc(HIST_BINS, sizeof(uint32_t));
    if (hist == NULL) return 1.0f;

    size_t counted = 0;
    for (size_t i = 0; i < count; i++) {
        float rgb[3];
        read_pixel(pixels + i * fi->bytes, fi->format, rgb);
        float l = luminance(rgb);
        if (!(l > 0)) continue;
        int bin = (int)((log2f(l) - HIST_MIN_LOG2) / range * HIST_BINS);
        if (bin < 0) bin = 0;
        if (bin >= HIST_BINS) bin = HIST_BINS - 1;
        hist[bin]++;
        counted++;
    }

    float result = 1.0f;
    size_t target = counted - counted / 1000, seen = 0;
    for (int bin = 0; bin < HIST_BINS && counted > 0; bin++) {
        seen += hist[bin];
        if (seen >= target) {
            result = exp2f(HIST_MIN_LOG2 + (bin + 1) * range / HIST_BINS);
            break;
        }
    }
    free(hist);
    return result < 1.0f ? 1.0f : result;   // never brighten an image that fits already
}

// --- Decoding ----------------------------------------------------------------

bool encore_jxr_to_webp(const char *src, const char *dst)
{
    PKCodecFactory *factory = NULL;
    PKImageDecode *decoder = NULL;
    uint8_t *pixels = NULL;
    uint8_t *resized = NULL;
    uint8_t *compressed = NULL;
    bool ok = false;
    struct stat st = {0};
    // Fail closed on a symlink, oversized or missing screenshot.
    if (!src || !dst || lstat(src, &st) != 0 ||
        !S_ISREG(st.st_mode) || st.st_size < 32 ||
        st.st_size > 48 * 1024 * 1024)
        return false;

    if (PKCreateCodecFactory(&factory, WMP_SDK_VERSION) != WMP_errSuccess) return false;
    if (factory->CreateDecoderFromFile(src, &decoder) != WMP_errSuccess) goto done;

    const FormatInfo *fi = NULL;
    for (size_t i = 0; i < sizeof(FORMATS) / sizeof(FORMATS[0]); i++) {
        if (IsEqualGUID(FORMATS[i].guid, &decoder->guidPixFormat)) {
            fi = &FORMATS[i];
            break;
        }
    }
    if (fi == NULL) {
        fprintf(stderr, "EDEN_JXR_SELECT status=unsupported_pixel_format\n");
        goto done;
    }

    // Decoder setup, as in jxrlib's own JxrDecApp: full image, no alpha.
    PKPixelInfo info;
    info.pGUIDPixFmt = &decoder->guidPixFormat;
    if (PixelFormatLookup(&info, LOOKUP_FORWARD) != WMP_errSuccess) goto done;
    decoder->WMP.wmiSCP.uAlphaMode = 0;
    decoder->WMP.wmiI.cfColorFormat = info.cfColorFormat;
    decoder->WMP.wmiI.bdBitDepth = info.bdBitDepth;
    decoder->WMP.wmiI.cBitsPerUnit = info.cbitUnit;
    decoder->WMP.wmiI.cThumbnailWidth = decoder->WMP.wmiI.cWidth;
    decoder->WMP.wmiI.cThumbnailHeight = decoder->WMP.wmiI.cHeight;
    decoder->WMP.wmiI.bSkipFlexbits = FALSE;
    decoder->WMP.wmiI.cROILeftX = 0;
    decoder->WMP.wmiI.cROITopY = 0;
    decoder->WMP.wmiI.cROIWidth = decoder->WMP.wmiI.cWidth;
    decoder->WMP.wmiI.cROIHeight = decoder->WMP.wmiI.cHeight;
    decoder->WMP.wmiI.oOrientation = O_NONE;
    decoder->WMP.wmiI.cPostProcStrength = 0;

    I32 width, height;
    if (decoder->GetSize(decoder, &width, &height) != WMP_errSuccess)
        goto done;
    // 4K PS5 captures are expected, but arbitrary image dimensions are not.
    if (width <= 0 || height <= 0 || width > 3840 || height > 2160 ||
        (size_t)width * (size_t)height > 3840u * 2160u ||
        (size_t)width > (size_t)UINT32_MAX / (size_t)fi->bytes)
        goto done;

    size_t count = (size_t)width * (size_t)height;
    if (count > SIZE_MAX / (size_t)fi->bytes) goto done;
    pixels = malloc(count * (size_t)fi->bytes);
    if (pixels == NULL) goto done;

    PKRect rect = { 0, 0, width, height };
    if (decoder->Copy(decoder, &rect, pixels, (U32)((size_t)width * fi->bytes)) != WMP_errSuccess) goto done;

    // Brightest point -> white. Extended Reinhard: keeps dark and mid tones
    // close to how they were and rolls the highlights off smoothly.
    float white = fi->kind == KIND_SDR ? 1.0f : bright_point(pixels, count, fi);
    float white_sq = white * white;

    // Write the 8-bit result over the decoded pixels: every format is at
    // least 3 bytes per pixel, so the output never overtakes the input.
    for (size_t i = 0; i < count; i++) {
        float rgb[3];
        read_pixel(pixels + i * fi->bytes, fi->format, rgb);
        uint8_t *out = pixels + i * 3;

        if (fi->kind == KIND_SDR) {
            for (int c = 0; c < 3; c++) out[c] = to_byte(rgb[c]);
            continue;
        }
        for (int c = 0; c < 3; c++) if (!(rgb[c] > 0)) rgb[c] = 0;  // out-of-gamut / NaN
        float l = luminance(rgb);
        float scale = l > 0 ? (1.0f + l / white_sq) / (1.0f + l) : 0;
        for (int c = 0; c < 3; c++) out[c] = to_srgb_byte(rgb[c] * scale);
    }

    // Decode only the SELECTED image. Reduce full 4K captures to 1080p:
    // this avoids uploading a huge wallpaper texture to the PS5 launcher.
    int target_w = width, target_h = height;
    if (width > 1920 || height > 1080) {
        const float scale = fminf(1920.0f / (float)width,
                                  1080.0f / (float)height);
        target_w = (int)fmaxf(1.0f, floorf((float)width * scale));
        target_h = (int)fmaxf(1.0f, floorf((float)height * scale));
        const size_t out_bytes = (size_t)target_w * (size_t)target_h * 3u;
        resized = (uint8_t *)malloc(out_bytes);
        if (resized == NULL) goto done;
        // Bilinear filtering is bounded and performed only once per chosen
        // file on the background conversion worker, never in the UI draw loop.
        for (int y = 0; y < target_h; ++y) {
            const float pos_y = ((y + 0.5f) * height / target_h) - 0.5f;
            const int y0 = (int)fmaxf(0.0f, floorf(pos_y));
            const int y1 = y0 + 1 < height ? y0 + 1 : height - 1;
            const float fy = fmaxf(0.0f, pos_y - y0);
            for (int x = 0; x < target_w; ++x) {
                const float pos_x = ((x + 0.5f) * width / target_w) - 0.5f;
                const int x0 = (int)fmaxf(0.0f, floorf(pos_x));
                const int x1 = x0 + 1 < width ? x0 + 1 : width - 1;
                const float fx = fmaxf(0.0f, pos_x - x0);
                for (int c = 0; c < 3; ++c) {
                    const float a = pixels[((size_t)y0 * width + x0) * 3u + c];
                    const float b = pixels[((size_t)y0 * width + x1) * 3u + c];
                    const float d = pixels[((size_t)y1 * width + x0) * 3u + c];
                    const float e = pixels[((size_t)y1 * width + x1) * 3u + c];
                    const float top = a + (b - a) * fx;
                    const float bottom = d + (e - d) * fx;
                    resized[((size_t)y * target_w + x) * 3u + c] =
                        (uint8_t)fminf(255.0f, fmaxf(0.0f,
                        top + (bottom - top) * fy + 0.5f));
                }
            }
        }
    }
    const uint8_t *output = resized ? resized : pixels;
    const size_t length = WebPEncodeRGB(output, target_w, target_h,
                                        target_w * 3, 85.0f, &compressed);
    if (length < 32 || length > (12u * 1024u * 1024u) || !compressed)
        goto done;
    int checked_w = 0, checked_h = 0;
    if (!WebPGetInfo(compressed, length, &checked_w, &checked_h) ||
        checked_w != target_w || checked_h != target_h)
        goto done;
    // The caller supplies a private temporary path. Never follow a link or
    // truncate an existing file, even when the gallery has been tampered with.
    const int fd = open(dst, O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW, 0600);
    if (fd < 0) goto done;
    FILE *out = fdopen(fd, "wb");
    if (!out) {
        close(fd);
        remove(dst);
        goto done;
    }
    ok = fwrite(compressed, 1, length, out) == length;
    if (fclose(out) != 0) ok = false;
    if (!ok) remove(dst);

done:
    if (compressed) WebPFree(compressed);
    free(resized);
    free(pixels);
    if (decoder) decoder->Release(&decoder);
    if (factory) factory->Release(&factory);
    return ok;
}

