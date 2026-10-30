/* test_drawcmd.c — PM4 command buffer building tests
 *
 * Verifies that the draw command buffer API emits correct PM4 packets.
 * Compares packet headers, opcodes, and key data dwords against
 * expected values derived from shadPS4 and freegnm.
 */
#include "test.h"

#include <stdint.h>
#include <string.h>

#include "gnm.h"
#include "gnm_commandbuffer.h"
#include "gnm_drawcommandbuffer.h"
#include "gnm_shader.h"
#include "gnm_types.h"
#include "gnmdriver.h"
#include "platform.h"
#include "pm4/sid.h"

/* PM4 header field extractors */
#define PKT_TYPE(x)     (((x) >> 30) & 0x3)
#define PKT_COUNT(x)    (((x) >> 16) & 0x3FFF)
#define PKT3_OPCODE(x)  (((x) >> 8) & 0xFF)
#define PKT3_PRED(x)    ((x) & 0x1)

#define PM4_EVENT_TYPE_ZPASS_DONE 0x15

static uint32_t s_cmdbuf[4096];
static uint32_t s_resizebuf[32];
static uint64_t s_test_labeladdr;

static int32_t test_get_buffer_label_address(
    int32_t videohandle, uint64_t* outaddr
) {
	(void)videohandle;
	*outaddr = s_test_labeladdr;
	return GNM_ERROR_OK;
}

static void use_test_label_address(uint64_t labeladdr) {
	s_test_labeladdr = labeladdr;
	GnmPlatParams params = {
	    .gpumode = GNM_GPU_BASE,
	    .getbufferlabeladdress = test_get_buffer_label_address,
	};
	sceGnmPlatInit(&params);
}

static void reset_platform_base(void) {
	GnmPlatParams params = {.gpumode = GNM_GPU_BASE};
	sceGnmPlatInit(&params);
}

static bool grow_callback(
    GnmCommandBuffer* cmd, uint32_t sizedwords, void* userdata
) {
	uint32_t* buf = userdata;
	(void)sizedwords;
	cmd->beginptr = buf;
	cmd->cmdptr = buf;
	cmd->endptr = buf + 32;
	cmd->sizedwords = 32;
	return true;
}

static GnmCommandBuffer new_cmdbuf(void) {
	GnmCommandBuffer cmd = sceGnmCmdInit(
	    s_cmdbuf, sizeof(s_cmdbuf), NULL, NULL
	);
	return cmd;
}

static uint32_t cmd_dwords_used(const GnmCommandBuffer* cmd) {
	return (uint32_t)(cmd->cmdptr - cmd->beginptr);
}

/* --- DrawIndexAuto: emits DRAW_INDEX_AUTO + NOP --- */
static TestResult test_drawindexauto(void) {
	memset(s_cmdbuf, 0, sizeof(s_cmdbuf));
	GnmCommandBuffer cmd = new_cmdbuf();

	sceGnmDrawCmdDrawIndexAuto(&cmd, 1024);

	const uint32_t used = cmd_dwords_used(&cmd);
	utasserteq((long long)used, 7LL);

	/* Header: type 3, opcode DRAW_INDEX_AUTO (0x2D), count 1 */
	utasserteq((long long)PKT_TYPE(s_cmdbuf[0]), 3LL);
	utasserteq((long long)PKT3_OPCODE(s_cmdbuf[0]), (long long)PKT3_DRAW_INDEX_AUTO);
	utasserteq((long long)PKT_COUNT(s_cmdbuf[0]), 1LL);
	utasserteq((long long)s_cmdbuf[1], 1024LL); /* index count */
	/* cmdbuf[2] = SOURCE_SELECT = AUTO_INDEX (0) */
	/* cmdbuf[3..6] = trailing NOP */
	utasserteq((long long)PKT3_OPCODE(s_cmdbuf[3]), (long long)PKT3_NOP);
	return test_success();
}

/* --- DrawIndex: emits DRAW_INDEX_2 + NOP --- */
static TestResult test_drawindex(void) {
	memset(s_cmdbuf, 0, sizeof(s_cmdbuf));
	GnmCommandBuffer cmd = new_cmdbuf();

	const void* fake_addr = (const void*)0x1000;
	sceGnmDrawCmdDrawIndex(&cmd, 256, fake_addr);

	const uint32_t used = cmd_dwords_used(&cmd);
	utasserteq((long long)used, 10LL);

	utasserteq((long long)PKT_TYPE(s_cmdbuf[0]), 3LL);
	utasserteq((long long)PKT3_OPCODE(s_cmdbuf[0]), (long long)PKT3_DRAW_INDEX_2);
	utasserteq((long long)PKT_COUNT(s_cmdbuf[0]), 4LL);
	utasserteq((long long)s_cmdbuf[1], 256LL); /* index count */
	/* cmdbuf[2] = addr low, cmdbuf[3] = addr high, cmdbuf[4] = max count */
	/* cmdbuf[5] = 0, cmdbuf[6..9] = trailing NOP */
	utasserteq((long long)PKT3_OPCODE(s_cmdbuf[6]), (long long)PKT3_NOP);
	return test_success();
}

/* --- DrawIndexAuto with predication enabled --- */
static TestResult test_drawindexauto_predicated(void) {
	memset(s_cmdbuf, 0, sizeof(s_cmdbuf));
	GnmCommandBuffer cmd = new_cmdbuf();
	cmd.flags.predication_enabled = 1;

	GnmDrawModifier mod = {0};
	sceGnmDrawCmdDrawIndexAuto2(&cmd, 512, mod);

	/* Predication bit should be set in the header */
	utasserteq((long long)PKT3_PRED(s_cmdbuf[0]), 1LL);
	utasserteq((long long)PKT3_OPCODE(s_cmdbuf[0]), (long long)PKT3_DRAW_INDEX_AUTO);
	return test_success();
}

