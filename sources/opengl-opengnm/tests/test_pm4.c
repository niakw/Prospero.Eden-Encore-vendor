/* test_pm4.c — PM4 decoder/formatter regression tests */
#include "test.h"

#include <string.h>

#include "pm4/pm4.h"
#include "pm4/amdgfxregs.h"
#include "pm4/sid.h"

static TestResult test_dma_data_decode_write_confirm_enabled(void) {
	uint32_t cmdbuf[] = {
	    PKT3(PKT3_DMA_DATA, 5, 0),
	    S_500_SRC_SEL(V_500_DATA) | S_500_DST_SEL(V_500_DST_ADDR) |
		S_500_CP_SYNC(1),
	    0xaabbccddu,
	    0,
	    0x23456780u,
	    0x1u,
	    S_415_BYTE_COUNT_GFX6(0x123),
	};
	Pm4Decoder dec = {0};
	Pm4Packet pkt = {0};
	Pm4Error err = pm4DecoderInit(&dec, cmdbuf, sizeof(cmdbuf));
	utasserteq((long long)err, (long long)PM4_ERR_OK);

	err = pm4DecodePacket(&dec, &pkt);
	utasserteq((long long)err, (long long)PM4_ERR_OK);
	utasserteq((long long)pkt.type, (long long)PM4_TYPE_3);
	utasserteq((long long)pkt.pkt3.opcode, (long long)PKT3_DMA_DATA);
	utasserteq((long long)pkt.pkt3.dma_data.src_sel, (long long)V_500_DATA);
	utasserteq(
	    (long long)pkt.pkt3.dma_data.dst_sel, (long long)V_500_DST_ADDR
	);
	utasserteq((long long)pkt.pkt3.dma_data.src_va, 0xaabbccddLL);
	utasserteq((long long)pkt.pkt3.dma_data.dst_va, 0x0000000123456780LL);
	utasserteq((long long)pkt.pkt3.dma_data.length, 0x123LL);
	utassert(pkt.pkt3.dma_data.wr_confirm);
	return test_success();
}

static TestResult test_dma_data_decode_write_confirm_disabled(void) {
	uint32_t cmdbuf[] = {
	    PKT3(PKT3_DMA_DATA, 5, 0),
	    S_500_SRC_SEL(V_500_SRC_ADDR) | S_500_DST_SEL(V_500_DST_ADDR_TC_L2),
	    0x10000000u,
	    0x2u,
	    0x20000000u,
	    0x3u,
	    S_415_BYTE_COUNT_GFX6(0x40) | S_415_DISABLE_WR_CONFIRM_GFX6(1),
	};
	Pm4Decoder dec = {0};
	Pm4Packet pkt = {0};
	Pm4Error err = pm4DecoderInit(&dec, cmdbuf, sizeof(cmdbuf));
	utasserteq((long long)err, (long long)PM4_ERR_OK);

	err = pm4DecodePacket(&dec, &pkt);
	utasserteq((long long)err, (long long)PM4_ERR_OK);
	utasserteq((long long)pkt.pkt3.dma_data.src_sel, (long long)V_500_SRC_ADDR);
	utasserteq(
	    (long long)pkt.pkt3.dma_data.dst_sel,
	    (long long)V_500_DST_ADDR_TC_L2
	);
	utasserteq((long long)pkt.pkt3.dma_data.src_va, 0x0000000210000000LL);
	utasserteq((long long)pkt.pkt3.dma_data.dst_va, 0x0000000320000000LL);
	utasserteq((long long)pkt.pkt3.dma_data.length, 0x40LL);
	utassert(!pkt.pkt3.dma_data.wr_confirm);
	return test_success();
}

int run_tests_pm4(void) {
	const TestUnit tests[] = {
	    {test_dma_data_decode_write_confirm_enabled,
	     "DMA_DATA decode write confirm enabled"},
	    {test_dma_data_decode_write_confirm_disabled,
	     "DMA_DATA decode write confirm disabled"},
	};
	return test_suite("pm4", tests, sizeof(tests) / sizeof(tests[0]));
}
