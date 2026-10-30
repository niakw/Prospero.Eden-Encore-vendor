#include "test.h"

#include <string.h>

#include "gnm.h"
#include "gnm_helpers.h"

static TestResult test_helpers_direct_memory(void) {
	GnmDirectMemory memory;
	GnmError err = sceGnmDirectMemoryAllocate(
	    &memory, 4097, 256, GNM_DIRECT_MEMORY_TYPE_WC_GARLIC,
	    GNM_PROT_CPU_GPU_RW
	);
	utasserteq((long long)err, (long long)GNM_ERROR_OK);
	utassert(memory.mapped != NULL);
	utassert((memory.size & 255u) == 0);
	utassert(((uintptr_t)memory.mapped & 255u) == 0);

	sceGnmDirectMemoryRelease(&memory);
	utassert(memory.mapped == NULL);
	utasserteq((long long)memory.size, 0LL);
	return test_success();
}

static TestResult test_helpers_videoout_layout(void) {
	GnmVideoOutCreateInfo info;
	sceGnmVideoOutInitDefaultCreateInfo(&info, 1280, 720);
	utasserteq((long long)info.width, 1280LL);
	utasserteq((long long)info.height, 720LL);
	utasserteq((long long)info.pitch, 1280LL);
	utasserteq((long long)info.numbuffers, 2LL);

	uint64_t buffersize = 0;
	uint64_t bufferstride = 0;
	GnmError err =
	    sceGnmVideoOutCalcBufferLayout(&info, &buffersize, &bufferstride);
	utasserteq((long long)err, (long long)GNM_ERROR_OK);
	utasserteq((long long)buffersize, 1280LL * 720LL * 4LL);
	utassert(bufferstride >= buffersize);
	utassert((bufferstride & (GNM_VIDEO_OUT_MEMORY_ALIGNMENT - 1)) == 0);

	info.pitch = UINT32_MAX;
	info.height = UINT32_MAX;
	info.bytesperpixel = UINT32_MAX;
	err = sceGnmVideoOutCalcBufferLayout(&info, NULL, NULL);
	utasserteq((long long)err, (long long)GNM_ERROR_OVERFLOW);
	return test_success();
}

static TestResult test_helpers_videoout_invalid_create_info(void) {
	GnmVideoOut videoout;
	GnmVideoOutCreateInfo info;
	sceGnmVideoOutInitDefaultCreateInfo(&info, 1280, 720);
	info.numbuffers = 0;

	memset(&videoout, 0xa5, sizeof(videoout));
	GnmError err = sceGnmVideoOutOpen(&videoout, &info);
	utasserteq((long long)err, (long long)GNM_ERROR_INVALID_ARGS);
	utasserteq((long long)videoout.handle, -1LL);
	utasserteq((long long)videoout.last_error_stage, 1LL);
	utasserteq((long long)videoout.last_error_code,
	    (long long)GNM_ERROR_INVALID_ARGS);

	memset(&videoout, 0xa5, sizeof(videoout));
	err = sceGnmVideoOutOpen(&videoout, NULL);
	utasserteq((long long)err, (long long)GNM_ERROR_INVALID_ARGS);
	utasserteq((long long)videoout.handle, -1LL);
	utasserteq((long long)videoout.last_error_stage, 1LL);
	utasserteq((long long)videoout.last_error_code,
	    (long long)GNM_ERROR_INVALID_ARGS);
	return test_success();
}

static TestResult test_helpers_resource_setup(void) {
	GnmTexture tex;
	uint64_t texsize = 0;
	uint32_t texalign = 0;
	GnmError err = sceGnmTexCreate2d(
	    &tex, NULL, GNM_FMT_R8G8B8A8_UNORM, 128, 64, 1,
	    GNM_TM_THIN_1D_THIN, GNM_GPU_BASE, &texsize, &texalign
	);
	utasserteq((long long)err, (long long)GNM_ERROR_OK);
	utasserteq((long long)sceGnmTexGetWidth(&tex), 128LL);
	utasserteq((long long)sceGnmTexGetHeight(&tex), 64LL);
	utassert(texsize > 0);
	utassert(texalign > 0);

	GnmRenderTarget rt;
	uint64_t rtsize = 0;
	uint32_t rtalign = 0;
	err = sceGnmRtCreateColorTarget(
	    &rt, NULL, GNM_FMT_R8G8B8A8_UNORM, 128, 64, 1, 1, 1,
	    GNM_TM_DISPLAY_LINEAR_ALIGNED, GNM_GPU_BASE, &rtsize, &rtalign
	);
	utasserteq((long long)err, (long long)GNM_ERROR_OK);
	utassert(sceGnmRtGetPitch(&rt) >= 128);
	utasserteq((long long)rt.info.simple_float, 1LL);
	utasserteq((long long)((rt.info.asuint >> 17) & 1), 1LL);
	utassert(rtsize > 0);
	utassert(rtalign > 0);
	return test_success();
}

