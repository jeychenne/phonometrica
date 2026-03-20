/***********************************************************************************************************************
 *                                                                                                                     *
 * Copyright (C) 2019-2026 Julien Eychenne                                                                             *
 *                                                                                                                     *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0. If a copy of the MPL was not   *
 * distributed with this file, You can obtain one at https://mozilla.org/MPL/2.0/.                                     *
 *                                                                                                                     *
 * Created: 04/06/2020                                                                                                 *
 *                                                                                                                     *
 * Purpose: Regex builtin functions.                                                                                   *
 *                                                                                                                     *
 ***********************************************************************************************************************/

#ifndef PHONOMETRICA_FUNC_REGEX_HPP
#define PHONOMETRICA_FUNC_REGEX_HPP

#include <phon/regex.hpp>
#include <phon/runtime.hpp>

namespace phonometrica {

static Variant regex_get_field(Runtime &rt, std::span<Variant> args)
{
	auto &re = cast<Regex>(args[0]);
	auto &key = cast<String>(args[1]);
	if (key == rt.length_string) {
		return re.count();
	}
	else if (key == "pattern") {
		return re.pattern();
	}

	throw error("[Index error] Regex type has no member named \"%\"", key);
}

static Variant regex_new1(Runtime &, std::span<Variant> args)
{
	auto &pattern = cast<String>(args[0]);
	return make_handle<Regex>(pattern);
}

static Variant regex_new2(Runtime &, std::span<Variant> args)
{
	auto &pattern = cast<String>(args[0]);
	auto &flags = cast<String>(args[1]);


	return make_handle<Regex>(pattern, flags);
}

static Variant regex_match1(Runtime &, std::span<Variant> args)
{
	auto &regex = cast<Regex>(args[0]);
	auto &subject = cast<String>(args[1]);


	return regex.match(subject);
}

static Variant regex_match2(Runtime &, std::span<Variant> args)
{
	auto &regex = cast<Regex>(args[0]);
	auto &subject = cast<String>(args[1]);
	intptr_t pos = cast<intptr_t>(args[2]);

	return regex.match(subject, pos);
}

static Variant regex_has_match(Runtime &, std::span<Variant> args)
{
	auto &regex = cast<Regex>(args[0]);
	return regex.has_match();
}

static Variant regex_count(Runtime &, std::span<Variant> args)
{
	auto &regex = cast<Regex>(args[0]);
	return regex.count();
}

static Variant regex_group(Runtime &, std::span<Variant> args)
{
	auto &regex = cast<Regex>(args[0]);
	intptr_t i = cast<intptr_t>(args[1]);
	if (!regex.has_match() || i < 0 || i > regex.count()) {
		throw error("[Index error] Invalid group index in regular expression: %", i);
	}

	return regex.capture(i);
}

static Variant regex_get_start(Runtime &, std::span<Variant> args)
{
	auto &regex = cast<Regex>(args[0]);
	intptr_t i = cast<intptr_t>(args[1]);
	if (!regex.has_match() || i < 0 || i > regex.count()) {
		throw error("[Index error] Invalid group index in regular expression: %", i);
	}

	return regex.capture_start(i);
}

static Variant regex_get_end(Runtime &, std::span<Variant> args)
{
	auto &regex = cast<Regex>(args[0]);
	intptr_t i = cast<intptr_t>(args[1]);
	if (!regex.has_match() || i < 0 || i > regex.count()) {
		throw error("[Index error] Invalid group index in regular expression: %", i);
	}

	return regex.capture_end(i);
}

} // namespace phonometrica

#endif // PHONOMETRICA_FUNC_REGEX_HPP
