// Phonometrica engine — benchmark harness (architecture §14).
// Copyright (C) 2019-2026 Julien Eychenne. GPLv3 (see LICENSE).
//
// Runs each `bench/scripts/<name>.phon` a few times on a fresh Runtime and reports the
// best (min) and median wall-clock time — the standard microbenchmark set from
// architecture §14 (call overhead, arithmetic loops, string building, map churn,
// dispatch, array kernels). A fresh Runtime per run isolates timings from module-state
// carryover; the process-global registry is initialized once (call_once).
//
// Usage:
//   phon_bench [--runs N] [name ...]
// With no names, runs the whole suite in a fixed order. `--runs` sets the repeat count
// (default 5). Exit code is nonzero if any benchmark fails to run (so it is CI-safe).

#include <phon/engine/runtime/runtime.hpp>
#include <phon/engine/types/string.hpp>
#include <phon/engine/compile/diagnostic.hpp> // SyntaxError

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

using namespace phonometrica;

namespace {

// The suite in a stable order; each name maps to bench/scripts/<name>.phon.
const char *SUITE[] = {"fib", "loops", "strings", "maps", "dispatch", "arrays"};

std::string read_file(const std::string &path, bool &ok)
{
	std::ifstream in(path, std::ios::binary);
	if (!in)
	{
		ok = false;
		return {};
	}
	ok = true;
	std::ostringstream ss;
	ss << in.rdbuf();
	return ss.str();
}

// Run one benchmark `runs` times, printing best + median in milliseconds. Returns false
// if the script is missing or throws.
bool run_one(const std::string &dir, const char *name, int runs)
{
	bool ok = false;
	std::string code = read_file(dir + "/" + name + ".phon", ok);
	if (!ok)
	{
		std::fprintf(stderr, "  %-10s  MISSING (%s/%s.phon)\n", name, dir.c_str(), name);
		return false;
	}

	std::vector<double> ms;
	ms.reserve(static_cast<size_t>(runs));
	for (int r = 0; r < runs; ++r)
	{
		Runtime rt; // fresh session per run: no module-state carryover
		String src(code);
		auto t0 = std::chrono::steady_clock::now();
		try
		{
			rt.do_string(src);
		}
		catch (const SyntaxError &e)
		{
			std::fprintf(stderr, "  %-10s  SYNTAX ERROR: %s (line %d)\n", name, e.what(), (int) e.line);
			return false;
		}
		catch (const RuntimeError &e)
		{
			std::fprintf(stderr, "  %-10s  RUNTIME ERROR: %s (line %d)\n", name,
			             std::string(e.message.data(), (size_t) e.message.size()).c_str(), (int) e.line);
			return false;
		}
		auto t1 = std::chrono::steady_clock::now();
		ms.push_back(std::chrono::duration<double, std::milli>(t1 - t0).count());
	}

	std::sort(ms.begin(), ms.end());
	double best = ms.front();
	double median = ms[ms.size() / 2];
	std::printf("  %-10s  best %8.1f ms   median %8.1f ms   (%d runs)\n", name, best, median, runs);
	return true;
}

} // namespace

int main(int argc, char **argv)
{
	int runs = 5;
	std::vector<const char *> names;
	for (int i = 1; i < argc; ++i)
	{
		if (std::strcmp(argv[i], "--runs") == 0 && i + 1 < argc)
			runs = std::max(1, std::atoi(argv[++i]));
		else
			names.push_back(argv[i]);
	}

	// Locate the scripts relative to this source file (set by CMake), falling back to a
	// path relative to the working directory.
	std::string dir =
#ifdef PHON_BENCH_DIR
	    PHON_BENCH_DIR;
#else
	    "bench/scripts";
#endif

	init_runtime();
	std::printf("phon benchmark suite (%d runs each)\n", runs);

	bool all_ok = true;
	if (names.empty())
		for (const char *n : SUITE)
			all_ok &= run_one(dir, n, runs);
	else
		for (const char *n : names)
			all_ok &= run_one(dir, n, runs);

	return all_ok ? 0 : 1;
}