static TestResult test_drawcmd_callback_resize(void) {
	uint32_t tiny[1] = {0};
	memset(s_resizebuf, 0, sizeof(s_resizebuf));
	GnmCommandCallbackFunc cb = grow_callback;
	GnmCommandBuffer cmd = sceGnmCmdInit(
	    tiny, sizeof(tiny), &cb, s_resizebuf
	);

	sceGnmDrawCmdDrawIndexAuto(&cmd, 64);

	utassert(cmd.beginptr == s_resizebuf);
	utasserteq((long long)cmd_dwords_used(&cmd), 7LL);
	utasserteq((long long)PKT3_OPCODE(s_resizebuf[0]), (long long)PKT3_DRAW_INDEX_AUTO);
	return test_success();
}

static TestResult test_cmdallocinside_small_size(void) {
	memset(s_cmdbuf, 0, sizeof(s_cmdbuf));
	GnmCommandBuffer cmd = new_cmdbuf();

	void* ptr = sceGnmCmdAllocInside(&cmd, 1, 4);

	utassert(ptr == &s_cmdbuf[1]);
	utasserteq((long long)cmd_dwords_used(&cmd), 2LL);
	utasserteq((long long)PKT3_OPCODE(s_cmdbuf[0]), (long long)PKT3_NOP);
	utasserteq((long long)PKT_COUNT(s_cmdbuf[0]), 0LL);
	return test_success();
}

/* --- SetVsShader via driver API --- */
static TestResult test_setvsshader_driver(void) {
	memset(s_cmdbuf, 0, sizeof(s_cmdbuf));
	GnmVsStageRegisters regs = {0};
	regs.spishaderpgmlovs = 0x1000;
	regs.spishaderpgmrsrc1vs = 0x40000;
	regs.spishaderpgmrsrc2vs = 0x100;
	regs.paclvsoutcntl = 0x1;
	regs.spivsoutconfig = 0x0;
	regs.spishaderposformat = 0x1;

	int32_t res = sceGnmSetVsShader(s_cmdbuf, 64, (const uint32_t*)&regs, 0);
	utasserteq((long long)res, (long long)GNM_ERROR_OK);

	/* First packet should be SET_SH_REG for SPI_SHADER_PGM_LO_VS */
	utasserteq((long long)PKT_TYPE(s_cmdbuf[0]), 3LL);
	utasserteq((long long)PKT3_OPCODE(s_cmdbuf[0]), (long long)PKT3_SET_SH_REG);
	return test_success();
}

/* --- SetPsShader via driver API --- */
static TestResult test_setpsshader_driver(void) {
	memset(s_cmdbuf, 0, sizeof(s_cmdbuf));
	GnmPsStageRegisters regs = {0};
	regs.spishaderpgmlops = 0x2000;
	regs.spishaderpgmrsrc1ps = 0x40000;

	int32_t res = sceGnmSetPsShader(s_cmdbuf, 64, (const uint32_t*)&regs);
	utasserteq((long long)res, (long long)GNM_ERROR_OK);

	utasserteq((long long)PKT_TYPE(s_cmdbuf[0]), 3LL);
	utasserteq((long long)PKT3_OPCODE(s_cmdbuf[0]), (long long)PKT3_SET_SH_REG);
	return test_success();
}

/* --- DrawInitDefaultHardwareState350 --- */
static TestResult test_drawinithwstate(void) {
	memset(s_cmdbuf, 0, sizeof(s_cmdbuf));
	uint32_t written = sceGnmDrawInitDefaultHardwareState350(
	    s_cmdbuf, sizeof(s_cmdbuf) / sizeof(uint32_t)
	);
	utassert(written > 0);
	/* Should start with a type-3 packet */
	utasserteq((long long)PKT_TYPE(s_cmdbuf[0]), 3LL);
	return test_success();
}

/* --- ResetVgtControl: 3 dwords, IA_MULTI_VGT_PARAM --- */
static TestResult test_resetvgtcontrol(void) {
	memset(s_cmdbuf, 0, sizeof(s_cmdbuf));
	int32_t res = sceGnmResetVgtControl(s_cmdbuf, 3);
	utasserteq((long long)res, (long long)GNM_ERROR_OK);

	utasserteq((long long)PKT_TYPE(s_cmdbuf[0]), 3LL);
	utasserteq((long long)PKT3_OPCODE(s_cmdbuf[0]), (long long)PKT3_SET_CONTEXT_REG);
	utasserteq((long long)PKT_COUNT(s_cmdbuf[0]), 1LL);
	/* Value should be 0xFF (default IA_MULTI_VGT_PARAM) */
	utasserteq((long long)s_cmdbuf[2], 0xFFLL);
	return test_success();
}

/* --- SetVgtControl: validation + value --- */
static TestResult test_setvgtcontrol(void) {
	memset(s_cmdbuf, 0, sizeof(s_cmdbuf));
	reset_platform_base();
	int32_t res = sceGnmSetVgtControl(s_cmdbuf, 3, 0x80, 1, 0);
	utasserteq((long long)res, (long long)GNM_ERROR_OK);

	utasserteq((long long)PKT3_OPCODE(s_cmdbuf[0]), (long long)PKT3_SET_CONTEXT_REG);
	/* Expected: (1 << 16) | 0x80 = 0x10080 */
	utasserteq((long long)s_cmdbuf[2], 0x10080LL);
	return test_success();
}

