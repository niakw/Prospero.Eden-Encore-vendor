/* test_api.c — sceGnm* API linkage + signature check
 *
 * Calls one function from each API category to verify:
 * 1. The function is linked (not an undefined symbol)
 * 2. The function is callable with the expected signature
 * 3. The return type matches expectations (int32_t/uint32_t/void/etc.)
 *
 * This catches:
 * - Missing implementations in either backend
 * - Signature mismatches between header and implementation
 * - Linker errors from renamed/removed functions
 */
#include "test.h"

#include <string.h>

#include "gnm.h"
#include "gnm_commandbuffer.h"
#include "gnm_depthrendertarget.h"
#include "gnm_drawcommandbuffer.h"
#include "gnm_error.h"
#include "gnm_rendertarget.h"
#include "gnm_shader.h"
#include "gnm_texture.h"
#include "gnm_types.h"
#include "gnmdriver.h"
#include "gpuaddr.h"
#include "platform.h"

static uint32_t s_cmdbuf[4096];

/* --- Draw API --- */
static TestResult test_api_draw(void) {
	memset(s_cmdbuf, 0, sizeof(s_cmdbuf));
	int32_t r = sceGnmDrawIndexAuto(s_cmdbuf, 7, 1024, 0);
	utassert(r == GNM_ERROR_OK || r == GNM_ERROR_CMD_FAILED);

	r = sceGnmDrawIndexOffset(s_cmdbuf, 9, 0, 64, 0);
	utassert(r == GNM_ERROR_OK || r == GNM_ERROR_CMD_FAILED);
	return test_success();
}

/* --- Dispatch API --- */
static TestResult test_api_dispatch(void) {
	memset(s_cmdbuf, 0, sizeof(s_cmdbuf));
	int32_t r = sceGnmDispatchDirect(s_cmdbuf, 12, 64, 64, 1, 0);
	utassert(r == GNM_ERROR_OK || r == GNM_ERROR_CMD_FAILED);

	r = sceGnmDispatchIndirect(s_cmdbuf, 7, 0, 0);
	utassert(r == GNM_ERROR_OK || r == GNM_ERROR_CMD_FAILED);
	return test_success();
}

/* --- Shader set API --- */
static TestResult test_api_shader_set(void) {
	memset(s_cmdbuf, 0, sizeof(s_cmdbuf));
	GnmVsStageRegisters vsregs = {0};
	int32_t r = sceGnmSetVsShader(s_cmdbuf, 64, (const uint32_t*)&vsregs, 0);
	utassert(r == GNM_ERROR_OK || r == GNM_ERROR_CMD_FAILED);

	GnmPsStageRegisters psregs = {0};
	r = sceGnmSetPsShader(s_cmdbuf, 64, (const uint32_t*)&psregs);
	utassert(r == GNM_ERROR_OK || r == GNM_ERROR_CMD_FAILED);

	GnmCsStageRegisters csregs = {0};
	r = sceGnmSetCsShader(s_cmdbuf, 64, (const uint32_t*)&csregs);
	utassert(r == GNM_ERROR_OK || r == GNM_ERROR_CMD_FAILED);

	r = sceGnmSetEmbeddedVsShader(s_cmdbuf, 64, GNM_EMBEDDED_VSH_FULLSCREEN, 0);
	utassert(r == GNM_ERROR_OK || r == GNM_ERROR_CMD_FAILED);

	r = sceGnmSetEmbeddedPsShader(s_cmdbuf, 64, GNM_EMBEDDED_PSH_DUMMY, 0);
	utassert(r == GNM_ERROR_OK || r == GNM_ERROR_CMD_FAILED);
	return test_success();
}

/* --- Shader update API --- */
static TestResult test_api_shader_update(void) {
	memset(s_cmdbuf, 0, sizeof(s_cmdbuf));
	GnmVsStageRegisters vsregs = {0};
	int32_t r = sceGnmUpdateVsShader(s_cmdbuf, 64, (const uint32_t*)&vsregs, 0);
	utassert(r == GNM_ERROR_OK || r == GNM_ERROR_CMD_FAILED);

	GnmPsStageRegisters psregs = {0};
	r = sceGnmUpdatePsShader(s_cmdbuf, 64, (const uint32_t*)&psregs);
	utassert(r == GNM_ERROR_OK || r == GNM_ERROR_CMD_FAILED);

	GnmGsStageRegisters gsregs = {0};
	r = sceGnmUpdateGsShader(s_cmdbuf, 64, (const uint32_t*)&gsregs);
	utassert(r == GNM_ERROR_OK || r == GNM_ERROR_CMD_FAILED);
	return test_success();
}

/* --- Init / default state API --- */
static TestResult test_api_init_state(void) {
	memset(s_cmdbuf, 0, sizeof(s_cmdbuf));
	uint32_t written = sceGnmDrawInitDefaultHardwareState(
	    s_cmdbuf, sizeof(s_cmdbuf) / sizeof(uint32_t)
	);
	utassert(written > 0);

	written = sceGnmDrawInitDefaultHardwareState350(
	    s_cmdbuf, sizeof(s_cmdbuf) / sizeof(uint32_t)
	);
	utassert(written > 0);

	written = sceGnmDispatchInitDefaultHardwareState(
	    s_cmdbuf, sizeof(s_cmdbuf) / sizeof(uint32_t)
	);
	/* May return 0 on generic — just verify it doesn't crash */
	(void)written;
	return test_success();
}

