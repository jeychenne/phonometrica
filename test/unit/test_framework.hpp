// Phonometrica engine — minimal single-header test framework.
// Copyright (C) 2019-2026 Julien Eychenne. GPLv3 (see LICENSE).
//
// No third-party dependency (design/architecture.md §0 permits doctest/Catch2,
// but a ~150-line harness keeps the tree fully self-contained and gives us exact
// control over output). Usage:
//
//   TEST_CASE("vector grows") { CHECK(v.size() == 3); REQUIRE(v.empty()); }
//
// CHECK records a failure and continues; REQUIRE aborts the current test case.
// Link exactly one translation unit against PHON_TEST_MAIN (test/unit/main.cpp).

#ifndef PHON_TEST_FRAMEWORK_HPP
#define PHON_TEST_FRAMEWORK_HPP

#include <cstdio>
#include <cstdlib>
#include <exception>

namespace phon_test {

struct AbortCase
{
};

struct Stats
{
	int checks = 0;
	int failures = 0;
	int cases = 0;
	int failed_cases = 0;
};

Stats &stats();
bool &current_case_failed();

using TestFn = void (*)();

struct Registrar
{
	Registrar(const char *name, TestFn fn);
};

void register_case(const char *name, TestFn fn);
int run_all();

inline void report_failure(const char *file, int line, const char *expr, const char *extra)
{
	std::fprintf(stderr, "  FAILED: %s:%d\n    %s\n", file, line, expr);
	if (extra && *extra)
		std::fprintf(stderr, "    %s\n", extra);
	++stats().failures;
	current_case_failed() = true;
}

} // namespace phon_test

#define PHON_TEST_CAT_(a, b) a##b
#define PHON_TEST_CAT(a, b) PHON_TEST_CAT_(a, b)

#define TEST_CASE(name)                                                             \
	static void PHON_TEST_CAT(phon_test_fn_, __LINE__)();                           \
	static ::phon_test::Registrar PHON_TEST_CAT(phon_test_reg_, __LINE__)(          \
	    name, &PHON_TEST_CAT(phon_test_fn_, __LINE__));                             \
	static void PHON_TEST_CAT(phon_test_fn_, __LINE__)()

#define CHECK(cond)                                                                \
	do                                                                             \
	{                                                                              \
		++::phon_test::stats().checks;                                             \
		if (!(cond))                                                               \
			::phon_test::report_failure(__FILE__, __LINE__, #cond, nullptr);       \
	} while (0)

#define CHECK_MESSAGE(cond, msg)                                                   \
	do                                                                             \
	{                                                                              \
		++::phon_test::stats().checks;                                             \
		if (!(cond))                                                               \
			::phon_test::report_failure(__FILE__, __LINE__, #cond, (msg));         \
	} while (0)

#define REQUIRE(cond)                                                              \
	do                                                                             \
	{                                                                              \
		++::phon_test::stats().checks;                                             \
		if (!(cond))                                                               \
		{                                                                          \
			::phon_test::report_failure(__FILE__, __LINE__, #cond, nullptr);       \
			throw ::phon_test::AbortCase{};                                        \
		}                                                                          \
	} while (0)

#endif // PHON_TEST_FRAMEWORK_HPP