static TestResult test_setvgtcontrol_neo(void) {
	GnmPlatParams neo = {.gpumode = GNM_GPU_NEO};

	memset(s_cmdbuf, 0, sizeof(s_cmdbuf));
	sceGnmPlatInit(&neo);
	int32_t res = sceGnmSetVgtControl(s_cmdbuf, 3, 0x7f, 0, 0);
	reset_platform_base();

	utasserteq((long long)res, (long long)GNM_ERROR_OK);
	utasserteq((long long)PKT3_OPCODE(s_cmdbuf[0]), (long long)PKT3_SET_UCONFIG_REG);
	utasserteq((long long)PKT_COUNT(s_cmdbuf[0]), 1LL);
	utasserteq((long long)s_cmdbuf[1], 0x40000258LL);
	utasserteq((long long)s_cmdbuf[2], 0x6d007fLL);

	memset(s_cmdbuf, 0, sizeof(s_cmdbuf));
	sceGnmPlatInit(&neo);
	res = sceGnmSetVgtControl(s_cmdbuf, 3, 0x80, 1, 1);
	reset_platform_base();

	utasserteq((long long)res, (long long)GNM_ERROR_OK);
	utasserteq((long long)PKT3_OPCODE(s_cmdbuf[0]), (long long)PKT3_SET_UCONFIG_REG);
	utasserteq((long long)PKT_COUNT(s_cmdbuf[0]), 1LL);
	utasserteq((long long)s_cmdbuf[1], 0x40000258LL);
	utasserteq((long long)s_cmdbuf[2], 0x750080LL);
	return test_success();
}

/* --- SetVgtControl: invalid args rejected --- */
static TestResult test_setvgtcontrol_invalid(void) {
	memset(s_cmdbuf, 0, sizeof(s_cmdbuf));
	/* prim_group_sz >= 0x100 should fail */
	int32_t res = sceGnmSetVgtControl(s_cmdbuf, 3, 0x100, 0, 0);
	utassert(res != GNM_ERROR_OK);

	/* mode >= 2 should fail */
	res = sceGnmSetVgtControl(s_cmdbuf, 3, 0x10, 2, 0);
	utassert(res != GNM_ERROR_OK);

	/* wrong size should fail */
	res = sceGnmSetVgtControl(s_cmdbuf, 4, 0x10, 0, 0);
	utassert(res != GNM_ERROR_OK);
	return test_success();
}

/* --- DrawIndexOffset: 9 dwords, predication + RT slice --- */
static TestResult test_drawindexoffset(void) {
	memset(s_cmdbuf, 0, sizeof(s_cmdbuf));
	/* flags: predication=1, RT slice offset=3 (bits 29-31) */
	uint32_t flags = 1 | (3u << 29);
	int32_t res = sceGnmDrawIndexOffset(s_cmdbuf, 9, 0x100, 256, flags);
	utasserteq((long long)res, (long long)GNM_ERROR_OK);

	utasserteq((long long)PKT3_OPCODE(s_cmdbuf[0]), (long long)PKT3_DRAW_INDEX_OFFSET_2);
	utasserteq((long long)PKT3_PRED(s_cmdbuf[0]), 1LL);
	utasserteq((long long)s_cmdbuf[1], 256LL);   /* index count */
	utasserteq((long long)s_cmdbuf[2], 0x100LL); /* index offset */
	utasserteq((long long)s_cmdbuf[4], 0LL);     /* base mode has no RT slice bits */
	/* cmdbuf[5] = trailing NOP */
	utasserteq((long long)PKT3_OPCODE(s_cmdbuf[5]), (long long)PKT3_NOP);
	return test_success();
}

static TestResult test_drawindexoffset_rejects_large_size(void) {
	memset(s_cmdbuf, 0, sizeof(s_cmdbuf));

	int32_t res = sceGnmDrawIndexOffset(s_cmdbuf, 16, 0x100, 256, 0);

	utassert(res != GNM_ERROR_OK);
	return test_success();
}

static TestResult test_drawindex_rejects_null_indexaddr(void) {
	memset(s_cmdbuf, 0, sizeof(s_cmdbuf));

	int32_t res = sceGnmDrawIndex(s_cmdbuf, 10, 256, 0, 0, 0);

	utassert(res != GNM_ERROR_OK);
	return test_success();
}

static TestResult test_drawindirect_rejects_truncated_sgpr_offsets(void) {
	memset(s_cmdbuf, 0, sizeof(s_cmdbuf));

	int32_t res = sceGnmDrawIndirect(
	    s_cmdbuf, 9, 0, GNM_STAGE_VS, 0x100, 0, 0
	);

	utassert(res != GNM_ERROR_OK);
	return test_success();
}

static TestResult test_drawindexoffset_neo_slice_bits(void) {
	memset(s_cmdbuf, 0, sizeof(s_cmdbuf));
	GnmPlatParams neo = {.gpumode = GNM_GPU_NEO};
	GnmPlatParams base = {.gpumode = GNM_GPU_BASE};
	sceGnmPlatInit(&neo);

	const uint32_t flags = 1 | (3u << 29);
	int32_t res = sceGnmDrawIndexOffset(s_cmdbuf, 9, 0x100, 256, flags);

	sceGnmPlatInit(&base);
	utasserteq((long long)res, (long long)GNM_ERROR_OK);
	utasserteq((long long)s_cmdbuf[4], (long long)(3u << 29));
	return test_success();
}