/* --- Submit API --- */
static TestResult test_api_submit(void) {
	/* Generic backend: submit is a no-op returning OK */
	void* const dcb_addrs[] = {s_cmdbuf};
	uint32_t dcb_sizes[] = {16};
	void* const ccb_addrs[] = {NULL};
	uint32_t ccb_sizes[] = {0};

	int32_t r = sceGnmSubmitCommandBuffers(
	    0, NULL, NULL, NULL, NULL
	);
	utasserteq((long long)r, (long long)GNM_ERROR_OK);

	r = sceGnmSubmitCommandBuffers(
	    1, dcb_addrs, dcb_sizes, ccb_addrs, ccb_sizes
	);
	utasserteq((long long)r, (long long)GNM_ERROR_OK);

	r = sceGnmSubmitAndFlipCommandBuffers(
	    1, dcb_addrs, dcb_sizes, ccb_addrs, ccb_sizes, 0, 0, 0, 0
	);
	utasserteq((long long)r, (long long)GNM_ERROR_OK);

	r = sceGnmSubmitCommandBuffersForWorkload(
	    3, 1, dcb_addrs, dcb_sizes, ccb_addrs, ccb_sizes
	);
	utasserteq((long long)r, (long long)GNM_ERROR_OK);

	r = sceGnmSubmitAndFlipCommandBuffersForWorkload(
	    3, 1, dcb_addrs, dcb_sizes, ccb_addrs, ccb_sizes, 0, 0, 0, 0
	);
	utasserteq((long long)r, (long long)GNM_ERROR_OK);

	r = sceGnmSubmitDone();
	utasserteq((long long)r, (long long)GNM_ERROR_OK);

	int allowed = sceGnmAreSubmitsAllowed();
	utasserteq((long long)allowed, 1LL);
	return test_success();
}

/* --- VGT control API --- */
static TestResult test_api_vgt(void) {
	memset(s_cmdbuf, 0, sizeof(s_cmdbuf));
	int32_t r = sceGnmResetVgtControl(s_cmdbuf, 3);
	utasserteq((long long)r, (long long)GNM_ERROR_OK);

	r = sceGnmSetVgtControl(s_cmdbuf, 3, 0x40, 0, 0);
	utasserteq((long long)r, (long long)GNM_ERROR_OK);
	return test_success();
}

/* --- Marker API --- */
static TestResult test_api_markers(void) {
	memset(s_cmdbuf, 0, sizeof(s_cmdbuf));
	int32_t r = sceGnmInsertSetMarker(s_cmdbuf, 4, "test");
	utassert(r == GNM_ERROR_OK || r == GNM_ERROR_CMD_FAILED);

	r = sceGnmInsertPushMarker(s_cmdbuf, 4, "test");
	utassert(r == GNM_ERROR_OK || r == GNM_ERROR_CMD_FAILED);

	r = sceGnmInsertPopMarker(s_cmdbuf, 4);
	utassert(r == GNM_ERROR_OK || r == GNM_ERROR_CMD_FAILED);
	return test_success();
}

/* --- Compute queue API --- */
static TestResult test_api_compute_queue(void) {
	int32_t r = sceGnmMapComputeQueue(0, 0, 0, 0, NULL);
	/* Generic returns UNSUPPORTED */
	utassert(r == GNM_ERROR_UNSUPPORTED || r == GNM_ERROR_OK);

	r = sceGnmUnmapComputeQueue(0);
	utassert(r == GNM_ERROR_OK || r == GNM_ERROR_UNSUPPORTED);

	sceGnmDingDong(0, 0); /* void — just verify it links */
	return test_success();
}

/* --- Event queue API --- */
static TestResult test_api_eq(void) {
	int32_t r = sceGnmAddEqEvent(NULL, 0, NULL);
	utassert(r == GNM_ERROR_UNSUPPORTED || r == GNM_ERROR_OK);

	r = sceGnmDeleteEqEvent(NULL, 0);
	utassert(r == GNM_ERROR_OK || r == GNM_ERROR_UNSUPPORTED);

	int32_t ev = sceGnmGetEqEventType(NULL);
	(void)ev; /* stub returns 0 — just verify linkage */

	int32_t ts = sceGnmGetEqTimeStamp();
	(void)ts;
	return test_success();
}

/* --- Workload API --- */
static TestResult test_api_workload(void) {
	uint64_t wl = 0;
	int r = sceGnmBeginWorkload(0, &wl);
	utasserteq((long long)r, (long long)GNM_ERROR_OK);

	r = sceGnmEndWorkload(wl);
	utasserteq((long long)r, (long long)GNM_ERROR_OK);

	uint32_t stream = 0;
	r = sceGnmCreateWorkloadStream(0, &stream);
	utasserteq((long long)r, (long long)GNM_ERROR_OK);

	r = sceGnmDestroyWorkloadStream();
	utasserteq((long long)r, (long long)GNM_ERROR_OK);
	return test_success();
}

