// Phonometrica engine — math standard library (architecture §12).
// Copyright (C) 2019-2026 Julien Eychenne. GPLv3 (see LICENSE).
//
// Ported from the old engine's func_math.hpp against the typed registration API. A
// scalar `double` parameter dispatches on Real, so a function like `sin` accepts an
// Integer or a Float and coerces (matching the old `Number`-typed overloads). abs /
// round / min / max carry a second Integer-typed overload so integer inputs stay
// integers — a direct demonstration of type-based dispatch from C++ registrations.

#include <phon/dispatch/generic.hpp> // register_constant (bare-name PI/E)
#include <phon/lib/lib.hpp>
#include <phon/runtime/native_traits.hpp>
#include <phon/types/atom.hpp> // intern

#include <cmath>
#include <random>

namespace phonometrica {

namespace {

// The old engine's round: half-up, NaN-preserving, sign-preserving zero.
double round_half_up(double x)
{
	if (!std::isfinite(x))
		return std::nan("");
	if (x == 0.0)
		return x;
	return std::floor(x + 0.5);
}

} // namespace

void register_math_lib()
{
	// Transcendental / rounding functions: `double` accepts any Number and returns a Float.
	register_function("sin", [](double x) { return std::sin(x); });
	register_function("cos", [](double x) { return std::cos(x); });
	register_function("tan", [](double x) { return std::tan(x); });
	register_function("asin", [](double x) { return std::asin(x); });
	register_function("acos", [](double x) { return std::acos(x); });
	register_function("atan", [](double x) { return std::atan(x); });
	register_function("atan2", [](double y, double x) { return std::atan2(y, x); });
	register_function("exp", [](double x) { return std::exp(x); });
	register_function("log", [](double x) { return std::log(x); });
	register_function("log2", [](double x) { return std::log2(x); });
	register_function("log10", [](double x) { return std::log10(x); });
	register_function("sqrt", [](double x) { return std::sqrt(x); });
	register_function("ceil", [](double x) { return std::ceil(x); });
	register_function("floor", [](double x) { return std::floor(x); });

	// Integer-preserving overloads: an integer argument selects the Integer method
	// (more specific than Real); a Float argument falls to the double method.
	register_function("abs", [](double x) { return std::fabs(x); });
	register_function("abs", [](int64_t n) { return n < 0 ? -n : n; });
	register_function("round", [](double x) { return round_half_up(x); });
	register_function("round", [](int64_t n) { return n; });
	register_function("round", [](double x, int64_t ndigits) {
		if (!std::isfinite(x) || x == 0.0)
			return x;
		double p = std::pow(10.0, static_cast<double>(ndigits));
		return std::round(x * p) / p;
	});
	register_function("min", [](double a, double b) { return a < b ? a : b; });
	register_function("min", [](int64_t a, int64_t b) { return a < b ? a : b; });
	register_function("max", [](double a, double b) { return a > b ? a : b; });
	register_function("max", [](int64_t a, int64_t b) { return a > b ? a : b; });

	// A uniform draw in [0, 1). Thread-local engine so worker threads never race (TSan).
	register_function("random", [] {
		static thread_local std::mt19937_64 rng(std::random_device{}());
		return std::uniform_real_distribution<double>(0.0, 1.0)(rng);
	});

	// Mathematical constants as bare-name global bindings (`PI`, `E`): the compiler
	// inlines them as constant loads, and a local/module binding of the same name
	// shadows them.
	register_constant(intern("PI"), Value::make(3.141592653589793));
	register_constant(intern("E"), Value::make(2.718281828459045));
}

} // namespace phonometrica