static TestResult test_event_write_eop_data64_layout(void) {
	memset(s_cmdbuf, 0, sizeof(s_cmdbuf));
	GnmCommandBuffer cmd = new_cmdbuf();

	sceGnmDrawCmdEventWriteEop(
	    &cmd, GNM_CACHE_FLUSH_AND_INV_TS_EVENT, 0x0000000123456780ULL,
	    GNM_DATA_SEL_SEND_DATA64, 0x1122334455667788ULL
	);

	utasserteq((long long)cmd_dwords_used(&cmd), 6LL);
	utasserteq((long long)PKT3_OPCODE(s_cmdbuf[0]), (long long)PKT3_EVENT_WRITE_EOP);
	utasserteq((long long)PKT_COUNT(s_cmdbuf[0]), 4LL);
	utasserteq(
	    (long long)s_cmdbuf[1],
	    (long long)(EVENT_TYPE(GNM_CACHE_FLUSH_AND_INV_TS_EVENT) | EVENT_INDEX(5))
	);
	utasserteq((long long)s_cmdbuf[2], 0x23456780LL);
	utasserteq(
	    (long long)s_cmdbuf[3],
	    (long long)(0x1 | EOP_DATA_SEL(GNM_DATA_SEL_SEND_DATA64) |
			EOP_INT_SEL(EOP_INT_SEL_SEND_DATA_AFTER_WR_CONFIRM))
	);
	utasserteq((long long)s_cmdbuf[4], 0x55667788LL);
	utasserteq((long long)s_cmdbuf[5], 0x11223344LL);
	return test_success();
}

static TestResult test_event_write_eop_rejects_high_address(void) {
	memset(s_cmdbuf, 0, sizeof(s_cmdbuf));
	GnmCommandBuffer cmd = new_cmdbuf();

	sceGnmDrawCmdEventWriteEop(
	    &cmd, GNM_CACHE_FLUSH_AND_INV_TS_EVENT, 0x0001000000000000ULL,
	    GNM_DATA_SEL_SEND_DATA64, 0
	);

	utasserteq((long long)cmd_dwords_used(&cmd), 0LL);
	return test_success();
}

static TestResult test_event_write_eop_rejects_bad_event(void) {
	memset(s_cmdbuf, 0, sizeof(s_cmdbuf));
	GnmCommandBuffer cmd = new_cmdbuf();

	sceGnmDrawCmdEventWriteEop(
	    &cmd, (GnmEventType)0x40, 0x0000000123456780ULL,
	    GNM_DATA_SEL_SEND_DATA64, 0
	);

	utasserteq((long long)cmd_dwords_used(&cmd), 0LL);
	utasserteq((long long)s_cmdbuf[0], 0LL);
	return test_success();
}

static TestResult test_event_write_eop_rejects_bad_datasel(void) {
	memset(s_cmdbuf, 0, sizeof(s_cmdbuf));
	GnmCommandBuffer cmd = new_cmdbuf();

	sceGnmDrawCmdEventWriteEop(
	    &cmd, GNM_CACHE_FLUSH_AND_INV_TS_EVENT, 0x0000000123456780ULL,
	    (GnmEventDataSel)6, 0
	);

	utasserteq((long long)cmd_dwords_used(&cmd), 0LL);
	utasserteq((long long)s_cmdbuf[0], 0LL);
	return test_success();
}

static TestResult test_event_write_eop_rejects_sys_clock_misalignment(void) {
	memset(s_cmdbuf, 0, sizeof(s_cmdbuf));
	GnmCommandBuffer cmd = new_cmdbuf();

	sceGnmDrawCmdEventWriteEop(
	    &cmd, GNM_CACHE_FLUSH_AND_INV_TS_EVENT, 0x0000000123456784ULL,
	    GNM_DATA_SEL_SEND_SYS_CLOCK, 0
	);

	utasserteq((long long)cmd_dwords_used(&cmd), 0LL);
	utasserteq((long long)s_cmdbuf[0], 0LL);
	return test_success();
}

static TestResult test_waitmem_layout(void) {
	memset(s_cmdbuf, 0, sizeof(s_cmdbuf));
	GnmCommandBuffer cmd = new_cmdbuf();

	sceGnmDrawCmdWaitMem(
	    &cmd, GNM_WAIT_REG_MEM_FUNC_NOT_EQUAL, 0x0000000123456780ULL,
	    0xabcdef01, 0xff00ff00
	);

	utasserteq((long long)cmd_dwords_used(&cmd), 7LL);
	utasserteq((long long)PKT3_OPCODE(s_cmdbuf[0]), (long long)PKT3_WAIT_REG_MEM);
	utasserteq((long long)PKT_COUNT(s_cmdbuf[0]), 5LL);
	utasserteq(
	    (long long)s_cmdbuf[1],
	    (long long)(GNM_WAIT_REG_MEM_FUNC_NOT_EQUAL | WAIT_REG_MEM_MEM_SPACE(1))
	);
	utasserteq((long long)s_cmdbuf[2], 0x23456780LL);
	utasserteq((long long)s_cmdbuf[3], 0x1LL);
	utasserteq((long long)s_cmdbuf[4], 0xabcdef01LL);
	utasserteq((long long)s_cmdbuf[5], 0xff00ff00LL);
	utasserteq((long long)s_cmdbuf[6], 4LL);
	return test_success();
}

static TestResult test_waitmem_rejects_high_address(void) {
	memset(s_cmdbuf, 0, sizeof(s_cmdbuf));
	GnmCommandBuffer cmd = new_cmdbuf();

	sceGnmDrawCmdWaitMem(
	    &cmd, GNM_WAIT_REG_MEM_FUNC_EQUAL, 0x0001000000000000ULL, 0, 0xffffffff
	);

	utasserteq((long long)cmd_dwords_used(&cmd), 0LL);
	utasserteq((long long)s_cmdbuf[0], 0LL);
	return test_success();
}

static TestResult test_waitmem_rejects_bad_func(void) {
	memset(s_cmdbuf, 0, sizeof(s_cmdbuf));
	GnmCommandBuffer cmd = new_cmdbuf();

	sceGnmDrawCmdWaitMem(&cmd, 7, 0x0000000123456780ULL, 0, 0xffffffff);

	utasserteq((long long)cmd_dwords_used(&cmd), 0LL);
	utasserteq((long long)s_cmdbuf[0], 0LL);
	return test_success();
}