/* --- Misc getters API --- */
static TestResult test_api_getters(void) {
	uint32_t clock = sceGnmGetGpuCoreClockFrequency();
	(void)clock; /* 0 on generic */

	uintptr_t tess = sceGnmGetTheTessellationFactorRingBufferBaseAddress();
	(void)tess; /* 0 on generic */

	int32_t sz = sceGnmGetOffChipTessellationBufferSize();
	(void)sz;

	sceGnmFlushGarlic(); /* void — verify linkage */

	int32_t r = sceGnmDrawInitToDefaultContextStateInternalSize();
	utassert(r > 0);
	return test_success();
}

/* --- Resource registration API (stubs) --- */
static TestResult test_api_resources(void) {
	int32_t r = sceGnmFindResourcesPublic();
	utassert(r != GNM_ERROR_OK); /* stubs return FAILURE */

	int r2 = sceGnmGetResourceType();
	utassert(r2 != GNM_ERROR_OK);

	r = sceGnmRegisterOwner(NULL, "test");
	utassert(r != GNM_ERROR_OK);
	return test_success();
}

/* --- SDMA API (stubs) --- */
static TestResult test_api_sdma(void) {
	int r = sceGnmSdmaOpen();
	utassert(r != GNM_ERROR_OK); /* stub returns FAILURE */

	r = sceGnmSdmaClose();
	utassert(r != GNM_ERROR_OK);

	r = sceGnmSdmaCopyLinear();
	utassert(r != GNM_ERROR_OK);
	return test_success();
}

/* --- Debugger API (stubs) --- */
static TestResult test_api_debugger(void) {
	int r = sceGnmDebuggerHaltWavefront();
	(void)r; /* stub — just verify linkage */

	sceGnmDebugReset(); /* void — verify linkage */

	r = sceGnmDebuggerReadGds();
	(void)r;
	return test_success();
}

/* --- Validation API --- */
static TestResult test_api_validation(void) {
	int v = sceGnmValidateGetVersion();
	utassert(v >= 0);

	int r = sceGnmValidateResetState();
	utasserteq((long long)r, (long long)GNM_ERROR_OK);

	bool onsubmit = sceGnmValidateOnSubmitEnabled();
	utassert(onsubmit == false);
	return test_success();
}

/* --- GpuAddr (sceGpa*) API --- */
static TestResult test_api_gpuaddr(void) {
	GpaTilingParams tp = {
	    .tilemode = GNM_TM_THIN_1D_THIN,
	    .mingpumode = GNM_GPU_BASE,
	    .linearwidth = 128,
	    .linearheight = 128,
	    .lineardepth = 1,
	    .numfragsperpixel = 1,
	    .basetiledpitch = 128,
	    .miplevel = 0,
	    .arrayslice = 0,
	    .bitsperfrag = 32,
	    .surfaceflags = {0},
	    .isblockcompressed = false,
	};
	GpaSurfaceInfo info = {0};
	GpaError err = sceGpaComputeSurfaceInfo(&info, &tp);
	utasserteq((long long)err, (long long)GPA_ERR_OK);

	const char* strerr = sceGpaStrError(GPA_ERR_OK);
	utassert(strerr != NULL);
	return test_success();
}

/* --- Platform API --- */
static TestResult test_api_platform(void) {
	GnmGpuMode mode = sceGnmGpuMode();
	utassert(mode == GNM_GPU_BASE || mode == GNM_GPU_NEO);

	uint64_t label = 0;
	int32_t r = sceGnmPlatGetBufferLabelAddress(0, &label);
	utasserteq((long long)r, (long long)GNM_ERROR_OK);
	return test_success();
}

int run_tests_api(void) {
	const TestUnit tests[] = {
	    {test_api_draw, "Draw API"},
	    {test_api_dispatch, "Dispatch API"},
	    {test_api_shader_set, "Shader set API"},
	    {test_api_shader_update, "Shader update API"},
	    {test_api_init_state, "Init/default state API"},
	    {test_api_submit, "Submit API"},
	    {test_api_vgt, "VGT control API"},
	    {test_api_markers, "Marker API"},
	    {test_api_compute_queue, "Compute queue API"},
	    {test_api_eq, "Event queue API"},
	    {test_api_workload, "Workload API"},
	    {test_api_getters, "Misc getters API"},
	    {test_api_resources, "Resource registration API"},
	    {test_api_sdma, "SDMA API"},
	    {test_api_debugger, "Debugger API"},
	    {test_api_validation, "Validation API"},
	    {test_api_gpuaddr, "GpuAddr (sceGpa*) API"},
	    {test_api_platform, "Platform API"},
	};
	return test_suite(
	    "api/linkage", tests, sizeof(tests) / sizeof(tests[0])
	);
}
