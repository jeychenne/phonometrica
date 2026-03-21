/***********************************************************************************************************************
 *                                                                                                                     *
 * Copyright (C) 2019-2026 Julien Eychenne                                                                             *
 *                                                                                                                     *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0. If a copy of the MPL was not   *
 * distributed with this file, You can obtain one at https://mozilla.org/MPL/2.0/.                                     *
 *                                                                                                                     *
 * Created: 28/02/2019                                                                                                 *
 *                                                                                                                     *
 * Purpose: parse Praat's TextGrid format.                                                                             *
 *                                                                                                                     *
 ***********************************************************************************************************************/

#ifndef PHONOMETRICA_PRAAT_HPP
#define PHONOMETRICA_PRAAT_HPP

#include <utility>
#include <phon/string.hpp>
#include <phon/file.hpp>

namespace phonometrica { namespace praat {

struct TierHeader
{
	String label;
	double xmin;
	double xmax;
	int size = 0;
	bool has_points;
};

struct Point
{
	double time;
	String text;
};

struct Interval
{
	double xmin;
	double xmax;
	String text;
};

// If a tier was found, sets the name of the tier and whether it is a point tier.
bool parse_tier_header(File &infile, const String &line, TierHeader &header);

bool parse_interval(File &infile, const String &line, Interval &interval);

bool parse_point(File &infile, const String &line, Point &point);

void open_textgrid(const String &tgd, const String &snd = String());

void open_sound(const String &path);

void open_interval(intptr_t tier, intptr_t interval, const String &textgrid, const String &sound);

}} // namespace phonometrica::praat

#endif // PHONOMETRICA_PRAAT_HPP
