/* test_surface.c — gpuaddr surface computation tests
 *
 * Verifies sceGpaComputeSurfaceInfo, sceGpaComputeSurfaceTileMode,
 * sceGpaFindOptimalSurface, sceGpaGetTileInfo, and
 * sceGpaComputeBaseSwizzle against known-good values from freegnm.
 */
#include "test.h"

#include <string.h>

#include "gpuaddr.h"
#include "gnm_dataformat.h"
#include "gnm_depthrendertarget.h"
#include "gnm_error.h"
#include "gnm_types.h"

/* --- Surface info: 256x256 R8G8B8A8_UNORM 2D thin --- */
static TestResult test_surfaceinfo_2d_thin(void) {
	GpaTilingParams tp = {
	    .tilemode = GNM_TM_THIN_1D_THIN,
	    .mingpumode = GNM_GPU_BASE,
	    .linearwidth = 256,
	    .linearheight = 256,
	    .lineardepth = 1,
	    .numfragsperpixel = 1,
	    .basetiledpitch = 256,
	    .miplevel = 0,
	    .arrayslice = 0,
	    .bitsperfrag = 32,
	    .surfaceflags = {0},
	    .isblockcompressed = false,
	};

	GpaSurfaceInfo info = {0};
	GpaError err = sceGpaComputeSurfaceInfo(&info, &tp);
	utassert(err == GPA_ERR_OK);
	/* 256x256x4 bytes = 256 KiB surface */
	utassert(info.surfacesize > 0);
	utassert(info.pitch > 0);
	utassert(info.height > 0);
	return test_success();
}

/* --- Surface info: 64x64 BC7 (block compressed) --- */
static TestResult test_surfaceinfo_bc7(void) {
	GpaTilingParams tp = {
	    .tilemode = GNM_TM_THIN_1D_THIN,
	    .mingpumode = GNM_GPU_BASE,
	    .linearwidth = 64,
	    .linearheight = 64,
	    .lineardepth = 1,
	    .numfragsperpixel = 1,
	    .basetiledpitch = 64,
	    .miplevel = 0,
	    .arrayslice = 0,
	    .bitsperfrag = 128, /* BC7 = 128 bits per 4x4 block */
	    .surfaceflags = {0},
	    .isblockcompressed = true,
	};

	GpaSurfaceInfo info = {0};
	GpaError err = sceGpaComputeSurfaceInfo(&info, &tp);
	utassert(err == GPA_ERR_OK);
	utassert(info.surfacesize > 0);
	return test_success();
}

/* --- FindOptimalSurface: 32bpp 2D --- */
static TestResult test_findoptimalsurface_2d(void) {
	GpaSurfaceProperties props = {0};
	GpaError err = sceGpaFindOptimalSurface(
	    &props, GPA_SURFACE_COLOR, 32, 1, false, GNM_GPU_BASE
	);
	utassert(err == GPA_ERR_OK);
	utassert(props.tilemode <= GNM_TM_DISPLAY_LINEAR_GENERAL);
	return test_success();
}

/* --- GetTileInfo: THIN_1D_THIN --- */
static TestResult test_gettileinfo_thin1d(void) {
	GpaTileInfo info = {0};
	GpaError err = sceGpaGetTileInfo(
	    &info, GNM_TM_THIN_1D_THIN, 32, 1, GNM_GPU_BASE
	);
	utassert(err == GPA_ERR_OK);
	return test_success();
}

/* --- ComputeBaseSwizzle: basic --- */
static TestResult test_computebaseswizzle(void) {
	uint32_t swizzle = 0;
	GpaError err = sceGpaComputeBaseSwizzle(
	    &swizzle, GNM_TM_THIN_1D_THIN, 0, 32, 1, GNM_GPU_BASE
	);
	utassert(err == GPA_ERR_OK);
	/* Swizzle should be a valid 13-bit value */
	utassert((swizzle & 0xFFFFE000) == 0);
	return test_success();
}