static TestResult test_wait_graphics_write_layout(void) {
	memset(s_cmdbuf, 0, sizeof(s_cmdbuf));
	GnmCommandBuffer cmd = new_cmdbuf();

	sceGnmDrawCmdWaitGraphicsWrite(
	    &cmd, GNM_ACQUIRE_TARGET_CB0 | GNM_ACQUIRE_TARGET_DB
	);

	const uint32_t expected_coher =
	    GNM_ACQUIRE_TARGET_CB0 | GNM_ACQUIRE_TARGET_DB |
	    S_0301F0_CB_ACTION_ENA(1) | S_0301F0_DB_ACTION_ENA(1) |
	    S_0301F0_TCL1_VOL_ACTION_ENA(1) |
	    S_0301F0_TC_VOL_ACTION_ENA(1) |
	    S_0301F0_TC_WB_ACTION_ENA(1);

	utasserteq((long long)cmd_dwords_used(&cmd), 7LL);
	utasserteq((long long)PKT3_OPCODE(s_cmdbuf[0]), (long long)PKT3_ACQUIRE_MEM);
	utasserteq((long long)PKT_COUNT(s_cmdbuf[0]), 5LL);
	utasserteq((long long)s_cmdbuf[1], (long long)expected_coher);
	utasserteq((long long)s_cmdbuf[2], 0xffffffffLL);
	utasserteq((long long)s_cmdbuf[3], 0xffLL);
	utasserteq((long long)s_cmdbuf[4], 0LL);
	utasserteq((long long)s_cmdbuf[5], 0LL);
	utasserteq((long long)s_cmdbuf[6], 0xaLL);
	return test_success();
}

static TestResult test_streamout_config_layout(void) {
	memset(s_cmdbuf, 0, sizeof(s_cmdbuf));
	GnmCommandBuffer cmd = new_cmdbuf();

	sceGnmDrawCmdSetStreamOutConfig(&cmd, 0xa, 3, 0x1234);

	utasserteq((long long)cmd_dwords_used(&cmd), 6LL);
	utasserteq((long long)PKT3_OPCODE(s_cmdbuf[0]), (long long)PKT3_SET_CONTEXT_REG);
	utasserteq((long long)PKT_COUNT(s_cmdbuf[0]), 1LL);
	utasserteq(
	    (long long)s_cmdbuf[1],
	    (long long)((R_028B94_VGT_STRMOUT_CONFIG - SI_CONTEXT_REG_OFFSET) >> 2)
	);
	utasserteq(
	    (long long)s_cmdbuf[2],
	    (long long)(S_028B94_RAST_STREAM(3) | 0xa)
	);
	utasserteq((long long)PKT3_OPCODE(s_cmdbuf[3]), (long long)PKT3_SET_CONTEXT_REG);
	utasserteq((long long)PKT_COUNT(s_cmdbuf[3]), 1LL);
	utasserteq(
	    (long long)s_cmdbuf[4],
	    (long long)((R_028B98_VGT_STRMOUT_BUFFER_CONFIG -
			 SI_CONTEXT_REG_OFFSET) >> 2)
	);
	utasserteq((long long)s_cmdbuf[5], 0x1234LL);
	return test_success();
}

static TestResult test_streamout_config_rejects_overflow(void) {
	memset(s_cmdbuf, 0, sizeof(s_cmdbuf));
	GnmCommandBuffer cmd = new_cmdbuf();

	sceGnmDrawCmdSetStreamOutConfig(&cmd, 0x10, 0, 0);
	utasserteq((long long)cmd_dwords_used(&cmd), 0LL);

	cmd = new_cmdbuf();
	sceGnmDrawCmdSetStreamOutConfig(&cmd, 0, 8, 0);
	utasserteq((long long)cmd_dwords_used(&cmd), 0LL);

	cmd = new_cmdbuf();
	sceGnmDrawCmdSetStreamOutConfig(&cmd, 0, 0, 0x10000);
	utasserteq((long long)cmd_dwords_used(&cmd), 0LL);
	utasserteq((long long)s_cmdbuf[0], 0LL);
	return test_success();
}

static TestResult test_driver_wait_flip_done_layout(void) {
	memset(s_cmdbuf, 0, sizeof(s_cmdbuf));
	use_test_label_address(0x0000000123456000ULL);

	int32_t res = sceGnmDriverInsertWaitFlipDone(s_cmdbuf, 7, 9, 3);
	reset_platform_base();

	utasserteq((long long)res, (long long)GNM_ERROR_OK);
	utasserteq((long long)PKT3_OPCODE(s_cmdbuf[0]), (long long)PKT3_WAIT_REG_MEM);
	utasserteq((long long)PKT_COUNT(s_cmdbuf[0]), 5LL);
	utasserteq(
	    (long long)s_cmdbuf[1],
	    (long long)(WAIT_REG_MEM_EQUAL | WAIT_REG_MEM_MEM_SPACE(1))
	);
	utasserteq((long long)s_cmdbuf[2], 0x23456018LL);
	utasserteq((long long)s_cmdbuf[3], 0x1LL);
	utasserteq((long long)s_cmdbuf[4], 0LL);
	utasserteq((long long)s_cmdbuf[5], 0xffffffffLL);
	utasserteq((long long)s_cmdbuf[6], 10LL);
	return test_success();
}