static TestResult test_helpers_shader_metadata(void) {
	enum {
		kHeaderSize = sizeof(GnmShaderFileHeader),
		kStageSize = sizeof(GnmVsShader) + sizeof(GnmInputUsageSlot),
		kCodeSize = 16,
		kBlobSize = kHeaderSize + kStageSize + kCodeSize,
	};
	uint8_t blob[kBlobSize];
	memset(blob, 0, sizeof(blob));

	GnmShaderFileHeader* header = (GnmShaderFileHeader*)blob;
	header->magic = GNM_SHADER_FILE_HEADER_ID;
	header->vermajor = 1;
	header->verminor = 0;
	header->type = GNM_SHADER_VERTEX;
	header->headersizedwords = kStageSize / 4;
	header->targetgpumodes = GNM_TARGETGPUMODE_BASE;

	GnmVsShader* vs = (GnmVsShader*)(blob + sizeof(GnmShaderFileHeader));
	vs->common.shadersize = kCodeSize;
	vs->common.numinputusageslots = 1;
	vs->numinputsemantics = 0;
	vs->numexportsemantics = 0;
	vs->registers.spishaderpgmlovs = kStageSize;

	GnmShaderMetadata metadata;
	GnmError err =
	    sceGnmShaderBinaryGetMetadata(blob, sizeof(blob), &metadata);
	utasserteq((long long)err, (long long)GNM_ERROR_OK);
	utasserteq((long long)metadata.type, (long long)GNM_SHADER_VERTEX);
	utasserteq((long long)metadata.numinputusageslots, 1LL);
	utasserteq((long long)metadata.shadercodesize, kCodeSize);
	utassert(metadata.shadercode == blob + kHeaderSize + kStageSize);
	return test_success();
}

static TestResult test_helpers_command_buffer_validation(void) {
	uint32_t words[16];
	GnmCommandBuffer cmd = sceGnmCmdInit(words, sizeof(words), NULL, NULL);
	GnmCommandBufferValidationInfo info;

	GnmError err = sceGnmCmdValidate(&cmd, &info);
	utasserteq((long long)err, (long long)GNM_ERROR_OK);
	utasserteq((long long)info.capacitydwords, 16LL);
	utasserteq((long long)info.useddwords, 0LL);
	utasserteq((long long)info.remainingdwords, 16LL);

	cmd.cmdptr = cmd.endptr + 1;
	err = sceGnmCmdValidate(&cmd, &info);
	utasserteq((long long)err, (long long)GNM_ERROR_OVERFLOW);
	utassert(info.message != NULL);
	return test_success();
}

static TestResult test_helpers_wrapped_shader_metadata(void) {
	enum {
		kWrapperSize = 0x24,
		kHeaderSize = sizeof(GnmShaderFileHeader),
		kStageSize = sizeof(GnmPsShader),
		kCodeSize = 16,
		kInfoSize = sizeof(GnmShaderBinaryInfo),
		kContainerCodeSize = kHeaderSize + kStageSize + kCodeSize + kInfoSize,
		kBlobSize = kWrapperSize + kContainerCodeSize,
	};
	uint8_t blob[kBlobSize];
	memset(blob, 0, sizeof(blob));
	GnmShaderFileHeader* header = (GnmShaderFileHeader*)(blob + kWrapperSize);
	header->magic = GNM_SHADER_FILE_HEADER_ID;
	header->type = GNM_SHADER_PIXEL;
	header->headersizedwords = kStageSize / 4;
	header->targetgpumodes = GNM_TARGETGPUMODE_BASE;
	GnmPsShader* ps = (GnmPsShader*)(blob + kWrapperSize + kHeaderSize);
	ps->common.shadersize = kCodeSize;
	ps->registers.spishaderpgmlops = kStageSize;
	memcpy(blob + 0x10, &(uint32_t){kContainerCodeSize}, sizeof(uint32_t));
	GnmShaderBinaryInfo* info =
	    (GnmShaderBinaryInfo*)(blob + kBlobSize - kInfoSize);
	info->length = kCodeSize;
	GnmShaderMetadata metadata;
	GnmError err = sceGnmShaderBinaryGetMetadata(blob, sizeof(blob), &metadata);
	utasserteq((long long)err, (long long)GNM_ERROR_OK);
	utasserteq((long long)metadata.type, (long long)GNM_SHADER_PIXEL);
	utassert(metadata.shadercode == blob + kWrapperSize + kHeaderSize + kStageSize);
	return test_success();
}

static TestResult test_helpers_zero_input_fetch_shader(void) {
	GnmVsStageRegisters registers;
	memset(&registers, 0, sizeof(registers));
	GnmFetchShaderCreateInfo info;
	memset(&info, 0, sizeof(info));
	info.regs = &registers;

	uint32_t size = 0;
	GnmError err = sceGnmFetchShaderCalcSize(&size, &info);
	utasserteq((long long)err, (long long)GNM_ERROR_OK);
	utasserteq((long long)size, 12LL);

	uint32_t code[3] = {0};
	GnmFetchShaderResults results;
	memset(&results, 0, sizeof(results));
	err = sceGnmCreateFetchShader(code, sizeof(code), &info, &results);
	utasserteq((long long)err, (long long)GNM_ERROR_OK);
	utasserteq((long long)code[2], 0LL);
	return test_success();
}

int run_tests_helpers(void) {
	const TestUnit tests[] = {
	    {test_helpers_direct_memory, "DirectMemory helper"},
	    {test_helpers_videoout_layout, "VideoOut layout helper"},
	    {test_helpers_videoout_invalid_create_info,
	     "VideoOut invalid create-info diagnostics"},
	    {test_helpers_resource_setup, "Resource setup helpers"},
	    {test_helpers_shader_metadata, "Shader metadata helper"},
		    {test_helpers_wrapped_shader_metadata, "Wrapped shader metadata helper"},
		    {test_helpers_zero_input_fetch_shader, "Zero-input fetch shader"},
	    {test_helpers_command_buffer_validation, "Command buffer validation"},
	};
	return test_suite(
	    "helpers", tests, sizeof(tests) / sizeof(tests[0])
	);
}