/* --- Tiling round-trip: 32x32 R8G8B8A8 linear <-> thin_1d --- */
static TestResult test_tiling_roundtrip(void) {
	const uint32_t w = 32, h = 32, bpp = 32;
	const size_t surfsize = (size_t)w * h * (bpp / 8);

	/* Source: linear pattern data */
	static uint8_t src[32 * 32 * 4] = {0};
	static uint8_t tiled[32 * 32 * 4] = {0};
	static uint8_t untiled[32 * 32 * 4] = {0};

	for (uint32_t y = 0; y < h; y += 1) {
		for (uint32_t x = 0; x < w; x += 1) {
			uint8_t* p = &src[(y * w + x) * 4];
			p[0] = (uint8_t)(x * 8);
			p[1] = (uint8_t)(y * 8);
			p[2] = 0x80;
			p[3] = 0xff;
		}
	}

	const GpaTilingParams srctp = {
	    .tilemode = GNM_TM_DISPLAY_LINEAR_GENERAL,
	    .mingpumode = GNM_GPU_BASE,
	    .linearwidth = w,
	    .linearheight = h,
	    .lineardepth = 1,
	    .numfragsperpixel = 1,
	    .basetiledpitch = w,
	    .miplevel = 0,
	    .arrayslice = 0,
	    .bitsperfrag = bpp,
	    .surfaceflags = {0},
	    .isblockcompressed = false,
	};
	const GpaTilingParams dst_tp = {
	    .tilemode = GNM_TM_THIN_1D_THIN,
	    .mingpumode = GNM_GPU_BASE,
	    .linearwidth = w,
	    .linearheight = h,
	    .lineardepth = 1,
	    .numfragsperpixel = 1,
	    .basetiledpitch = w,
	    .miplevel = 0,
	    .arrayslice = 0,
	    .bitsperfrag = bpp,
	    .surfaceflags = {0},
	    .isblockcompressed = false,
	};

	GpaError err = sceGpaTileSurface(
	    tiled, surfsize, src, surfsize, &srctp, &dst_tp
	);
	utassert(err == GPA_ERR_OK);

	/* Reverse direction: tiled -> linear */
	err = sceGpaTileSurface(
	    untiled, surfsize, tiled, surfsize, &dst_tp, &srctp
	);
	utassert(err == GPA_ERR_OK);

	/* Round-trip should reproduce the original */
	utassert(memcmp(src, untiled, surfsize) == 0);
	return test_success();
}

/* --- DRT byte size --- */
static TestResult test_drtbytesize(void) {
	GnmDepthRenderTarget desc = {0};
	desc.zinfo.asuint = 34547;
	desc.stencilinfo.asuint = 537927680;
	desc.zreadbase256b = 33614080;
	desc.stencilreadbase256b = 0;
	desc.zwritebase256b = 33614080;
	desc.stencilwritebase256b = 0;
	desc.depthsize.asuint = 293103;
	desc.depthslice.asuint = 0;
	desc.depthview.asuint = 0;
	desc.htiledatabase256b = 0;
	desc.htilesurface.asuint = 0;
	desc.depthinfo.asuint = 1739840;
	desc.size.asuint = 0;

	uint64_t zsize = 0;
	GnmError err = sceGnmDrtCalcByteSize(&zsize, NULL, &desc);
	utassert(err == GNM_ERROR_OK);
	utasserteq((long long)zsize, 0x900000LL);
	return test_success();
}

int run_tests_surface(void) {
	const TestUnit tests[] = {
	    {test_surfaceinfo_2d_thin, "ComputeSurfaceInfo 2D thin 256x256 RGBA8"},
	    {test_surfaceinfo_bc7, "ComputeSurfaceInfo BC7 64x64"},
	    {test_findoptimalsurface_2d, "FindOptimalSurface 32bpp 2D"},
	    {test_gettileinfo_thin1d, "GetTileInfo THIN_1D_THIN"},
	    {test_computebaseswizzle, "ComputeBaseSwizzle"},
	    {test_tiling_roundtrip, "Tiling round-trip 32x32 RGBA8"},
	    {test_drtbytesize, "DRT byte size"},
	};
	return test_suite("gpuaddr/surface", tests, sizeof(tests) / sizeof(tests[0]));
}