static TestResult test_driver_wait_flip_done_rejects_high_address(void) {
	memset(s_cmdbuf, 0, sizeof(s_cmdbuf));
	use_test_label_address(0x0001000000000000ULL);

	int32_t res = sceGnmDriverInsertWaitFlipDone(s_cmdbuf, 7, 9, 0);
	reset_platform_base();

	utasserteq((long long)res, (long long)GNM_ERROR_CMD_FAILED);
	utasserteq((long long)s_cmdbuf[0], 0LL);
	return test_success();
}

static TestResult test_compute_wait_on_address_layout(void) {
	memset(s_cmdbuf, 0, sizeof(s_cmdbuf));

	int32_t res = sceGnmComputeWaitOnAddress(
	    s_cmdbuf, 7, (uintptr_t)0x0000000123456780ULL, 0xff00ff00,
	    GNM_WAIT_REG_MEM_FUNC_NOT_EQUAL, 0xabcdef01
	);

	utasserteq((long long)res, (long long)GNM_ERROR_OK);
	utasserteq((long long)PKT3_OPCODE(s_cmdbuf[0]), (long long)PKT3_WAIT_REG_MEM);
	utasserteq((long long)PKT_COUNT(s_cmdbuf[0]), 5LL);
	utasserteq(
	    (long long)s_cmdbuf[1],
	    (long long)(GNM_WAIT_REG_MEM_FUNC_NOT_EQUAL | WAIT_REG_MEM_MEM_SPACE(1))
	);
	utasserteq((long long)s_cmdbuf[2], 0x23456780LL);
	utasserteq((long long)s_cmdbuf[3], 0x1LL);
	utasserteq((long long)s_cmdbuf[4], 0xabcdef01LL);
	utasserteq((long long)s_cmdbuf[5], 0xff00ff00LL);
	utasserteq((long long)s_cmdbuf[6], 10LL);
	return test_success();
}

static TestResult test_compute_wait_on_address_rejects_high_address(void) {
	memset(s_cmdbuf, 0, sizeof(s_cmdbuf));

	int32_t res = sceGnmComputeWaitOnAddress(
	    s_cmdbuf, 7, (uintptr_t)0x0001000000000000ULL, 0xffffffff,
	    GNM_WAIT_REG_MEM_FUNC_EQUAL, 0
	);

	utasserteq((long long)res, (long long)GNM_ERROR_CMD_FAILED);
	utasserteq((long long)s_cmdbuf[0], 0LL);
	return test_success();
}

static TestResult test_compute_wait_on_address_rejects_bad_func(void) {
	memset(s_cmdbuf, 0, sizeof(s_cmdbuf));

	int32_t res = sceGnmComputeWaitOnAddress(
	    s_cmdbuf, 7, (uintptr_t)0x0000000123456780ULL, 0xffffffff,
	    7, 0
	);

	utasserteq((long long)res, (long long)GNM_ERROR_CMD_FAILED);
	utasserteq((long long)s_cmdbuf[0], 0LL);
	return test_success();
}

static TestResult test_reset_query_zpass_eop_layout(void) {
	memset(s_cmdbuf, 0, sizeof(s_cmdbuf));
	GnmCommandBuffer cmd = new_cmdbuf();

	sceGnmDrawCmdResetQuery(&cmd, 0x0000000100004000ULL);

	utasserteq((long long)cmd_dwords_used(&cmd), 6LL);
	utasserteq((long long)PKT3_OPCODE(s_cmdbuf[0]), (long long)PKT3_EVENT_WRITE_EOP);
	utasserteq((long long)PKT_COUNT(s_cmdbuf[0]), 4LL);
	utasserteq(
	    (long long)s_cmdbuf[1],
	    (long long)(EVENT_TYPE(PM4_EVENT_TYPE_ZPASS_DONE) | EVENT_INDEX(1))
	);
	utasserteq((long long)s_cmdbuf[2], 0x4000LL);
	utasserteq(
	    (long long)s_cmdbuf[3],
	    (long long)(0x1 | EOP_DATA_SEL(GNM_DATA_SEL_SEND_DATA32) |
			EOP_INT_SEL(EOP_INT_SEL_SEND_DATA_AFTER_WR_CONFIRM))
	);
	utasserteq((long long)s_cmdbuf[4], 0LL);
	utasserteq((long long)s_cmdbuf[5], 0LL);
	return test_success();
}

