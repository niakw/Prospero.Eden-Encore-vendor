// SPDX-License-Identifier: MIT
// Native C regression: reject overflowed image allocations without truncating
// dimensions or performing a short allocation followed by an oversized write.
#include <assert.h>
#include <limits.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#define STB_IMAGE_IMPLEMENTATION
#include "../sources/eden/externals/stb/stb_image.h"
#define STB_IMAGE_RESIZE_IMPLEMENTATION
#include "../sources/eden/externals/stb/stb_image_resize.h"

int main(void)
{
    unsigned char source[16] = {255,0,0,255, 0,255,0,255, 0,0,255,255, 255,255,255,255};
    unsigned char target[16] = {0};
    stbi__uint16 *pixels;
    stbi__uint16 *converted;

    assert(stbir_resize_uint8(source, 2, 2, 0, target, 1, 1, 0, 4) == 1);
    assert(stbir_resize_uint8(source, 2, 2, 0, target, 2, 2, 0, 4) == 1);
    assert(stbir_resize_uint8(source, 2, 2, 0, target, 2, 2, INT_MAX, 4) == 0);
    assert(stbir_resize_uint8(source, 2, 2, INT_MAX, target, 2, 2, 0, 4) == 0);
    assert(stbir_resize_uint8(source, INT_MAX, 1, 0, target, 1, 1, 0, 4) == 0);
    assert(stbir_resize_uint8(source, 1, 1, 0, target, INT_MAX, 1, 0, 4) == 0);
    assert(stbir_resize_uint8(source, 0, 2, 0, target, 2, 2, 0, 4) == 0);
    assert(stbir_resize_uint8(source, 2, 2, 0, target, 0, 2, 0, 4) == 0);
    assert(stbir_resize_uint8(source, 2, 2, 0, target, 2, 2, 0, 0) == 0);
    assert(stbir_resize_uint8(source, 2, 2, 0, target, 2, 2, 0, 65) == 0);

    pixels = (stbi__uint16 *)malloc(sizeof(*pixels));
    assert(pixels != NULL);
    pixels[0] = 1234;
    converted = stbi__convert_format16(pixels, 1, 4, 1, 1);
    assert(converted != NULL);
    assert(converted[0] == 1234 && converted[1] == 1234 &&
           converted[2] == 1234 && converted[3] == 65535);
    free(converted);

    pixels = (stbi__uint16 *)malloc(sizeof(*pixels));
    assert(pixels != NULL);
    converted = stbi__convert_format16(pixels, 1, 4, UINT_MAX, UINT_MAX);
    assert(converted == NULL); // helper frees pixels on allocation failure

    return 0;
}
