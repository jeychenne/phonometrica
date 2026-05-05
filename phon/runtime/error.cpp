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
 * Created: 20/02/2019                                                                                                 *
 *                                                                                                                     *
 * purpose: see header.                                                                                                *
 *                                                                                                                     *
 ***********************************************************************************************************************/

#include <phon/error.hpp>
#include <phon/string.hpp>
#include <phon/runtime/variant.hpp>

namespace phonometrica {

std::runtime_error error(const String &msg)
{
	return error(msg.data());
}

RuntimeError::RuntimeError(intptr_t line, const String &s) :
	std::runtime_error(s.data()), line(line)
{

}

ScriptException::ScriptException(intptr_t line, const String &msg, Variant value) :
	RuntimeError(line, msg),
	m_value(std::make_unique<Variant>(std::move(value)))
{

}

ScriptException::ScriptException(const ScriptException &other) :
	RuntimeError(other),
	m_value(std::make_unique<Variant>(*other.m_value))
{

}

ScriptException::ScriptException(ScriptException &&other) noexcept :
	RuntimeError(other),
	m_value(std::move(other.m_value))
{

}

ScriptException::~ScriptException() = default;

const Variant &ScriptException::value() const
{
	return *m_value;
}

Variant ScriptException::take_value()
{
	return std::move(*m_value);
}

} // namespace phonometrica