static TestResult test_depth_target_stencil_layout(void) {
	const GnmDepthRenderTargetCreateInfo z24s8spec = {
	    .width = 128,
	    .height = 64,
	    .pitch = 0,
	    .numslices = 1,

	    .zfmt = GNM_Z_24,
	    .stencilfmt = GNM_STENCIL_8,
	    .tilemodehint = GNM_TM_DEPTH_2D_THIN_64,
	    .mingpumode = GNM_GPU_BASE,
	    .numfragments = 1,
	};
	GnmDepthRenderTarget z24s8 = {0};
	GnmError gerr = sceGnmCreateDepthRenderTarget(&z24s8, &z24s8spec);
	utassert(gerr == GNM_ERROR_OK);

	uint64_t z24s8size = 0;
	uint32_t z24s8align = 0;
	gerr = sceGnmDrtCalcByteSize(&z24s8size, &z24s8align, &z24s8);
	utassert(gerr == GNM_ERROR_OK);
	utassert(z24s8size > 0);
	utassert(z24s8align > 0);

	uint64_t stenciloffset = 0;
	gerr = sceGnmDrtCalcStencilByteOffset(&stenciloffset, &z24s8);
	utassert(gerr == GNM_ERROR_OK);
	utassert(stenciloffset > 0);
	utassert(stenciloffset < z24s8size);
	utassert((stenciloffset & 0xff) == 0);

	void* zbase = (void*)0x10000000;
	void* stencilbase = (uint8_t*)zbase + stenciloffset;
	utassert(sceGnmDrtSetZReadAddress(&z24s8, zbase) == GNM_ERROR_OK);
	utassert(sceGnmDrtSetZWriteAddress(&z24s8, zbase) == GNM_ERROR_OK);
	utassert(
	    sceGnmDrtSetStencilReadAddress(&z24s8, stencilbase) ==
	    GNM_ERROR_OK
	);
	utassert(
	    sceGnmDrtSetStencilWriteAddress(&z24s8, stencilbase) ==
	    GNM_ERROR_OK
	);
	utassert(sceGnmDrtGetZReadAddress(&z24s8) != NULL);
	utassert(sceGnmDrtGetStencilReadAddress(&z24s8) != NULL);

	const GnmDepthRenderTargetCreateInfo stencilspec = {
	    .width = 128,
	    .height = 64,
	    .pitch = 0,
	    .numslices = 1,

	    .zfmt = GNM_Z_INVALID,
	    .stencilfmt = GNM_STENCIL_8,
	    .tilemodehint = GNM_TM_DEPTH_2D_THIN_64,
	    .mingpumode = GNM_GPU_BASE,
	    .numfragments = 1,
	};
	GnmDepthRenderTarget stencilonly = {0};
	gerr = sceGnmCreateDepthRenderTarget(&stencilonly, &stencilspec);
	utassert(gerr == GNM_ERROR_OK);

	uint64_t stencilsize = 0;
	uint32_t stencilalign = 0;
	gerr = sceGnmDrtCalcByteSize(&stencilsize, &stencilalign, &stencilonly);
	utassert(gerr == GNM_ERROR_OK);
	utassert(stencilsize > 0);
	utassert(stencilalign > 0);

	gerr = sceGnmDrtCalcStencilByteOffset(&stenciloffset, &stencilonly);
	utassert(gerr == GNM_ERROR_OK);
	utassert(stenciloffset == 0);
	utassert(sceGnmDrtGetZReadAddress(&stencilonly) == NULL);
	utassert(
	    sceGnmDrtSetZReadAddress(&stencilonly, (void*)0x20000000) ==
	    GNM_ERROR_INVALID_STATE
	);
	utassert(
	    sceGnmDrtSetStencilReadAddress(
		&stencilonly, (void*)0x20000000
	    ) == GNM_ERROR_OK
	);
	utassert(
	    sceGnmDrtSetStencilWriteAddress(
		&stencilonly, (void*)0x20000000
	    ) == GNM_ERROR_OK
	);

	return test_success();
}

static TestResult test_fill_memory_layout(void) {
	uint32_t words[32] = {0};
	GnmCommandBuffer cmd = sceGnmCmdInit(words, sizeof(words), NULL, NULL);
	utassert(sceGnmDrawCmdFillMemory(&cmd, 0x123456780ULL, 0x100, 0xaabbccdd));
	utasserteq(cmd.cmdptr - cmd.beginptr, 7);
	utasserteq(words[0], PKT3(PKT3_DMA_DATA, 5, 0));
	utasserteq(words[2], 0xaabbccdd);
	utasserteq(words[4], 0x23456780);
	utasserteq(words[5], 1);
	utasserteq(G_415_BYTE_COUNT_GFX6(words[6]), 0x100);
	return test_success();
}

static TestResult test_fill_memory_rejects_invalid_args(void) {
	uint32_t words[8] = {0};
	GnmCommandBuffer cmd = sceGnmCmdInit(words, sizeof(words), NULL, NULL);
	utassert(!sceGnmDrawCmdFillMemory(&cmd, 0, 4, 0));
	utassert(!sceGnmDrawCmdFillMemory(&cmd, 0x1001, 4, 0));
	utassert(!sceGnmDrawCmdFillMemory(&cmd, 0x1000, 3, 0));
	utassert(!sceGnmDrawCmdFillMemory(&cmd, 0x1000, 0x200000, 0));
	utasserteq(cmd.cmdptr - cmd.beginptr, 0);
	return test_success();
}

static TestResult test_fill_memory_splits_large_ranges(void) {
	uint32_t words[16] = {0};
	GnmCommandBuffer cmd = sceGnmCmdInit(words, sizeof(words), NULL, NULL);
	utassert(sceGnmDrawCmdFillMemory(&cmd, 0x10000000, 0x200000, 0x10203040));
	utasserteq(cmd.cmdptr - cmd.beginptr, 14);
	utasserteq(G_415_BYTE_COUNT_GFX6(words[6]), 0x1ffffc);
	utasserteq(words[11], 0x101ffffc);
	utasserteq(G_415_BYTE_COUNT_GFX6(words[13]), 4);
	return test_success();
}

static TestResult test_copy_memory_layout(void) {
	uint32_t words[8] = {0};
	GnmCommandBuffer cmd = sceGnmCmdInit(words, sizeof(words), NULL, NULL);
	utassert(sceGnmDrawCmdCopyMemory(
	    &cmd, 0x234567890ULL, 0x123456780ULL, 0x100
	));
	utasserteq(cmd.cmdptr - cmd.beginptr, 7);
	utasserteq(words[0], PKT3(PKT3_DMA_DATA, 5, 0));
	utasserteq(G_500_SRC_SEL(words[1]), V_500_SRC_ADDR);
	utasserteq(words[2], 0x23456780);
	utasserteq(words[3], 1);
	utasserteq(words[4], 0x34567890);
	utasserteq(words[5], 2);
	utasserteq(G_415_BYTE_COUNT_GFX6(words[6]), 0x100);
	return test_success();
}

