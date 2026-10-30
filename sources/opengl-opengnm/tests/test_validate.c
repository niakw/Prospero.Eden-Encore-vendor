/* test_validate.c — PM4 validation tests (generic backend)
 *
 * Verifies that the validation stubs return the expected values
 * and that the generic backend handles command buffer validation
 * gracefully (returns OK or appropriate error codes).
 */
#include "test.h"

#include "gnm.h"
#include "gnm_error.h"
#include "gnmdriver.h"
#include "pm4/sid.h"

/* --- ValidateGetVersion: should return a positive version --- */
static TestResult test_validate_getversion(void) {
	int v = sceGnmValidateGetVersion();
	utassert(v >= 0);
	return test_success();
}

/* --- ValidateResetState: should return OK --- */
static TestResult test_validate_resetstate(void) {
	int res = sceGnmValidateResetState();
	utasserteq((long long)res, (long long)GNM_ERROR_OK);
	return test_success();
}

/* --- ValidateOnSubmitEnabled: should return false on generic --- */
static TestResult test_validate_onsubmit(void) {
	bool enabled = sceGnmValidateOnSubmitEnabled();
	utassert(enabled == false);
	return test_success();
}

/* --- ValidateDisableDiagnostics: should return OK --- */
static TestResult test_validate_disablediag(void) {
	int res = sceGnmValidateDisableDiagnostics();
	utasserteq((long long)res, (long long)GNM_ERROR_OK);

	res = sceGnmValidateDisableDiagnostics2();
	utasserteq((long long)res, (long long)GNM_ERROR_OK);
	return test_success();
}

/* --- ValidateCommandBuffers: stub returns 0 on generic --- */
static TestResult test_validate_cmdbuffers(void) {
	int32_t res = sceGnmValidateCommandBuffers();
	/* Generic backend validation stubs return 0 (no errors found) */
	utasserteq((long long)res, 0LL);
	return test_success();
}

/* --- ValidateDrawCommandBuffers: stub returns 0 --- */
static TestResult test_validate_drawcmdbuffers(void) {
	int res = sceGnmValidateDrawCommandBuffers();
	utasserteq((long long)res, 0LL);
	return test_success();
}

/* --- ValidateDispatchCommandBuffers: stub returns 0 --- */
static TestResult test_validate_dispatchcmdbuffers(void) {
	int res = sceGnmValidateDispatchCommandBuffers();
	utasserteq((long long)res, 0LL);
	return test_success();
}

/* --- ValidateGetDiagnostics: stub returns 0 --- */
static TestResult test_validate_getdiagnostics(void) {
	int res = sceGnmValidateGetDiagnostics();
	utasserteq((long long)res, 0LL);
	return test_success();
}

/* --- ValidateGetDiagnosticInfo: stub returns 0 --- */
static TestResult test_validate_getdiaginfo(void) {
	int res = sceGnmValidateGetDiagnosticInfo();
	utasserteq((long long)res, 0LL);
	return test_success();
}

int run_tests_validate(void) {
	const TestUnit tests[] = {
	    {test_validate_getversion, "ValidateGetVersion"},
	    {test_validate_resetstate, "ValidateResetState"},
	    {test_validate_onsubmit, "ValidateOnSubmitEnabled"},
	    {test_validate_disablediag, "ValidateDisableDiagnostics"},
	    {test_validate_cmdbuffers, "ValidateCommandBuffers"},
	    {test_validate_drawcmdbuffers, "ValidateDrawCommandBuffers"},
	    {test_validate_dispatchcmdbuffers, "ValidateDispatchCommandBuffers"},
	    {test_validate_getdiagnostics, "ValidateGetDiagnostics"},
	    {test_validate_getdiaginfo, "ValidateGetDiagnosticInfo"},
	};
	return test_suite(
	    "validate", tests, sizeof(tests) / sizeof(tests[0])
	);
}
