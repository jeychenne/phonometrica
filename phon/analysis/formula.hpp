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
 * Created: 30/03/2026                                                                                                 *
 *                                                                                                                     *
 * Purpose: symbolic representation of a model formula (R-style syntax).                                               *
 *                                                                                                                     *
 ***********************************************************************************************************************/

#ifndef PHONOMETRICA_FORMULA_HPP
#define PHONOMETRICA_FORMULA_HPP

#include <phon/string.hpp>
#include <phon/array.hpp>

namespace phonometrica::stats {

// A single fixed-effects term.
// Simple term:      variables = {"vowel"}
// Interaction term: variables = {"vowel", "context"}  (represents vowel:context)
struct FixedTerm
{
	Array<String> variables;

	bool operator==(const FixedTerm &other) const;

	// True if this is an interaction (two or more variables).
	bool is_interaction() const { return variables.size() > 1; }

	// String representation: "vowel" or "vowel:context"
	String to_string() const;
};


// A smooth term: s(variable) or s(variable, k=15)
// Represents a penalized regression spline of a numeric covariate.
struct SmoothTerm
{
	String variable;           // covariate name, e.g. "duration"
	String basis = "cr";       // basis type: "cr" (cubic regression spline, default)
	intptr_t k = 10;           // basis dimension (number of knots, default 10)

	bool operator==(const SmoothTerm &other) const
	{
		return variable == other.variable && basis == other.basis && k == other.k;
	}

	// String representation: "s(duration)" or "s(duration, k=15)"
	String to_string() const;
};


// A random-effects term: (1 + slope1 + slope2 | group)
struct RandomTerm
{
	String group;            // grouping factor, e.g. "speaker"
	Array<String> slopes;    // random slope variables (may be empty for intercept-only)
	bool intercept = true;   // whether a random intercept is included

	String to_string() const;
};


// A parsed model formula.
// Example: "F1 ~ vowel * context + s(duration) + (1 + vowel | speaker) + (1 | item)"
//
// This is a symbolic description — it knows nothing about data.
// Binding to actual data (resolving column names, building design matrices)
// happens at fit() time.
struct Formula
{
	String response;                // e.g. "F1"
	Array<FixedTerm> fixed;         // fixed-effects terms
	Array<SmoothTerm> smooth;       // smooth (GAM) terms
	Array<RandomTerm> random;       // random-effects terms
	bool intercept = true;          // whether a fixed intercept is included

	// Parse an R-style formula string.
	// Supported syntax:
	//   response ~ term + term + ...
	//   a * b  expands to  a + b + a:b
	//   a : b  is an interaction
	//   - 1  or  + 0  removes the intercept
	//   s(x)                      smooth of x (default k=10, bs="cr")
	//   s(x, k=15)                smooth of x with 15 knots
	//   (1 | group)               random intercept
	//   (1 + slope | group)       random intercept + slope
	//   (0 + slope | group)       random slope only (no intercept)
	// Throws on parse error.
	static Formula parse(const String &text);

	// Reconstruct the formula as a string.
	String to_string() const;

	// True if the formula has any random-effects terms.
	bool has_random_effects() const { return !random.empty(); }

	// True if the formula has any smooth terms.
	bool has_smooth_terms() const { return !smooth.empty(); }

	// Collect all variable names appearing in the formula (response + all terms).
	// Useful for validating against available columns.
	Array<String> all_variables() const;
};

} // namespace phonometrica::stats

#endif // PHONOMETRICA_FORMULA_HPP
