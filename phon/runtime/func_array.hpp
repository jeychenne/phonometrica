/***********************************************************************************************************************
 *                                                                                                                     *
 * Copyright (C) 2019-2026 Julien Eychenne                                                                             *
 *                                                                                                                     *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0. If a copy of the MPL was not   *
 * distributed with this file, You can obtain one at https://mozilla.org/MPL/2.0/.                                     *
 *                                                                                                                     *
 * Created: 06/06/2019                                                                                                 *
 *                                                                                                                     *
 * Purpose: Array builtin functions.                                                                                   *
 *                                                                                                                     *
 ***********************************************************************************************************************/

#ifndef PHONOMETRICA_FUNC_ARRAY_HPP
#define PHONOMETRICA_FUNC_ARRAY_HPP

#include <phon/runtime.hpp>

namespace phonometrica {

static Variant array_get_field(Runtime &rt, std::span<Variant> args)
{
	auto &array = cast<Array<double>>(args[0]);
	auto &key = cast<String>(args[1]);
	if (key == rt.length_string) {
		return array.size();
	}
	else if (key == "ndim") {
		return array.ndim();
	}
	else if (key == "nrow") {
		return array.nrow();
	}
	else if (key == "ncol") {
		return array.ncol();
	}

	throw error("[Index error] String type has no member named \"%\"", key);
}

static Variant array_zeros1(Runtime &, std::span<Variant> args)
{
	intptr_t size = cast<intptr_t>(args[0]);
	return make_handle<Array<double>>(size, 0.0);
}

static Variant array_zeros2(Runtime &, std::span<Variant> args)
{
	intptr_t nrow = cast<intptr_t>(args[0]);
	intptr_t ncol = cast<intptr_t>(args[1]);

	return make_handle<Array<double>>(nrow, ncol, 0.0);
}

static Variant array_ones1(Runtime &, std::span<Variant> args)
{
	intptr_t size = cast<intptr_t>(args[0]);
	return make_handle<Array<double>>(size, 1.0);
}

static Variant array_ones2(Runtime &, std::span<Variant> args)
{
	intptr_t nrow = cast<intptr_t>(args[0]);
	intptr_t ncol = cast<intptr_t>(args[1]);

	return make_handle<Array<double>>(nrow, ncol, 1.0);
}

static Variant array_get_item1(Runtime &rt, std::span<Variant> args)
{
	if (rt.needs_reference()) {
		throw error("[Reference error] Array elements cannot be passed by reference");
	}

	auto &array = cast<Array<double>>(args[0]);
	intptr_t i = cast<intptr_t>(args[1]);
	if (array.ndim() != 1) {
		throw error("[Index error] Only one index provided in array with % dimensions", array.ndim());
	}

	return array.at(i);
}

static Variant array_get_item2(Runtime &rt, std::span<Variant> args)
{
	if (rt.needs_reference()) {
		throw error("[Reference error] Array elements cannot be passed by reference");
	}

	auto &array = cast<Array<double>>(args[0]);
	intptr_t i = cast<intptr_t>(args[1]);
	intptr_t j = cast<intptr_t>(args[2]);
	if (array.ndim() != 2) {
		throw error("[Index error] 2 indexes provided in array with % dimension(s)", array.ndim());
	}

	return array.at(i,j);
}

static Variant array_set_item1(Runtime &, std::span<Variant> args)
{
	auto &array = cast<Array<double>>(args[0]);
	intptr_t i = cast<intptr_t>(args[1]);
	double value = args[2].resolve().get_number();
	if (array.ndim() != 1) {
		throw error("[Index error] Only one index provided in array with % dimensions", array.ndim());
	}
	array.at(i) = value;

	return Variant();
}

static Variant array_set_item2(Runtime &rt, std::span<Variant> args)
{
	if (rt.needs_reference()) {
		throw error("[Reference error] Array elements cannot be passed by reference");
	}

	auto &array = cast<Array<double>>(args[0]);
	intptr_t i = cast<intptr_t>(args[1]);
	intptr_t j = cast<intptr_t>(args[2]);
	double value = args[3].resolve().get_number();
	if (array.ndim() != 2) {
		throw error("[Index error] 2 indexes provided in array with % dimension(s)", array.ndim());
	}
	array.at(i,j) = value;

	return Variant();
}

static Variant array_min(Runtime &, std::span<Variant> args)
{
	auto minimum = (std::numeric_limits<double>::max)();
	auto &array = cast<Array<double>>(args[0]);
	for (intptr_t i = 1; i <= array.size(); i++)
	{
		auto value = array[i];
		if (value < minimum) minimum = value;
	}

	return minimum;
}

static Variant array_max(Runtime &, std::span<Variant> args)
{
	auto maximum = (std::numeric_limits<double>::min)();
	auto &array = cast<Array<double>>(args[0]);
	for (intptr_t i = 1; i <= array.size(); i++)
	{
		auto value = array[i];
		if (value > maximum) maximum = value;
	}

	return maximum;
}

static Variant array_clear(Runtime &, std::span<Variant> args)
{
	auto &array = cast<Array<double>>(args[0]);
	for (intptr_t i = 1; i <= array.size(); i++) {
		array[i] = 0.0;
	}

	return Variant();
}

} // namespace phonometrica

#endif // PHONOMETRICA_FUNC_ARRAY_HPP
