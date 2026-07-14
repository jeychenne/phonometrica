/***********************************************************************************************************************
 *                                                                                                                     *
 * Copyright (C) 2019-2026 Julien Eychenne                                                                             *
 *                                                                                                                     *
 * This program is free software: you can redistribute it and/or modify it under the terms of the GNU General Public   *
 * License as published by the Free Software Foundation, either version 3 of the License, or (at your option) any      *
 * later version.                                                                                                      *
 *                                                                                                                     *
 * This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied  *
 * warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for more       *
 * details.                                                                                                            *
 *                                                                                                                     *
 * You should have received a copy of the GNU General Public License along with this program. If not, see              *
 * <http://www.gnu.org/licenses/>.                                                                                     *
 *                                                                                                                     *
 * Created: 04/06/2020                                                                                                 *
 *                                                                                                                     *
 * Purpose: Regex builtin functions. The script API is stateful (match, then read groups), so the last match is        *
 * stored in the boxed ScriptRegex next to the compiled pattern; the C++ Regex class itself is stateless.              *
 *                                                                                                                     *
 ***********************************************************************************************************************/

#ifndef PHONOMETRICA_FUNC_REGEX_HPP
#define PHONOMETRICA_FUNC_REGEX_HPP

#include <phon/runtime/script_regex.hpp>
#include <phon/runtime.hpp>

namespace phonometrica {

static Variant regex_get_field(Runtime &rt, std::span<Variant> args)
{
	auto &re = cast<ScriptRegex>(args[0]);
	auto &key = cast<String>(args[1]);
	if (key == rt.length_string) {
		return re.last.count();
	}
	else if (key == "pattern") {
		return re.re.pattern();
	}

	throw error("[Index error] Regex type has no member named \"%\"", key);
}

static Variant regex_new1(Runtime &, std::span<Variant> args)
{
	auto &pattern = cast<String>(args[0]);
	return make_handle<ScriptRegex>(pattern);
}

static Variant regex_new2(Runtime &, std::span<Variant> args)
{
	auto &pattern = cast<String>(args[0]);
	auto &flags = cast<String>(args[1]);


	return make_handle<ScriptRegex>(pattern, flags);
}

static Variant regex_match1(Runtime &, std::span<Variant> args)
{
	auto &regex = cast<ScriptRegex>(args[0]);
	auto &subject = cast<String>(args[1]);
	regex.last = regex.re.match(subject);

	return regex.last.has_match();
}

static Variant regex_match2(Runtime &, std::span<Variant> args)
{
	auto &regex = cast<ScriptRegex>(args[0]);
	auto &subject = cast<String>(args[1]);
	intptr_t pos = cast<intptr_t>(args[2]);

	// The starting position is a 1-based character index, converted by the String itself (strings
	// are 1-based at the native level too); an empty subject never matches.
	if (subject.empty()) {
		regex.last = Match();
	}
	else {
		regex.last = regex.re.match(subject, subject.index_to_iter(pos));
	}

	return regex.last.has_match();
}

static Variant regex_has_match(Runtime &, std::span<Variant> args)
{
	auto &regex = cast<ScriptRegex>(args[0]);
	return regex.last.has_match();
}

static Variant regex_count(Runtime &, std::span<Variant> args)
{
	auto &regex = cast<ScriptRegex>(args[0]);
	return regex.last.count();
}

static Variant regex_group(Runtime &, std::span<Variant> args)
{
	auto &regex = cast<ScriptRegex>(args[0]);
	intptr_t i = cast<intptr_t>(args[1]);
	if (!regex.last.has_match() || i < 0 || i > regex.last.count()) {
		throw error("[Index error] Invalid group index in regular expression: %", i);
	}

	return regex.last.capture(i);
}

static Variant regex_get_start(Runtime &, std::span<Variant> args)
{
	auto &regex = cast<ScriptRegex>(args[0]);
	intptr_t i = cast<intptr_t>(args[1]);
	if (!regex.last.has_match() || i < 0 || i > regex.last.count()) {
		throw error("[Index error] Invalid group index in regular expression: %", i);
	}

	return regex.last.capture_start(i);
}

static Variant regex_get_end(Runtime &, std::span<Variant> args)
{
	auto &regex = cast<ScriptRegex>(args[0]);
	intptr_t i = cast<intptr_t>(args[1]);
	if (!regex.last.has_match() || i < 0 || i > regex.last.count()) {
		throw error("[Index error] Invalid group index in regular expression: %", i);
	}

	return regex.last.capture_end(i);
}

} // namespace phonometrica

#endif // PHONOMETRICA_FUNC_REGEX_HPP
