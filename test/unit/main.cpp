// Phonometrica engine — test runner.
// Copyright (C) 2019-2026 Julien Eychenne. GPLv3 (see LICENSE).

#include "test_framework.hpp"

#include "runtime/bootstrap.hpp"

#include <cstdio>
#include <exception>

namespace phon_test {

namespace {

struct Node
{
	const char *name;
	TestFn fn;
	Node *next;
};

// Intrusive registry built at static-init time.
Node *g_head = nullptr;

} // namespace

Stats &stats()
{
	static Stats s;
	return s;
}

bool &current_case_failed()
{
	static bool flag = false;
	return flag;
}

void register_case(const char *name, TestFn fn)
{
	// Freed at the end of run_all() so the suite is ASan-leak-clean.
	Node *n = new Node{name, fn, g_head};
	g_head = n;
}

Registrar::Registrar(const char *name, TestFn fn)
{
	register_case(name, fn);
}

int run_all()
{
	// The registry is in reverse registration order; reverse it so cases run
	// roughly in source order (per file link order).
	Node *ordered = nullptr;
	for (Node *n = g_head; n != nullptr;)
	{
		Node *next = n->next;
		n->next = ordered;
		ordered = n;
		n = next;
	}

	for (Node *n = ordered; n != nullptr; n = n->next)
	{
		++stats().cases;
		current_case_failed() = false;
		try
		{
			n->fn();
		}
		catch (const AbortCase &)
		{
			// REQUIRE tripped; failure already recorded.
		}
		catch (const std::exception &e)
		{
			std::fprintf(stderr, "  FAILED: %s threw std::exception: %s\n", n->name, e.what());
			++stats().failures;
			current_case_failed() = true;
		}
		catch (...)
		{
			std::fprintf(stderr, "  FAILED: %s threw unknown exception\n", n->name);
			++stats().failures;
			current_case_failed() = true;
		}
		if (current_case_failed())
		{
			++stats().failed_cases;
			std::fprintf(stderr, "[FAIL] %s\n", n->name);
		}
	}

	// Release the registry.
	for (Node *n = ordered; n != nullptr;)
	{
		Node *next = n->next;
		delete n;
		n = next;
	}
	g_head = nullptr;

	const Stats &s = stats();
	std::fprintf(stderr, "\n%d cases (%d failed), %d checks (%d failed)\n",
	             s.cases, s.failed_cases, s.checks, s.failures);
	return s.failures == 0 ? 0 : 1;
}

} // namespace phon_test

int main()
{
	phonometrica::bootstrap();
	return phon_test::run_all();
}
