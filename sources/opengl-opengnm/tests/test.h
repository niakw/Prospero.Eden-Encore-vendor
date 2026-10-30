#ifndef OPENGNM_TEST_H
#define OPENGNM_TEST_H

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

typedef enum {
	TEST_OK = 0,
	TEST_FAIL,
} TestStatus;

typedef struct {
	TestStatus st;
	char msg[160];
} TestResult;

typedef TestResult (*TestFunc)(void);
typedef struct {
	TestFunc fn;
	const char* name;
} TestUnit;

static inline TestResult test_success(void) {
	TestResult r = {TEST_OK, ""};
	return r;
}

static inline TestResult test_fail(const char* msg) {
	TestResult r = {TEST_FAIL, ""};
	if (msg) {
		size_t i = 0;
		for (; i < sizeof(r.msg) - 1 && msg[i]; i += 1) {
			r.msg[i] = msg[i];
		}
		r.msg[i] = '\0';
	}
	return r;
}

static inline TestResult test_failf(const char* fmt, ...) {
	/* Simple variant without varargs to keep the header dependency-free.
	 * Use test_fail() for formatted messages, or snprintf at call site. */
	(void)fmt;
	return test_fail("test_failf");
}

#define utassert(_expr)                                                       \
	do {                                                                  \
		if (!(_expr)) {                                               \
			TestResult _r = test_success();                       \
			_r.st = TEST_FAIL;                                   \
			snprintf(_r.msg, sizeof(_r.msg),                      \
			         "Assertion \"%s\" failed (%s:%i)",           \
			         #_expr, __FILE__, __LINE__);                 \
			return _r;                                           \
		}                                                             \
	} while (0)

#define utasserteq(_a, _b)                                                    \
	do {                                                                  \
		long long _va = (long long)(_a);                              \
		long long _vb = (long long)(_b);                              \
		if (_va != _vb) {                                             \
			TestResult _r = test_success();                       \
			_r.st = TEST_FAIL;                                   \
			snprintf(_r.msg, sizeof(_r.msg),                      \
			         "Expected %lld == %lld (%s:%i)",             \
			         _va, _vb, __FILE__, __LINE__);               \
			return _r;                                           \
		}                                                             \
	} while (0)

static inline bool test_run(const TestUnit* test, uint32_t* passed) {
	TestResult r = test->fn();
	if (r.st == TEST_OK) {
		printf("  [PASS] %s\n", test->name);
		*passed += 1;
		return true;
	}
	printf("  [FAIL] %s: %s\n", test->name, r.msg);
	return false;
}

static inline int test_suite(
    const char* suitename, const TestUnit* tests, size_t count
) {
	printf("[%s] %zu tests\n", suitename, count);
	uint32_t passed = 0;
	uint32_t failed = 0;
	for (size_t i = 0; i < count; i += 1) {
		if (!test_run(&tests[i], &passed)) {
			failed += 1;
		}
	}
	printf("[%s] %u passed, %u failed\n\n", suitename, passed, failed);
	return failed ? 1 : 0;
}

#endif /* OPENGNM_TEST_H */
