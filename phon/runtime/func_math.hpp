/***********************************************************************************************************************
 *                                                                                                                     *
 * Copyright (C) 2019-2026 Julien Eychenne                                                                             *
 *                                                                                                                     *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0. If a copy of the MPL was not   *
 * distributed with this file, You can obtain one at https://mozilla.org/MPL/2.0/.                                     *
 *                                                                                                                     *
 * Created: 06/06/2020                                                                                                 *
 *                                                                                                                     *
 * Purpose: Math builtin functions.                                                                                    *
 *                                                                                                                     *
 ***********************************************************************************************************************/

#ifndef PHONOMETRICA_FUNC_MATH_HPP
#define PHONOMETRICA_FUNC_MATH_HPP

#include <cmath>
#include <phon/runtime.hpp>

namespace phonometrica {

static Variant math_abs(Runtime &, std::span<Variant> args)
{
	auto &v = args[0].resolve();
	if (check_type<double>(v)) {
		return std::fabs(raw_cast<double>(v));		
	}
	else {
		return std::abs(cast<intptr_t>(v));
	}

}

template<double(*f)(double)>
Variant math_array_func(Runtime &, std::span<Variant> args)
{
	auto &array = cast<Array<double>>(args[0]);
	Array<double> result(array.nrow(), array.ncol(), 0.0);
	for (intptr_t i = 1; i <= array.size(); i++) {
		result[i] = f(array[i]);
	}

	return make_handle<Array<double>>(std::move(result));
}

static Variant math_acos(Runtime &, std::span<Variant> args)
{
	auto n = args[0].resolve().get_number();
	return std::acos(n);
}

static Variant math_asin(Runtime &, std::span<Variant> args)
{
	auto n = args[0].resolve().get_number();
	return std::asin(n);
}

static Variant math_atan(Runtime &, std::span<Variant> args)
{
	auto n = args[0].resolve().get_number();
	return std::atan(n);
}

static Variant math_atan2(Runtime &, std::span<Variant> args)
{
	auto y = args[0].resolve().get_number();
	auto x = args[1].resolve().get_number();

	return std::atan2(y, x);
}

static Variant math_ceil(Runtime &, std::span<Variant> args)
{
	auto n = args[0].resolve().get_number();
	return std::ceil(n);
}

static Variant math_cos(Runtime &, std::span<Variant> args)
{
	auto n = args[0].resolve().get_number();
	return std::cos(n);
}

static Variant math_exp(Runtime &, std::span<Variant> args)
{
	auto n = args[0].resolve().get_number();
	return std::exp(n);
}

static Variant math_floor(Runtime &, std::span<Variant> args)
{
	auto n = args[0].resolve().get_number();
	return std::floor(n);
}

static Variant math_log(Runtime &, std::span<Variant> args)
{
	auto n = args[0].resolve().get_number();
	return std::log(n);
}

static Variant math_log10(Runtime &, std::span<Variant> args)
{
	auto n = args[0].resolve().get_number();
	return std::log10(n);
}

static Variant math_log2(Runtime &, std::span<Variant> args)
{
	auto n = args[0].resolve().get_number();
	return std::log2(n);
}

static Variant math_max(Runtime &, std::span<Variant> args)
{
	auto x = args[0].resolve().get_number();
	auto y = args[1].resolve().get_number();

	return (std::max)(x, y);
}

static Variant math_max2(Runtime &, std::span<Variant> args)
{
	auto x = cast<intptr_t>(args[0]);
	auto y = cast<intptr_t>(args[1]);

	return (std::max)(x, y);
}

static Variant math_min(Runtime &, std::span<Variant> args)
{
	auto x = args[0].resolve().get_number();
	auto y = args[1].resolve().get_number();

	return (std::min)(x, y);
}

static Variant math_min2(Runtime &, std::span<Variant> args)
{
	auto x = cast<intptr_t>(args[0]);
	auto y = cast<intptr_t>(args[1]);

	return (std::min)(x, y);
}

static Variant math_random(Runtime &, std::span<Variant>)
{
	return double(std::rand()) / RAND_MAX;
}

static double round_float(double x)
{
	if (!std::isfinite(x)) return std::nan("");
	if (x == 0) return x;

	return std::floor(x + 0.5);
}

static Variant math_round(Runtime &, std::span<Variant> args)
{
	auto &v = args[0].resolve();

	if (check_type<intptr_t>(v)) {
		return cast<intptr_t>(v);
	}

	return round_float(cast<double>(v));
}

static Variant math_roundn(Runtime &, std::span<Variant> args)
{
	long double x = args[0].resolve().get_number();
	double n = args[1].resolve().get_number();
	double p = pow(10, n);
	if (!std::isfinite(x)) return std::nan("");
	if (x == 0) return x;

	return double(std::round(x * p) / p);
}

static Variant math_sin(Runtime &, std::span<Variant> args)
{
	auto n = args[0].resolve().get_number();
	return std::sin(n);
}

static Variant math_sqrt(Runtime &, std::span<Variant> args)
{
	auto n = args[0].resolve().get_number();
	return std::sqrt(n);
}

static Variant math_tan(Runtime &, std::span<Variant> args)
{
	auto n = args[0].resolve().get_number();
	return std::tan(n);
}

} // namespace phonometrica

#endif // PHONOMETRICA_FUNC_MATH_HPP