static TestResult test_copy_memory_rejects_invalid_args(void) {
	uint32_t words[8] = {0};
	GnmCommandBuffer cmd = sceGnmCmdInit(words, sizeof(words), NULL, NULL);
	utassert(!sceGnmDrawCmdCopyMemory(&cmd, 0, 0x2000, 4));
	utassert(!sceGnmDrawCmdCopyMemory(&cmd, 0x1000, 0, 4));
	utassert(!sceGnmDrawCmdCopyMemory(&cmd, 0x1001, 0x2000, 4));
	utassert(!sceGnmDrawCmdCopyMemory(&cmd, 0x1000, 0x2001, 4));
	utassert(!sceGnmDrawCmdCopyMemory(&cmd, 0x1000, 0x2000, 3));
	utasserteq(cmd.cmdptr - cmd.beginptr, 0);
	return test_success();
}

static TestResult test_blend_control_keeps_rop3_enabled(void) {
	memset(s_cmdbuf, 0, sizeof(s_cmdbuf));
	GnmCommandBuffer cmd = new_cmdbuf();
	const GnmBlendControl blend = {
	    .colordstmult = GNM_BLEND_ONE_MINUS_CONSTANT_ALPHA,
	    .colorsrcmult = GNM_BLEND_CONSTANT_ALPHA,
	    .colorfunc = GNM_COMB_DST_PLUS_SRC,
	    .alphadstmult = GNM_BLEND_ZERO,
	    .alphasrcmult = GNM_BLEND_ONE,
	    .alphafunc = GNM_COMB_DST_PLUS_SRC,
	    .blendenabled = true,
	};

	sceGnmDrawCmdSetBlendControl(&cmd, 0, &blend);
	utasserteq(cmd_dwords_used(&cmd), 3);
	utasserteq(PKT3_OPCODE(s_cmdbuf[0]), PKT3_SET_CONTEXT_REG);
	utasserteq(s_cmdbuf[1], (R_028780_CB_BLEND0_CONTROL - SI_CONTEXT_REG_OFFSET) >> 2);
	utasserteq(s_cmdbuf[2], 0x40011413);
	utasserteq(G_028780_DISABLE_ROP3(s_cmdbuf[2]), 0);
	return test_success();
}

int run_tests_drawcmd(void) {
	const TestUnit tests[] = {
	    {test_drawindexauto, "DrawIndexAuto PM4"},
	    {test_drawindex, "DrawIndex PM4"},
	    {test_drawindexauto_predicated, "DrawIndexAuto predicated"},
	    {test_drawcmd_callback_resize, "DrawCmd callback resize"},
	    {test_cmdallocinside_small_size, "CmdAllocInside small size"},
	    {test_setvsshader_driver, "SetVsShader driver"},
	    {test_setpsshader_driver, "SetPsShader driver"},
	    {test_drawinithwstate, "DrawInitDefaultHardwareState350"},
	    {test_resetvgtcontrol, "ResetVgtControl"},
	    {test_setvgtcontrol, "SetVgtControl"},
	    {test_setvgtcontrol_neo, "SetVgtControl Neo"},
	    {test_setvgtcontrol_invalid, "SetVgtControl invalid args"},
	    {test_drawindexoffset, "DrawIndexOffset"},
	    {test_drawindexoffset_rejects_large_size, "DrawIndexOffset rejects large size"},
	    {test_drawindex_rejects_null_indexaddr, "DrawIndex rejects null index address"},
	    {test_drawindirect_rejects_truncated_sgpr_offsets, "DrawIndirect rejects truncated SGPR offsets"},
	    {test_drawindexoffset_neo_slice_bits, "DrawIndexOffset Neo slice bits"},
	    {test_event_write_eop_data64_layout, "EventWriteEop DATA64 layout"},
	    {test_event_write_eop_rejects_high_address, "EventWriteEop rejects high address"},
	    {test_event_write_eop_rejects_bad_event, "EventWriteEop rejects bad event"},
	    {test_event_write_eop_rejects_bad_datasel, "EventWriteEop rejects bad data select"},
	    {test_event_write_eop_rejects_sys_clock_misalignment, "EventWriteEop rejects sys clock misalignment"},
	    {test_waitmem_layout, "WaitMem PM4 layout"},
	    {test_waitmem_rejects_high_address, "WaitMem rejects high address"},
	    {test_waitmem_rejects_bad_func, "WaitMem rejects bad function"},
	    {test_wait_graphics_write_layout, "WaitGraphicsWrite PM4 layout"},
	    {test_streamout_config_layout, "StreamOutConfig PM4 layout"},
	    {test_streamout_config_rejects_overflow, "StreamOutConfig rejects overflow"},
	    {test_driver_wait_flip_done_layout, "Driver wait flip done layout"},
	    {test_driver_wait_flip_done_rejects_high_address, "Driver wait flip done rejects high address"},
	    {test_compute_wait_on_address_layout, "Compute wait on address layout"},
	    {test_compute_wait_on_address_rejects_high_address, "Compute wait on address rejects high address"},
	    {test_compute_wait_on_address_rejects_bad_func, "Compute wait on address rejects bad function"},
	    {test_reset_query_zpass_eop_layout, "ResetQuery ZPASS EOP layout"},
	    {test_depth_target_stencil_layout, "Depth target stencil layout"},
	    {test_fill_memory_layout, "FillMemory DMA_DATA layout"},
	    {test_fill_memory_rejects_invalid_args, "FillMemory invalid args"},
		    {test_fill_memory_splits_large_ranges, "FillMemory multi-packet range"},
		    {test_copy_memory_layout, "CopyMemory DMA_DATA layout"},
	    {test_copy_memory_rejects_invalid_args, "CopyMemory invalid args"},
	    {test_blend_control_keeps_rop3_enabled, "BlendControl preserves ROP3"},
	};
	return test_suite(
	    "drawcmd/PM4", tests, sizeof(tests) / sizeof(tests[0])
	);
}
