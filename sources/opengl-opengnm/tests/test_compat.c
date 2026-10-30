#include "test.h"

#include <gnm/drawcommandbuffer.h>
#include <gnm/gpuaddr/gpuaddr.h>
#include <gnm/platform.h>
#include <gnm/strings.h>

#include <string.h>

static uint32_t s_cmd_storage[256] __attribute__((aligned(16)));

static TestResult compat_old_include_paths_compile(void) {
	GnmPlatParams params = {.gpumode = GNM_GPU_BASE};
	gnmPlatInit(&params);
	utasserteq(gnmGpuMode(), GNM_GPU_BASE);

	GnmCommandBuffer cmd = gnmCmdInit(
	    s_cmd_storage,
	    (uint32_t)sizeof(s_cmd_storage),
	    NULL,
	    NULL
	);
	utassert(cmd.beginptr == s_cmd_storage);
	utassert(cmd.cmdptr == s_cmd_storage);

	void* scratch = gnmCmdAllocInside(&cmd, 16, 16);
	utassert(scratch != NULL);
	utassert((((uintptr_t)scratch) & 15u) == 0);

	gnmDrawCmdSetPrimitiveType(&cmd, GNM_PT_TRILIST);
	gnmDrawCmdSetIndexSize(&cmd, GNM_INDEX_16, GNM_POLICY_LRU);
	gnmDrawCmdSetIndexBuffer(&cmd, (const void*)(uintptr_t)0x1000);
	gnmDrawCmdDrawIndexAuto(&cmd, 3);
	utassert(cmd.cmdptr > s_cmd_storage);

	gnmCmdReset(&cmd);
	utassert(cmd.cmdptr == cmd.beginptr);

	return test_success();
}

static TestResult compat_helper_aliases_compile(void) {
	const GnmDataFormat fmt = GNM_FMT_R8G8B8A8_UNORM;
	utasserteq(gnmDfGetBytesPerElement(fmt), 4);
	utasserteq(gnmDfGetTotalBytesPerElement(fmt), 4);
	utassert(gnmStrTileMode(GNM_TM_DISPLAY_LINEAR_ALIGNED) != NULL);

	GnmRenderTarget rt;
	memset(&rt, 0, sizeof(rt));
	gnmRtSetBaseAddr(&rt, (void*)(uintptr_t)0x2000);
	utassert(gnmRtGetBaseAddr(&rt) == (void*)(uintptr_t)0x2000);

	GnmBuffer buf = gnmCreateVertexBuffer(
	    (void*)(uintptr_t)0x3000,
	    fmt,
	    4,
	    2
	);
	utassert(gnmBufGetBaseAddress(&buf) == (void*)(uintptr_t)0x3000);
	utasserteq(gnmBufGetFormat(&buf).asuint, fmt.asuint);

	GpaSurfaceProperties props;
	memset(&props, 0, sizeof(props));
	GpaError err = gpaFindOptimalSurface(
	    &props,
	    GPA_SURFACE_COLOR,
	    32,
	    1,
	    false,
	    GNM_GPU_BASE
	);
	utasserteq(err, GPA_ERR_OK);
	utassert(gpaStrError(err) != NULL);

	return test_success();
}

static TestResult compat_driver_aliases_compile_and_link(void) {
	uint32_t cmdbuf[16] = {0};
	SceGnmDrawFlags flags = {0};
	int32_t res = gnmDriverDrawIndexAuto(cmdbuf, 7, 3, flags);
	utassert(res == 0 || res < 0);

	res = gnmDriverDrawInitDefaultHardwareState350(cmdbuf, 16);
	utassert(res >= 0 || res < 0);

	return test_success();
}

int run_tests_compat(void) {
	static const TestUnit tests[] = {
	    {compat_old_include_paths_compile, "old <gnm/...> include paths"},
	    {compat_helper_aliases_compile, "freegnm helper aliases"},
	    {compat_driver_aliases_compile_and_link, "freegnm driver aliases"},
	};

	return test_suite("compat/freegnm", tests, sizeof(tests) / sizeof(tests[0]));
}
