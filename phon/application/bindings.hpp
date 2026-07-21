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
 * Created: 21/07/2026                                                                                                 *
 *                                                                                                                     *
 * Purpose: shared support for the app's native bindings on the NEW engine (roadmap A3). Conventions follow            *
 * stats_host/bindings.cpp: natives that can throw run their body through `guarded` (a domain-layer std::exception     *
 * becomes a catchable script error via Isolate::raise; RuntimeError passes through untouched); script indices         *
 * convert through phon/index_conversion.hpp at the binding.                                                           *
 *                                                                                                                     *
 ***********************************************************************************************************************/

#ifndef PHONOMETRICA_APPLICATION_BINDINGS_HPP
#define PHONOMETRICA_APPLICATION_BINDINGS_HPP

#include <exception>
#include <phon/runtime.hpp>
#include <phon/index_conversion.hpp>

namespace phonometrica::bindings {

// Convert a domain-layer exception into a catchable script error. (The engine only
// auto-converts RuntimeError/ScriptError; a plain std::runtime_error would abort the
// script run without a script-side catch.)
[[noreturn]] inline void raise_from(Isolate &iso, const std::exception &e)
{
	iso.raise(String(e.what()), 0);
}

// Run a native body, converting std::exception to a script error.
template<typename F>
auto guarded(Isolate &iso, F &&f) -> decltype(f())
{
	try
	{
		return f();
	}
	catch (RuntimeError &)
	{
		throw; // already a script error (e.g. a nested raise)
	}
	catch (std::exception &e)
	{
		raise_from(iso, e);
	}
}

// Build a script List from the elements of `items`, boxing each with Variant::make.
template<typename T>
List make_list(const Array<T> &items)
{
	List out;
	for (auto &x : items) {
		out.append(Variant::make(x));
	}
	return out;
}

// Analysis-layer Array<double> -> script numeric array (the class named "Array").
// Both sides are column-major, so a 2-D shape copies element-for-element.
inline NumArray to_numarray(const Array<double> &a)
{
	NumArray out = (a.ndim() == 2) ? NumArray::make_2d(a.nrow(), a.ncol())
	                               : NumArray::make_1d(a.size());
	double *d = out.detach();
	for (intptr_t i = 0; i < a.size(); ++i) {
		d[i] = a[i];
	}
	return out;
}

// Script numeric array -> analysis-layer Array<double> (compacts the view;
// preserves a 2-D shape, column-major on both sides).
inline Array<double> to_array_double(const NumArray &a)
{
	NumArray flat = a.contiguous();
	const double *s = flat.data() + flat.offset();
	Array<double> out = (flat.rank() == 2) ? Array<double>(flat.dim(0), flat.dim(1), 0.0)
	                                       : Array<double>(flat.size(), 0.0);
	for (intptr_t i = 0; i < flat.size(); ++i) {
		out[i] = s[i];
	}
	return out;
}

} // namespace phonometrica::bindings

#endif // PHONOMETRICA_APPLICATION_BINDINGS_HPP
