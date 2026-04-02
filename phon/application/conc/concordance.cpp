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
 * Created: 08/02/2021                                                                                                 *
 *                                                                                                                     *
 * Purpose: see header.                                                                                                *
 *                                                                                                                     *
 ***********************************************************************************************************************/

#include <cmath>
#include <phon/application/conc/concordance.hpp>
#include <phon/application/project.hpp>
#include <phon/application/settings.hpp>
#include <phon/analysis/speech_utils.hpp>
#include <phon/utils/xml.hpp>

namespace phonometrica {

static constexpr const char *EVENT_SEPARATOR = " ";
// file, layer, start time, end time
static const int FILE_INFO_COLUMN_COUNT = 4;
// Global counter for unique default concordance names.
static int s_concordance_id = 0;

int Concordance::next_id() { return ++s_concordance_id; }


Concordance::Concordance(Directory *parent, const String &path) :
		DataTable(meta::get_class<Concordance>(), parent, path)
{
	preload();
}

Concordance::Concordance(intptr_t target_count, Context ctx, intptr_t context_length, Array <AutoMatch> matches, Directory *parent, const String &path) :
		DataTable(meta::get_class<Concordance>(), parent, path), m_matches(std::move(matches))
{
	m_target_count = (int) target_count;
	m_context_type = ctx;
	m_context_length = (int) context_length;
	m_context.reserve(m_matches.size());
	find_context();
	m_loaded = true;
	m_content_modified = true; // New concordance — prompt to save on close.
}

Concordance::Concordance(const Concordance &other) :
		DataTable(other.klass, other.parent(), String())
{
	m_target_count = other.m_target_count;
	m_context_type = other.m_context_type;
	m_context_length = other.m_context_length;
	m_extra_headers = other.m_extra_headers;
	m_measurement_points = other.m_measurement_points;
	m_base_headers = other.m_base_headers;
	m_has_average = other.m_has_average;
	m_has_series = other.m_has_series;
	m_layout = other.m_layout;

	m_nformant = other.m_nformant;
	m_has_bandwidth = other.m_has_bandwidth;
	m_has_erb = other.m_has_erb;
	m_has_bark = other.m_has_bark;
	m_has_auto_params = other.m_has_auto_params;
	m_header_aliases = other.m_header_aliases;

	m_is_pitch = other.m_is_pitch;
	m_has_semitones = other.m_has_semitones;
	m_semitone_ref = other.m_semitone_ref;
	m_has_pitch_erb = other.m_has_pitch_erb;

	m_is_intensity = other.m_is_intensity;

	m_has_duration = other.m_has_duration;
	m_duration_in_ms = other.m_duration_in_ms;
	m_highlight_targets = other.m_highlight_targets;

	m_aux_columns = other.m_aux_columns;
	m_aux_pitch_st = other.m_aux_pitch_st;
	m_aux_pitch_erb = other.m_aux_pitch_erb;
	m_aux_formant_erb = other.m_aux_formant_erb;
	m_aux_formant_bark = other.m_aux_formant_bark;

	m_label = other.m_label;

	m_matches.reserve(other.m_matches.size());

	for (auto &m : other.m_matches) {
		m_matches.append(std::make_unique<Match>(*m));
	}

	m_context = other.m_context;

	m_loaded = true;
	m_content_modified = true;
}

bool Concordance::empty() const
{
	return m_matches.empty();
}

// ── Formant metadata ─────────────────────────────────────────────────────────

void Concordance::set_formant_meta(int nformant, bool bandwidth, bool erb, bool bark, bool auto_params)
{
	m_nformant = nformant;
	m_has_bandwidth = bandwidth;
	m_has_erb = erb;
	m_has_bark = bark;
	m_has_auto_params = auto_params;
}

void Concordance::set_has_erb(bool b)
{
	if (m_has_erb == b) return;
	m_has_erb = b;
	rebuild_extra_headers();
	m_content_modified = true;
}

void Concordance::set_has_bark(bool b)
{
	if (m_has_bark == b) return;
	m_has_bark = b;
	rebuild_extra_headers();
	m_content_modified = true;
}

void Concordance::set_pitch_meta(bool semitones, double st_ref, bool erb)
{
	m_is_pitch = true;
	m_has_semitones = semitones;
	m_semitone_ref = st_ref;
	m_has_pitch_erb = erb;
}

void Concordance::set_has_semitones(bool b)
{
	if (m_has_semitones == b) return;
	m_has_semitones = b;
	rebuild_extra_headers();
	m_content_modified = true;
}

void Concordance::set_has_pitch_erb(bool b)
{
	if (m_has_pitch_erb == b) return;
	m_has_pitch_erb = b;
	rebuild_extra_headers();
	m_content_modified = true;
}

void Concordance::set_intensity_meta()
{
	m_is_intensity = true;
}

int Concordance::stored_fields_per_point() const
{
	if (m_is_pitch || m_is_intensity) return 1;
	return m_nformant + (m_has_bandwidth ? m_nformant : 0);
}

int Concordance::display_fields_per_point() const
{
	int sfpp = stored_fields_per_point();
	int n = sfpp;
	if (m_is_pitch)
	{
		if (m_has_semitones) n++;
		if (m_has_pitch_erb) n++;
	}
	else
	{
		if (m_has_erb) n += sfpp;  // ERB of all stored columns (F + B)
		if (m_has_bark) n += sfpp; // Bark of all stored columns (F + B)
	}
	return n;
}

void Concordance::rebuild_extra_headers()
{
	m_extra_headers.clear();
	m_base_headers.clear();

	if (m_is_intensity)
	{
		// ── Intensity headers ─────────────────────────────────────────────
		auto emit_intensity_group = [&](Array<String> &headers, const char *suffix)
		{
			headers.append(String::format("Intensity(dB)%s", suffix));
		};

		emit_intensity_group(m_base_headers, "");

		if (!has_measurement_data())
		{
			emit_intensity_group(m_extra_headers, "");
		}
		else
		{
			if (m_has_series)
			{
				for (intptr_t p = 1; p <= m_measurement_points.size(); p++)
				{
					char suffix[16];
					std::snprintf(suffix, sizeof(suffix), "(%d%%)", (int)m_measurement_points[p]);
					emit_intensity_group(m_extra_headers, suffix);
				}
			}
			if (m_has_average)
			{
				emit_intensity_group(m_extra_headers, "(avg)");
			}
		}
		return;
	}

	if (m_is_pitch)
	{
		// ── Pitch headers ────────────────────────────────────────────────
		auto emit_pitch_group = [&](Array<String> &headers, const char *suffix)
		{
			headers.append(String::format("F0%s", suffix));
			if (m_has_semitones)
				headers.append(String::format("F0(st)%s", suffix));
			if (m_has_pitch_erb)
				headers.append(String::format("F0(ERB)%s", suffix));
		};

		// Base headers (for long mode): un-suffixed
		emit_pitch_group(m_base_headers, "");

		if (!has_measurement_data())
		{
			// Midpoint: flat extra headers
			emit_pitch_group(m_extra_headers, "");
		}
		else
		{
			if (m_has_series)
			{
				for (intptr_t p = 1; p <= m_measurement_points.size(); p++)
				{
					char suffix[16];
					std::snprintf(suffix, sizeof(suffix), "(%d%%)", (int)m_measurement_points[p]);
					emit_pitch_group(m_extra_headers, suffix);
				}
			}
			if (m_has_average)
			{
				emit_pitch_group(m_extra_headers, "(avg)");
			}
		}
		return;
	}

	// ── Formant headers ──────────────────────────────────────────────────

	if (m_nformant == 0) return;

	auto emit_group = [&](Array<String> &headers, const char *suffix)
	{
		for (int i = 1; i <= m_nformant; i++)
			headers.append(String::format("F%d%s", i, suffix));
		if (m_has_bandwidth)
			for (int i = 1; i <= m_nformant; i++)
				headers.append(String::format("B%d%s", i, suffix));
		if (m_has_erb)
		{
			for (int i = 1; i <= m_nformant; i++)
				headers.append(String::format("E%d%s", i, suffix));
			if (m_has_bandwidth)
				for (int i = 1; i <= m_nformant; i++)
					headers.append(String::format("E(B%d)%s", i, suffix));
		}
		if (m_has_bark)
		{
			for (int i = 1; i <= m_nformant; i++)
				headers.append(String::format("z%d%s", i, suffix));
			if (m_has_bandwidth)
				for (int i = 1; i <= m_nformant; i++)
					headers.append(String::format("z(B%d)%s", i, suffix));
		}
	};

	// Base headers (for long mode): un-suffixed
	emit_group(m_base_headers, "");

	if (!has_measurement_data())
	{
		// Midpoint: flat extra headers (same as base, plus auto params)
		emit_group(m_extra_headers, "");
	}
	else
	{
		// NPoint wide headers
		if (m_has_series)
		{
			for (intptr_t p = 1; p <= m_measurement_points.size(); p++)
			{
				char suffix[16];
				std::snprintf(suffix, sizeof(suffix), "(%d%%)", (int)m_measurement_points[p]);
				emit_group(m_extra_headers, suffix);
			}
		}
		if (m_has_average)
		{
			emit_group(m_extra_headers, "(avg)");
		}
	}

	if (m_has_auto_params) {
		m_extra_headers.append("Max freq");
		m_extra_headers.append("LPC order");
	}
}

void Concordance::set_measurement_info(Array<double> points, bool has_average)
{
	m_measurement_points = std::move(points);
	m_has_average = has_average;
}

// ── Column aliases ───────────────────────────────────────────────────────────

void Concordance::set_header_alias(const String &default_header, const String &alias)
{
	m_header_aliases[default_header] = alias;
	m_content_modified = true;
}

void Concordance::clear_header_alias(const String &default_header)
{
	m_header_aliases.erase(default_header);
	m_content_modified = true;
}

// ── Headers ──────────────────────────────────────────────────────────────────

String Concordance::get_default_header(intptr_t j) const
{
	// The logic of this function is the same as that of get_cell(). See comments there.
	if (j == 1) {
		return "File";
	}
	else if (j == 2) {
		return "Layer";
	}
	else if (j == 3) {
		return "Start time";
	}
	else if (j == 4) {
		return "End time";
	}
	else if (j == 5 && has_context()) {
		return "Left context";
	}

	j -= FILE_INFO_COLUMN_COUNT;
	if (has_context()) j--;

	// We are now ready to consume the match: j starts at 1.
	if (j <= m_target_count)
	{
		if (m_target_count == 1) {
			return "Target";
		}
		else {
			return String::format("Target %d", (int) j);
		}
	}

	j -= m_target_count;
	if (has_context())
	{
		if (j == 1) {
			return "Right context";
		}
		j--;
	}

	// Duration columns
	if (m_has_duration)
	{
		if (j <= m_target_count)
		{
			auto unit = m_duration_in_ms ? "ms" : "s";
			if (m_target_count == 1) {
				return String::format("Duration (%s)", unit);
			}
			return String::format("Duration %d (%s)", (int) j, unit);
		}
		j -= m_target_count;
	}

	// Extra columns (formant measurements, etc.) — layout-dependent
	auto eff = effective_extra_count();
	if (eff > 0)
	{
		if (j <= eff)
		{
			if (m_layout == Layout::Long && has_measurement_data())
			{
				if (j == 1) return "Step";
				if (j == 2) return "Time";
				return m_base_headers[j - 2]; // 1-based
			}
			else
			{
				return m_extra_headers[j]; // 1-based
			}
		}
		j -= eff;
	}

	// Auxiliary columns from merge
	if (!m_aux_columns.empty())
	{
		for (intptr_t c = 1; c <= m_aux_columns.size(); c++)
		{
			int dw = aux_col_display_width(c);
			if (j <= dw) {
				return aux_display_header(c, j);
			}
			j -= dw;
		}
	}

	// We are now ready to consume the properties. Switch to base 0 because
	// we'll use an iterator.
	j--;

	if (j < Property::category_count())
	{
		auto it = Property::get_categories().begin();
		std::advance(it, j);
		return *it;
	}
	assert(j == Property::category_count());

	return "Description";
}

String Concordance::get_header(intptr_t j) const
{
	String default_hdr = get_default_header(j);

	auto it = m_header_aliases.find(default_hdr);
	if (it != m_header_aliases.end()) {
		return it->second;
	}

	return default_hdr;
}

// ── Cell access ──────────────────────────────────────────────────────────────

/// Resolve a display column within a measurement group to a double value.
/// stored_base: offset into match.measurements for the start of this group.
/// within_group: 0-based index within the display fields for one point.
double Concordance::resolve_group_value(const std::vector<double> &meas, int stored_base, int within_group) const
{
	if (m_is_intensity)
	{
		// Intensity: 1 stored value (dB), no computed columns.
		intptr_t idx = stored_base + within_group;
		if (idx < (intptr_t)meas.size()) return meas[idx];
		return std::nan("");
	}

	if (m_is_pitch)
	{
		// Pitch: 1 stored value (F0 in Hz), then optional semitones and ERB computed on the fly.
		if (within_group == 0)
		{
			// F0 in Hz — directly stored
			intptr_t idx = stored_base;
			if (idx < (intptr_t)meas.size()) return meas[idx];
			return std::nan("");
		}

		// Derived value: semitones or ERB
		intptr_t idx = stored_base;
		if (idx >= (intptr_t)meas.size()) return std::nan("");
		double f0 = meas[idx];
		if (!std::isfinite(f0) || f0 <= 0) return std::nan("");

		int derived = within_group - 1; // 0-based within derived columns
		if (m_has_semitones && derived == 0) {
			return speech::hertz_to_semitones(f0, m_semitone_ref);
		}
		// ERB is next (whether or not semitones is present)
		return speech::hertz_to_erb(f0);
	}

	// ── Formant path ─────────────────────────────────────────────────────
	int nf = m_nformant;
	int bw = m_has_bandwidth ? nf : 0;
	int sfpp = nf + bw;

	if (within_group < nf)
	{
		// Formant value — directly stored
		intptr_t idx = stored_base + within_group;
		if (idx < (intptr_t)meas.size()) return meas[idx];
		return std::nan("");
	}
	else if (within_group < nf + bw)
	{
		// Bandwidth value — directly stored
		intptr_t idx = stored_base + within_group;
		if (idx < (intptr_t)meas.size()) return meas[idx];
		return std::nan("");
	}
	else
	{
		// Derived value: ERB or Bark of any stored column (formant or bandwidth)
		int erb_count = m_has_erb ? sfpp : 0;
		int derived_offset = within_group - sfpp;

		int source_index; // 0-based index into stored columns (F1..Fn, B1..Bn)
		bool is_erb;

		if (derived_offset < erb_count) {
			// ERB
			source_index = derived_offset;
			is_erb = true;
		}
		else {
			// Bark
			source_index = derived_offset - erb_count;
			is_erb = false;
		}

		intptr_t f_idx = stored_base + source_index;
		if (f_idx >= (intptr_t)meas.size()) return std::nan("");
		double f = meas[f_idx];
		if (!std::isfinite(f)) return f;
		return is_erb ? speech::hertz_to_erb(f) : speech::hertz_to_bark(f);
	}
}

String Concordance::format_measurement(double val, int within_group) const
{
	if (std::isnan(val)) return "nan";

	int sfpp = stored_fields_per_point();

	// Read the global precision setting (0 = round to nearest Hz).
	int hz_dec = 0;
	try { hz_dec = Settings::get_int("display", "hz_decimals"); }
	catch (...) {}

	if (m_is_intensity)
	{
		// Intensity in dB — 1 decimal place
		return String::format("%.1f", val);
	}

	if (m_is_pitch)
	{
		if (within_group == 0)
		{
			// F0 in Hz
			char fmt[16];
			std::snprintf(fmt, sizeof(fmt), "%%.%df", hz_dec);
			return String::format(fmt, val);
		}
		// Semitones or ERB — hz_dec + 2 extra decimal places
		char fmt[16];
		std::snprintf(fmt, sizeof(fmt), "%%.%df", hz_dec + 2);
		return String::format(fmt, val);
	}

	if (within_group < sfpp)
	{
		// Formant or bandwidth in Hz
		char fmt[16];
		std::snprintf(fmt, sizeof(fmt), "%%.%df", hz_dec);
		return String::format(fmt, val);
	}
	// ERB or Bark — hz_dec + 2 extra decimal places
	char fmt[16];
	std::snprintf(fmt, sizeof(fmt), "%%.%df", hz_dec + 2);
	return String::format(fmt, val);
}

String Concordance::get_cell(intptr_t i, intptr_t j) const
{
	// In long mode, map display row to match index and point index.
	intptr_t mi = (m_layout == Layout::Long && has_measurement_data()) ? match_for_row(i) : i;

	// First handle information columns: these are fixed.
	if (j == 1) {
		return m_matches[mi]->annotation()->label();
	}
	else if (j == 2) {
		return String::convert(m_matches[mi]->get_layer(1));
	}
	else if (j == 3) {
        return String::format("%.4f", m_matches[mi]->get_start_time(1));
	}
	else if (j == 4) {
        return String::format("%.4f", m_matches[mi]->get_end_time(1));
	}
	else if (j == 5 && has_context()) {
		return get_left_context(mi);
	}

	// At this point, j == 5 if we have no context or 6 if we have one because we consumed the left context.
	j -= FILE_INFO_COLUMN_COUNT;
	if (has_context()) j--;

	// We are now ready to consume the match: j starts at 1.
	if (j <= m_target_count) {
		return m_matches[mi]->get_value(j);
	}

	// We now consume the right context if we have one
	j -= m_target_count;
	if (has_context())
	{
		if (j == 1) {
			return get_right_context(mi);
		}
		j--;
	}

	// Duration columns
	if (m_has_duration)
	{
		if (j <= m_target_count)
		{
			auto *target = m_matches[mi]->get((intptr_t)j);
			if (target) {
				double dur = target->end_time - target->start_time;
				if (m_duration_in_ms) dur *= 1000.0;
				return String::format(m_duration_in_ms ? "%.1f" : "%.4f", dur);
			}
			return String();
		}
		j -= m_target_count;
	}

	// Extra columns (formant measurements, etc.) — layout-dependent
	auto eff = effective_extra_count();
	if (eff > 0)
	{
		if (j <= eff)
		{
			auto &meas = m_matches[mi]->measurements;

			if (m_layout == Layout::Long && has_measurement_data())
			{
				// Long mode: Step, Time, then measurement group for this point
				intptr_t pi = point_for_row(i); // 0-based point index

				if (j == 1) {
					return String::convert(intptr_t(pi + 1));
				}
				if (j == 2) {
					return String::format("%.4f", m_measurement_points[pi + 1] / 100.0);
				}
				// j >= 3: measurement within the point
				int within = (int)(j - 3); // 0-based
				int stored_base = (int)(pi * stored_fields_per_point());

				double val = resolve_group_value(meas, stored_base, within);
				return format_measurement(val, within);
			}
			else
			{
				// Wide mode (or midpoint): direct mapping
				int d0 = (int)(j - 1); // 0-based within extra columns

				if (m_is_pitch || m_is_intensity)
				{
					int dfpp = display_fields_per_point();
					int sfpp = stored_fields_per_point();

					if (dfpp > 0) {
						int group_index = d0 / dfpp;
						int within_group = d0 % dfpp;
						int stored_base = group_index * sfpp;

						double val = resolve_group_value(meas, stored_base, within_group);
						return format_measurement(val, within_group);
					}
				}
				else if (m_nformant > 0)
				{
					int dfpp = display_fields_per_point();
					int sfpp = stored_fields_per_point();

					// Check for auto params at the end
					int auto_count = m_has_auto_params ? 2 : 0;
					int total_display = (int)m_extra_headers.size();

					if (auto_count > 0 && d0 >= total_display - auto_count)
					{
						// Auto param: stored at the end of the measurement vector
						int series_points = (has_measurement_data() && m_has_series)
							? (int)m_measurement_points.size() : 0;
						int ngroups = series_points + (m_has_average ? 1 : 0);
						if (!has_measurement_data()) ngroups = 1; // midpoint
						int auto_base = ngroups * sfpp;
						int auto_idx = d0 - (total_display - auto_count);
						intptr_t idx = auto_base + auto_idx;
						if (idx < (intptr_t)meas.size()) {
							double val = meas[idx];
							if (std::isnan(val)) return "nan";
							// Both Max freq and LPC order are integer-valued.
							return String::convert(intptr_t(val));
						}
						return "nan";
					}

					// Measurement group
					if (dfpp > 0) {
						int group_index = d0 / dfpp;
						int within_group = d0 % dfpp;
						int stored_base = group_index * sfpp;

						double val = resolve_group_value(meas, stored_base, within_group);
						return format_measurement(val, within_group);
					}
				}

				// Fallback for non-formant extra columns (shouldn't happen with current architecture)
				intptr_t idx = j - 1; // 0-based
				if (idx < (intptr_t)meas.size())
				{
					double val = meas[idx];
					if (std::isnan(val)) return "nan";
					return String::format("%.1f", val);
				}
				return "nan";
			}
		}
		j -= eff;
	}

	// Auxiliary columns from merge
	if (!m_aux_columns.empty())
	{
		for (intptr_t c = 1; c <= m_aux_columns.size(); c++)
		{
			int dw = aux_col_display_width(c);
			if (j <= dw) {
				return aux_display_value(c, mi, j);
			}
			j -= dw;
		}
	}

	// We are now ready to consume the properties. Switch to base 0 because
	// we'll use an iterator.
	j--;

	if (j < Property::category_count())
	{
		auto it = Property::get_categories().begin();
		std::advance(it, j);
		return m_matches[mi]->annotation()->get_property_value(*it);
	}

	// We now reach the description
	assert(j == Property::category_count());

	return m_matches[mi]->annotation()->description();
}

// ── Cell editing ─────────────────────────────────────────────────────────────

intptr_t Concordance::stored_index_for_column(intptr_t extra_j, intptr_t row) const
{
	if (m_nformant == 0 && !m_is_pitch && !m_is_intensity) return -1;

	int sfpp = stored_fields_per_point();
	int dfpp = display_fields_per_point();
	if (dfpp == 0) return -1;

	if (m_layout == Layout::Long && has_measurement_data())
	{
		// Long mode: extra_j 1=Step, 2=Time, 3+=measurement
		if (extra_j <= 2) return -1;
		int within = (int)(extra_j - 3); // 0-based
		if (within >= sfpp) return -1; // derived or out of range
		intptr_t pi = point_for_row(row); // 0-based
		return pi * sfpp + within;
	}

	// Wide mode (or midpoint)
	int d0 = (int)(extra_j - 1); // 0-based

	// Check auto params at end
	int auto_count = m_has_auto_params ? 2 : 0;
	int total = (int)m_extra_headers.size();
	if (auto_count > 0 && d0 >= total - auto_count)
		return -1; // auto params are not editable

	int within_group = d0 % dfpp;
	if (within_group >= sfpp)
		return -1; // derived column (ERB/Bark)

	int group_index = d0 / dfpp;
	return group_index * sfpp + within_group;
}

void Concordance::set_cell(intptr_t i, intptr_t j, const String &value)
{
	if (!is_editable_measurement(j)) {
		throw error("Cannot edit column %", j);
	}

	bool ok;
	double val = value.to_float(&ok);
	if (!ok) {
		throw error("Invalid numeric value: %", value);
	}

	// Map display row to match index
	intptr_t mi = (m_layout == Layout::Long && has_measurement_data()) ? match_for_row(i) : i;

	// Compute extra column index: strip file info, context, targets, context, duration
	intptr_t extra_j = j - FILE_INFO_COLUMN_COUNT;
	if (has_context()) extra_j--;
	extra_j -= m_target_count;
	if (has_context()) extra_j--;
	extra_j -= duration_column_count();

	intptr_t stored_idx = stored_index_for_column(extra_j, i);
	if (stored_idx < 0) {
		throw error("Cannot edit this cell");
	}

	auto &meas = m_matches[mi]->measurements;
	if (stored_idx < (intptr_t)meas.size()) {
		meas[stored_idx] = val;
		modify();
	}
}

bool Concordance::is_editable_measurement(intptr_t col) const
{
	if (m_nformant == 0 && !m_is_pitch && !m_is_intensity) return false;
	if (!is_measurement_column(col)) return false;

	// Compute extra column index
	intptr_t extra_j = col - (FILE_INFO_COLUMN_COUNT + m_target_count + context_column_count() + duration_column_count());

	// Use row 1 — the stored_index_for_column logic for within_group doesn't depend on row in wide mode,
	// and in long mode it only matters for determining the point (which doesn't affect editability).
	// We just check whether within_group < sfpp.

	int dfpp = display_fields_per_point();
	if (dfpp == 0) return false;

	if (m_layout == Layout::Long && has_measurement_data())
	{
		if (extra_j <= 2) return false; // Step, Time
		int within = (int)(extra_j - 3);
		return within < stored_fields_per_point();
	}

	int d0 = (int)(extra_j - 1);
	int auto_count = m_has_auto_params ? 2 : 0;
	int total = (int)m_extra_headers.size();
	if (auto_count > 0 && d0 >= total - auto_count) return false;

	int within_group = d0 % dfpp;
	return within_group < stored_fields_per_point();
}

bool Concordance::is_left_context(intptr_t col) const
{
	return has_context() && col == FILE_INFO_COLUMN_COUNT + 1;
}

bool Concordance::is_right_context(intptr_t col) const
{
	return has_context() && col == FILE_INFO_COLUMN_COUNT + 2 + m_target_count;
}

bool Concordance::is_time(intptr_t col) const
{
	return col == 3 || col == 4;
}

intptr_t Concordance::row_count() const
{
	if (m_layout == Layout::Long && has_measurement_data())
	{
		return m_matches.size() * m_measurement_points.size();
	}
	return m_matches.size();
}

intptr_t Concordance::column_count() const
{
	// Add 1 for description.
	return FILE_INFO_COLUMN_COUNT + context_column_count() + m_target_count + duration_column_count() + effective_extra_count() + aux_display_column_count() + Property::category_count() + 1;
}


void Concordance::preload()
{
	xml_document doc;
	xml_node root;
	using str = std::string_view;

	try
	{
		root = read_xml(doc, m_path);
	}
	catch (...)
	{
		throw error("Cannot open text query \"%\"", m_path);
	}

	if (root.name() != str("Phonometrica")) {
		throw error("Invalid XML project root in %", m_path);
	}

	auto attr = root.attribute("class");

	if (!attr || class_name() != attr.as_string()) {
		throw error("Expected a concordance, got a % file instead", attr.as_string());
	}
	attr = root.attribute("label");
	if (attr && attr.value()[0] != '\0') {
		set_label(attr.value(), false);
	}

	for (auto node = root.first_child(); node; node = node.next_sibling())
	{
		if (node.name() == str("Metadata"))
		{
			metadata_from_xml(node);
		}
	}
}


void Concordance::load()
{
	xml_document doc;
	xml_node root;
	using str = std::string_view;

	try
	{
		root = read_xml(doc, m_path);
	}
	catch (...)
	{
		throw error("Cannot open text query \"%\"", m_path);
	}

	if (root.name() != str("Phonometrica")) {
		throw error("Invalid XML project root in %", m_path);
	}

	auto attr = root.attribute("class");

	if (!attr || class_name() != attr.as_string()) {
		throw error("Expected a concordance, got a % file instead", attr.as_string());
	}

	// Temporary storage for old-format fields_per_point (used during migration)
	int loaded_fpp = 0;

	for (auto node = root.first_child(); node; node = node.next_sibling())
	{
		if (node.name() == str("Options"))
		{
			parse_options_from_xml(node);
		}
		else if (node.name() == str("ExtraHeaders"))
		{
			for (auto child = node.first_child(); child; child = child.next_sibling())
			{
				if (child.name() == str("Header")) {
					m_extra_headers.append(child.text().get());
				}
			}
		}
		else if (node.name() == str("MeasurementInfo"))
		{
			for (auto child = node.first_child(); child; child = child.next_sibling())
			{
				if (child.name() == str("Points"))
				{
					auto text = String(child.text().get());
					auto parts = text.split(" ");
					for (auto &s : parts)
					{
						bool ok;
						double v = s.to_float(&ok);
						if (ok) m_measurement_points.append(v);
					}
				}
				else if (child.name() == str("BaseHeaders"))
				{
					for (auto hdr = child.first_child(); hdr; hdr = hdr.next_sibling())
					{
						if (hdr.name() == str("Header")) {
							m_base_headers.append(hdr.text().get());
						}
					}
				}
				else if (child.name() == str("FieldsPerPoint"))
				{
					loaded_fpp = child.text().as_int(0);
				}
				else if (child.name() == str("HasAverage"))
				{
					m_has_average = child.text().as_bool(false);
				}
				else if (child.name() == str("Layout"))
				{
					auto val = str(child.text().get());
					m_layout = (val == "long") ? Layout::Long : Layout::Wide;
				}
				else if (child.name() == str("HasSeries"))
				{
					m_has_series = child.text().as_bool(true);
				}
			}
		}
		else if (node.name() == str("FormantMeta"))
		{
			// New format: explicit formant metadata
			for (auto child = node.first_child(); child; child = child.next_sibling())
			{
				if (child.name() == str("NFormant"))
					m_nformant = child.text().as_int(0);
				else if (child.name() == str("HasBandwidth"))
					m_has_bandwidth = child.text().as_bool(false);
				else if (child.name() == str("HasERB"))
					m_has_erb = child.text().as_bool(false);
				else if (child.name() == str("HasBark"))
					m_has_bark = child.text().as_bool(false);
				else if (child.name() == str("HasAutoParams"))
					m_has_auto_params = child.text().as_bool(false);
				else if (child.name() == str("HasSeries"))
					m_has_series = child.text().as_bool(true);
			}
		}
		else if (node.name() == str("PitchMeta"))
		{
			m_is_pitch = true;
			for (auto child = node.first_child(); child; child = child.next_sibling())
			{
				if (child.name() == str("HasSemitones"))
					m_has_semitones = child.text().as_bool(false);
				else if (child.name() == str("SemitoneReference"))
					m_semitone_ref = child.text().as_double(100);
				else if (child.name() == str("HasERB"))
					m_has_pitch_erb = child.text().as_bool(false);
				else if (child.name() == str("HasSeries"))
					m_has_series = child.text().as_bool(true);
			}
		}
		else if (node.name() == str("IntensityMeta"))
		{
			m_is_intensity = true;
			for (auto child = node.first_child(); child; child = child.next_sibling())
			{
				if (child.name() == str("HasSeries"))
					m_has_series = child.text().as_bool(true);
			}
		}
		else if (node.name() == str("ColumnAliases"))
		{
			for (auto child = node.first_child(); child; child = child.next_sibling())
			{
				if (child.name() == str("Alias"))
				{
					auto key_attr = child.attribute("key");
					if (key_attr) {
						m_header_aliases[String(key_attr.value())] = String(child.text().get());
					}
				}
			}
		}
		else if (node.name() == str("AuxColumns"))
		{
			m_aux_pitch_st = node.attribute("pitch_st").as_bool(false);
			m_aux_pitch_erb = node.attribute("pitch_erb").as_bool(false);
			m_aux_formant_erb = node.attribute("formant_erb").as_bool(false);
			m_aux_formant_bark = node.attribute("formant_bark").as_bool(false);

			for (auto col_node = node.first_child(); col_node; col_node = col_node.next_sibling())
			{
				if (col_node.name() != str("Column")) continue;
				auto hdr_attr = col_node.attribute("header");
				if (!hdr_attr) continue;

				AuxColumn col;
				col.header = String(hdr_attr.value());

				auto type_attr = col_node.attribute("type");
				std::string_view type_str = type_attr ? type_attr.value() : "text";
				if (type_str == "numeric")        col.type = AuxColumnType::Numeric;
				else if (type_str == "formant_hz") col.type = AuxColumnType::FormantHz;
				else if (type_str == "bandwidth_hz") col.type = AuxColumnType::BandwidthHz;
				else if (type_str == "pitch_hz")   col.type = AuxColumnType::PitchHz;
				else if (type_str == "intensity_db") col.type = AuxColumnType::IntensityDb;
				else                               col.type = AuxColumnType::Text;

				if (col.type == AuxColumnType::PitchHz) {
					col.semitone_ref = col_node.attribute("semitone_ref").as_double(100);
				}

				for (auto cell = col_node.first_child(); cell; cell = cell.next_sibling())
				{
					if (cell.name() != str("Cell")) continue;
					if (col.type == AuxColumnType::Text) {
						col.text_data.append(String(cell.text().get()));
					} else {
						col.num_data.append(String(cell.text().get()).to_float());
					}
				}
				m_aux_columns.append(std::move(col));
			}
		}
		else if (node.name() == str("Metadata"))
		{
			metadata_from_xml(node);
		}
		else if (node.name() == str("Matches"))
		{
			parse_matches_from_xml(node);
		}
	}

	find_context();

	// After loading all data, normalize the measurement format.
	normalize_after_load();
}


// ── Backward compatibility: detect and migrate old measurement format ─────────

void Concordance::normalize_after_load()
{
	if (m_nformant > 0)
	{
		// New format: formant metadata was loaded from XML. Just rebuild display headers.
		rebuild_extra_headers();
		return;
	}

	if (m_is_pitch)
	{
		// Pitch metadata was loaded from XML. Just rebuild display headers.
		rebuild_extra_headers();
		return;
	}

	if (m_is_intensity)
	{
		// Intensity metadata was loaded from XML. Just rebuild display headers.
		rebuild_extra_headers();
		return;
	}

	// ── Old format detection ──────────────────────────────────────────────
	// Detect formant metadata from the headers that were loaded.

	// Try base_headers first (NPoint mode), then extra_headers (midpoint).
	const Array<String> &headers = m_base_headers.empty() ? m_extra_headers : m_base_headers;
	if (headers.empty()) return; // No formant data at all (pure text concordance).

	int nf = 0;
	bool has_bw = false, has_erb = false, has_bark = false, has_auto = false;

	for (intptr_t i = 1; i <= headers.size(); i++)
	{
		const String &h = headers[i];
		if (h.empty()) continue;

		// For extra_headers in wide mode, strip the suffix: "F1(25%)" → 'F'
		if (h.starts_with('F')) nf++;
		else if (h.starts_with('B')) has_bw = true;
		else if (h.starts_with('E')) has_erb = true;
		else if (h.starts_with('z')) has_bark = true;
		else if (h == "Max freq") { has_auto = true; break; }
	}

	// For NPoint base_headers, nf is the number of F-prefixed headers.
	// For midpoint extra_headers with suffixes, nf is also the count of F-prefixed headers.
	if (nf == 0) return;

	m_nformant = nf;
	m_has_bandwidth = has_bw;
	m_has_erb = has_erb;
	m_has_bark = has_bark;
	m_has_auto_params = has_auto;

	// ── Migrate measurement vectors if ERB/Bark are baked in ──────────────
	// Strip derived (E, z) values from each match's measurement vector,
	// keeping only raw F and B values (plus auto params at the end).

	int old_fpp = nf + (has_bw ? nf : 0) + (has_erb ? nf : 0) + (has_bark ? nf : 0);
	int new_fpp = nf + (has_bw ? nf : 0);

	if (old_fpp != new_fpp)
	{
		// Determine how many groups (points) are stored
		int ngroups;
		if (has_measurement_data()) {
			ngroups = (m_has_series ? (int)m_measurement_points.size() : 0)
					+ (m_has_average ? 1 : 0);
		}
		else {
			ngroups = 1; // midpoint
		}

		for (auto &match : m_matches)
		{
			std::vector<double> new_meas;
			auto &old = match->measurements;
			new_meas.reserve(ngroups * new_fpp + (has_auto ? 2 : 0));

			for (int g = 0; g < ngroups; g++)
			{
				int base = g * old_fpp;
				for (int k = 0; k < new_fpp; k++)
				{
					intptr_t idx = base + k;
					new_meas.push_back(idx < (intptr_t)old.size() ? old[idx] : std::nan(""));
				}
			}

			// Copy auto params (at the end of the old vector)
			if (has_auto)
			{
				int auto_base = ngroups * old_fpp;
				for (int k = 0; k < 2; k++)
				{
					intptr_t idx = auto_base + k;
					new_meas.push_back(idx < (intptr_t)old.size() ? old[idx] : std::nan(""));
				}
			}

			match->measurements = std::move(new_meas);
		}
	}

	// Rebuild display headers from the new metadata.
	rebuild_extra_headers();
}


void Concordance::parse_options_from_xml(xml_node root)
{
	using str = std::string_view;

	for (auto node = root.first_child(); node; node = node.next_sibling())
	{
		if (node.name() == str("Context"))
		{
			auto attr = node.attribute("type");

			if (attr.value() == str("labels"))
			{
				m_context_type = Context::Labels;
			}
			else if (attr.value() == str("kwic"))
			{
				m_context_type = Context::KWIC;
				attr = node.attribute("length");
				if (!attr) {
					throw error("KWIC context in concordance requires a length attribute");
				}
				m_context_length = attr.as_int();
				if (m_context_length < 1) {
					throw error("Invalid context length in KWIC concordance: %", m_context_length);
				}
			}
			else
			{
				m_context_type = Context::None;
			}
		}
		else if (node.name() == str("Duration"))
		{
			auto attr = node.attribute("enabled");
			m_has_duration = attr && attr.as_bool();
			auto unit_attr = node.attribute("unit");
			m_duration_in_ms = unit_attr && unit_attr.value() == str("ms");
		}
		else if (node.name() == str("HighlightTargets"))
		{
			auto attr = node.attribute("enabled");
			m_highlight_targets = !attr || attr.as_bool(); // default true
		}
		else
		{
			throw error("Invalid option for concordance: %", node.name());
		}
	}
}

void Concordance::parse_matches_from_xml(xml_node root)
{
	using str = std::string_view;

#ifdef PHON_TIMING
	auto first_time = clock();
#endif

	auto attr = root.attribute("count");
	if (!attr){
		throw error("Matches node has no 'count' attribute");
	}
	int size = attr.as_int();
	if (size > 0) m_matches.reserve(size);
	auto msg = String("Opening concordance %1").arg(label());
	request_progress(msg, "Loading matches...", size);

	attr = root.attribute("length");
	if (!attr) {
		throw error("Matches node has no 'length' attribute");
	}
	m_target_count = attr.as_int();
	if (m_target_count < 1) {
		throw error("Invalid length in Match node");
	}
	int count = 1;
	for (auto node = root.first_child(); node; node = node.next_sibling())
	{
		if (node.name() != str("Match")) {
			throw error("Expected a Match, got a % in concordance", node.name());
		}
		update_progress(count++);

		Handle<Annotation> annot;
		std::unique_ptr<Match::Target> first_target;
		Match::Target *last_target = nullptr;
		String path;
		std::vector<double> measurements;

		for (auto subnode = node.first_child(); subnode; subnode = subnode.next_sibling())
		{
			if (subnode.name() == str("Annotation"))
			{
				path = subnode.text().get();
				annot = recast<Annotation>(Project::get()->get(path));
			}
			else if (subnode.name() == str("Targets"))
			{
				if (!annot) {
					throw error("A match was found in file '%' but this file is no longer in the current project", path);
				}
				annot->open();

				for (auto target_node = subnode.first_child(); target_node; target_node = target_node.next_sibling())
				{
					attr = target_node.attribute("layer");
					if (!attr) {
						throw error("Missing 'layer' attribute in concordance target");
					}
					int layer = attr.as_int();
					attr = target_node.attribute("event");
					if (layer < 1 || layer > annot->size()) {
						throw error("Invalid layer index (%) in match", layer);
					}
					if (!attr) {
						throw error("Missing 'event' attribute in concordance target");
					}
					int index = attr.as_int();
					auto &events = annot->get_layer_events(layer);
					if (index < 1 || index > events.size()) {
						throw error("Invalid event index (%) in layer with % events", index, events.size());
					}
					auto event = events[index];
					attr = target_node.attribute("offset");
					if (!attr) {
						throw error("Missing 'offset' attribute in concordance target");
					}
					int offset = attr.as_int();
					attr = target_node.attribute("ref");
					if (!attr) {
						throw error("Missing 'ref' attribute in concordance target");
					}
					bool is_ref = attr.as_bool();
					String value = target_node.text().get();

					if (last_target)
					{
                        last_target->next = std::make_unique<Match::Target>(event.start, event.end, value, layer, offset, is_ref);
						last_target = last_target->next.get();
					}
					else
					{
                        first_target = std::make_unique<Match::Target>(event.start, event.end, value, layer, offset, is_ref);
						last_target = first_target.get();
					}
				}
			}
			else if (subnode.name() == str("Measurements"))
			{
				auto text = String(subnode.text().get());
				auto parts = text.split(" ");
				for (auto &s : parts)
				{
					if (s == "NaN") {
						measurements.push_back(std::nan(""));
					}
					else {
						bool ok;
						measurements.push_back(s.to_float(&ok));
						if (!ok) measurements.back() = std::nan("");
					}
				}
			}
			else
			{
				throw error("Invalid node in Match: %", subnode.name());
			}
		}

		assert(first_target);
		auto match = std::make_unique<Match>(annot, std::move(first_target));
		match->measurements = std::move(measurements);
		m_matches.append(std::move(match));
	}

#ifdef PHON_TIMING
	auto last_time = clock();
	auto total = double(last_time-first_time) * 1000 / CLOCKS_PER_SEC;
	std::cerr << "Total loading time concordance: " << total << " ms\n";
#endif
}

void Concordance::write()
{
	open();
	xml_document doc;

	auto root = doc.append_child("Phonometrica");
	root.append_attribute("class").set_value(class_name().data());
	// Only write an explicit label if one was set. Otherwise, the filename is used.
	if (!m_label.empty()) {
		root.append_attribute("label").set_value(m_label.data());
	}
	root.append_attribute("type").set_value("text");
	auto metadata_node = root.append_child("Metadata");
	metadata_to_xml(metadata_node);

	auto option_node = root.append_child("Options");
	auto ctx_node = option_node.append_child("Context");
	auto type_attr = ctx_node.append_attribute("type");

	switch (m_context_type)
	{
		case Context::Labels:
		{
			type_attr.set_value("labels");
		} break;
		case Context::KWIC:
		{
			type_attr.set_value("kwic");
			ctx_node.append_attribute("length").set_value(m_context_length);
		} break;
		default:
			type_attr.set_value("none");
	}

	if (m_has_duration) {
		auto dur_node = option_node.append_child("Duration");
		dur_node.append_attribute("enabled").set_value(true);
		dur_node.append_attribute("unit").set_value(m_duration_in_ms ? "ms" : "s");
	}

	if (!m_highlight_targets) {
		option_node.append_child("HighlightTargets").append_attribute("enabled").set_value(false);
	}

	auto matches_node = root.append_child("Matches");
	matches_node.append_attribute("count").set_value(m_matches.size());
	matches_node.append_attribute("length").set_value(m_target_count);

	// ── Formant metadata (new format) ─────────────────────────────────────
	if (m_nformant > 0)
	{
		auto fm = root.append_child("FormantMeta");
		fm.append_child("NFormant").append_child(node_pcdata)
			.set_value(String::convert(intptr_t(m_nformant)).data());
		fm.append_child("HasBandwidth").append_child(node_pcdata)
			.set_value(m_has_bandwidth ? "true" : "false");
		fm.append_child("HasERB").append_child(node_pcdata)
			.set_value(m_has_erb ? "true" : "false");
		fm.append_child("HasBark").append_child(node_pcdata)
			.set_value(m_has_bark ? "true" : "false");
		fm.append_child("HasAutoParams").append_child(node_pcdata)
			.set_value(m_has_auto_params ? "true" : "false");
		fm.append_child("HasSeries").append_child(node_pcdata)
			.set_value(m_has_series ? "true" : "false");
	}

	// ── Pitch metadata ───────────────────────────────────────────────────
	if (m_is_pitch)
	{
		auto pm = root.append_child("PitchMeta");
		pm.append_child("HasSemitones").append_child(node_pcdata)
			.set_value(m_has_semitones ? "true" : "false");
		pm.append_child("SemitoneReference").append_child(node_pcdata)
			.set_value(String::format("%.1f", m_semitone_ref).data());
		pm.append_child("HasERB").append_child(node_pcdata)
			.set_value(m_has_pitch_erb ? "true" : "false");
		pm.append_child("HasSeries").append_child(node_pcdata)
			.set_value(m_has_series ? "true" : "false");
	}

	// ── Intensity metadata ──────────────────────────────────────────────
	if (m_is_intensity)
	{
		auto im = root.append_child("IntensityMeta");
		im.append_child("HasSeries").append_child(node_pcdata)
			.set_value(m_has_series ? "true" : "false");
	}

	// ── Measurement metadata for wide/long toggle ─────────────────────────
	if (has_measurement_data())
	{
		auto minfo = root.append_child("MeasurementInfo");

		// Measurement points (percentages)
		String pts;
		for (intptr_t i = 1; i <= m_measurement_points.size(); i++)
		{
			if (i > 1) pts.append(' ');
			pts.append(String::format("%.1f", m_measurement_points[i]));
		}
		add_data_node(minfo, "Points", pts);

		minfo.append_child("HasAverage").append_child(node_pcdata)
			.set_value(m_has_average ? "true" : "false");
		minfo.append_child("HasSeries").append_child(node_pcdata)
			.set_value(m_has_series ? "true" : "false");
		minfo.append_child("Layout").append_child(node_pcdata)
			.set_value(m_layout == Layout::Long ? "long" : "wide");
	}

	// ── Column aliases ────────────────────────────────────────────────────
	if (!m_header_aliases.empty())
	{
		auto aliases_node = root.append_child("ColumnAliases");
		for (auto &[key, val] : m_header_aliases)
		{
			auto alias_node = aliases_node.append_child("Alias");
			alias_node.append_attribute("key").set_value(key.data());
			alias_node.append_child(node_pcdata).set_value(val.data());
		}
	}

	// ── Auxiliary columns from merge ──────────────────────────────────────
	if (!m_aux_columns.empty())
	{
		auto aux_node = root.append_child("AuxColumns");
		if (m_aux_pitch_st) aux_node.append_attribute("pitch_st").set_value(true);
		if (m_aux_pitch_erb) aux_node.append_attribute("pitch_erb").set_value(true);
		if (m_aux_formant_erb) aux_node.append_attribute("formant_erb").set_value(true);
		if (m_aux_formant_bark) aux_node.append_attribute("formant_bark").set_value(true);

		for (intptr_t c = 1; c <= m_aux_columns.size(); c++)
		{
			auto &col = m_aux_columns[c];
			auto col_node = aux_node.append_child("Column");
			col_node.append_attribute("header").set_value(col.header.data());

			const char *type_str = "text";
			switch (col.type) {
			case AuxColumnType::Numeric:     type_str = "numeric"; break;
			case AuxColumnType::FormantHz:   type_str = "formant_hz"; break;
			case AuxColumnType::BandwidthHz: type_str = "bandwidth_hz"; break;
			case AuxColumnType::PitchHz:     type_str = "pitch_hz"; break;
			case AuxColumnType::IntensityDb: type_str = "intensity_db"; break;
			default: break;
			}
			col_node.append_attribute("type").set_value(type_str);

			if (col.type == AuxColumnType::PitchHz) {
				col_node.append_attribute("semitone_ref").set_value(
					String::format("%.1f", col.semitone_ref).data());
			}

			auto nrows = (col.type == AuxColumnType::Text) ? col.text_data.size() : col.num_data.size();
			for (intptr_t r = 1; r <= nrows; r++)
			{
				if (col.type == AuxColumnType::Text) {
					add_data_node(col_node, "Cell", col.text_data[r]);
				} else {
					add_data_node(col_node, "Cell", String::format("%.17g", col.num_data[r]));
				}
			}
		}
	}

	// ── Matches ───────────────────────────────────────────────────────────
	auto msg = String("Writing concordance %1").arg(label());
	request_progress(msg, "Writing matches...", (int)m_matches.size());
	int count = 1;
	for (auto &match : m_matches)
	{
		update_progress(count++);
		match->to_xml(matches_node);
	}

	write_xml(doc, m_path);
}

bool Concordance::has_context() const
{
	return m_context_type != Context::None;
}

void Concordance::find_context()
{
	m_context.clear();

	switch (m_context_type)
	{
		case Context::Labels:
			find_labels_context();
			break;
		case Context::KWIC:
			find_kwic_context();
			break;
		default:
			break;
	}
}

void Concordance::find_kwic_context()
{
	String sep(EVENT_SEPARATOR);

	for (auto &match : m_matches)
	{
		m_context.append(get_kwic_context(*match, sep));
	}
}

bool Concordance::is_file_info_column(intptr_t col) const
{
	return col <= FILE_INFO_COLUMN_COUNT;
}

bool Concordance::is_metadata_column(intptr_t col) const
{
	intptr_t bound = FILE_INFO_COLUMN_COUNT + m_target_count + context_column_count() + duration_column_count() + effective_extra_count() + aux_display_column_count();
	return col > bound;
}

bool Concordance::is_measurement_column(intptr_t col) const
{
	auto eff = effective_extra_count();
	if (eff == 0) return false;
	intptr_t lower = FILE_INFO_COLUMN_COUNT + m_target_count + context_column_count() + duration_column_count();
	intptr_t upper = lower + eff;
	return col > lower && col <= upper;
}

bool Concordance::is_duration_column(intptr_t col) const
{
	if (!m_has_duration) return false;
	intptr_t lower = FILE_INFO_COLUMN_COUNT + context_column_count() + m_target_count;
	return col > lower && col <= lower + m_target_count;
}

void Concordance::find_labels_context()
{
	for (auto &match : m_matches)
	{
		m_context.append(get_labels_context(*match));
	}
}

String Concordance::get_left_context(intptr_t i) const
{
	return has_context() ? m_context[i].first : String();
}

String Concordance::get_right_context(intptr_t i) const
{
	return has_context() ? m_context[i].second : String();
}

bool Concordance::is_target(intptr_t col) const
{
	intptr_t lower = FILE_INFO_COLUMN_COUNT + int(has_context()); // add 1 column for the left context
	intptr_t upper = lower + m_target_count;

	return col > lower && col <= upper;
}

Match &Concordance::get_match(intptr_t i)
{
	// In long mode, i is a display row — map to the actual match.
	if (m_layout == Layout::Long && has_measurement_data()) {
		return *m_matches[match_for_row(i)];
	}
	return *m_matches[i];
}

int Concordance::match_region_size() const
{
	return m_target_count + context_column_count();
}

int Concordance::context_column_count() const
{
	return has_context() ? 2 : 0;
}

String Concordance::label() const
{
	return m_label.empty() ? Document::label() : m_label;
}

void Concordance::set_label(String value, bool mutate)
{
	m_label = std::move(value);
	if (mutate) m_content_modified = true;
}

void Concordance::modify()
{
	m_content_modified = true;
}

AutoMatch Concordance::remove_match(intptr_t row)
{
	auto m = m_matches.at(row).release();
	m_matches.remove_at(row);
	modify();
	file_modified();

	return AutoMatch(m);
}

void Concordance::restore_match(intptr_t row, AutoMatch m)
{
	m_matches.insert(row, std::move(m));
	modify();
	file_modified();
}

void Concordance::check_columns_compatible(const Concordance &other) const
{
	auto n = column_count();
	if (n != other.column_count()) {
		throw error("Cannot combine concordances: they have different numbers of columns (% vs %)", n, other.column_count());
	}
	for (intptr_t j = 1; j <= n; j++) {
		auto h1 = get_default_header(j);
		auto h2 = other.get_default_header(j);
		if (h1 != h2) {
			throw error("Cannot combine concordances: column % has different names (\"%\" vs \"%\")", j, h1, h2);
		}
	}
}

void Concordance::copy_metadata_to(Concordance &target) const
{
	target.set_formant_meta(m_nformant, m_has_bandwidth, m_has_erb, m_has_bark, m_has_auto_params);
	if (m_is_pitch) {
		target.set_pitch_meta(m_has_semitones, m_semitone_ref, m_has_pitch_erb);
	}
	if (m_is_intensity) {
		target.set_intensity_meta();
	}
	if (has_measurement_data()) {
		target.set_measurement_info(m_measurement_points, m_has_average);
		target.set_has_series(m_has_series);
		target.set_layout(m_layout);
	}
	target.set_has_duration(m_has_duration);
	target.set_duration_in_ms(m_duration_in_ms);
	target.rebuild_extra_headers();
}

Handle<Concordance> Concordance::unite(const Concordance &other, const String &label) const
{
	check_columns_compatible(other);

	std::set<AutoMatch, MatchLess> buffer;

	for (auto &match : m_matches) {
		buffer.insert(std::make_unique<Match>(*match));
	}
	for (auto &match : other.m_matches) {
		buffer.insert(std::make_unique<Match>(*match));
	}
	Array<AutoMatch> result;
	result.reserve((intptr_t)buffer.size());
	for (auto &match : buffer)
	{
		auto m = const_cast<AutoMatch&>(match).release();
		result.append(std::unique_ptr<Match>(m));
	}
	auto conc = make_handle<Concordance>(m_target_count, m_context_type, m_context_length, std::move(result), nullptr);
	conc->set_label(label, false);
	copy_metadata_to(*conc);

	auto parent = Project::get()->data().get();
	parent->append(conc, false);

	return conc;
}

Handle<Concordance> Concordance::intersect(const Concordance &other, const String &label) const
{
	check_columns_compatible(other);

	Array<AutoMatch> result;

	for (auto &match : m_matches)
	{
		// Matches are guaranteed to be sorted
		auto it = std::lower_bound(other.m_matches.begin(), other.m_matches.end(), match, MatchLess());

		if (it != other.m_matches.end() && **it == *match) {
			result.append(std::make_unique<Match>(*match));
		}
	}

	auto conc = make_handle<Concordance>(m_target_count, m_context_type, m_context_length, std::move(result), nullptr);
	conc->set_label(label, false);
	copy_metadata_to(*conc);

	auto parent = Project::get()->data().get();
	parent->append(conc, false);

	return conc;
}

Handle<Concordance> Concordance::complement(const Concordance &other, const String &label) const
{
	check_columns_compatible(other);

	Array<AutoMatch> result;

	for (auto &match : m_matches)
	{
		// Matches are guaranteed to be sorted
		auto it = std::lower_bound(other.m_matches.begin(), other.m_matches.end(), match, MatchLess());

		if (it == other.m_matches.end() || **it != *match) {
			result.append(std::make_unique<Match>(*match));
		}
	}

	auto conc = make_handle<Concordance>(m_target_count, m_context_type, m_context_length, std::move(result), nullptr);
	conc->set_label(label, false);
	copy_metadata_to(*conc);

	auto parent = Project::get()->data().get();
	parent->append(conc, false);

	return conc;
}

bool Concordance::matches_equal(const Concordance &other) const
{
	if (m_matches.size() != other.m_matches.size()) return false;

	for (intptr_t i = 1; i <= m_matches.size(); i++)
	{
		if (*m_matches[i] != *other.m_matches[i]) return false;
	}
	return true;
}

Handle<Concordance> Concordance::merge(const DataTable &other, const String &label,
                                       const Array<std::pair<String, intptr_t>> &columns_to_add) const
{
	if (row_count() != other.row_count()) {
		throw error("Cannot merge: tables have different numbers of rows (% vs %)", row_count(), other.row_count());
	}

	// Start with a copy of this concordance.
	auto result = make_handle<Concordance>(*this);
	result->m_loaded = true;
	result->set_label(label, false);

	auto nrows = m_matches.size();
	auto *other_conc = dynamic_cast<const Concordance *>(&other);

	for (intptr_t idx = 1; idx <= columns_to_add.size(); idx++)
	{
		auto &[hdr, b_col] = columns_to_add[idx];

		AuxColumn col;
		col.header = hdr;

		// Detect type from source
		if (other_conc && other_conc->is_stored_measurement(b_col))
		{
			col.type = other_conc->get_measurement_type(b_col);
			if (col.type == AuxColumnType::PitchHz) {
				col.semitone_ref = other_conc->semitone_reference();
			}
			col.num_data.resize(nrows);
			for (intptr_t i = 1; i <= nrows; i++) {
				col.num_data[i] = other_conc->get_raw_measurement_value(i, b_col);
			}
		}
		else
		{
			// Try to parse as numeric
			bool all_numeric = true;
			Array<double> nums;
			nums.resize(nrows);
			for (intptr_t i = 1; i <= nrows; i++)
			{
				auto cell = other.get_cell(i, b_col);
				if (cell == "nan" || cell.empty()) {
					nums[i] = std::nan("");
				} else {
					bool ok;
					nums[i] = cell.to_float(&ok);
					if (!ok) { all_numeric = false; break; }
				}
			}

			if (all_numeric)
			{
				col.type = AuxColumnType::Numeric;
				col.num_data = std::move(nums);
			}
			else
			{
				col.type = AuxColumnType::Text;
				col.text_data.resize(nrows);
				for (intptr_t i = 1; i <= nrows; i++) {
					col.text_data[i] = other.get_cell(i, b_col);
				}
			}
		}

		result->m_aux_columns.append(std::move(col));
	}

	auto parent = Project::get()->data().get();
	parent->append(result, false);

	return result;
}

// ── Aux column display logic ────────────────────────────────────────────────

intptr_t Concordance::aux_display_column_count() const
{
	intptr_t total = 0;
	for (intptr_t c = 1; c <= m_aux_columns.size(); c++) {
		total += aux_col_display_width(c);
	}
	return total;
}

int Concordance::aux_col_display_width(intptr_t c) const
{
	auto &col = m_aux_columns[c];
	switch (col.type) {
	case AuxColumnType::PitchHz:
		return 1 + (m_aux_pitch_st ? 1 : 0) + (m_aux_pitch_erb ? 1 : 0);
	case AuxColumnType::FormantHz:
	case AuxColumnType::BandwidthHz:
		return 1 + (m_aux_formant_erb ? 1 : 0) + (m_aux_formant_bark ? 1 : 0);
	default:
		return 1;
	}
}

String Concordance::aux_display_header(intptr_t c, intptr_t k) const
{
	auto &col = m_aux_columns[c];
	if (k == 1) return col.header;

	// Derived headers
	int derived = (int)(k - 2); // 0-based within derived columns

	switch (col.type) {
	case AuxColumnType::PitchHz:
		if (m_aux_pitch_st && derived == 0) {
			return col.header + " [ST]";
		}
		return col.header + " [ERB]";
	case AuxColumnType::FormantHz:
	case AuxColumnType::BandwidthHz:
		if (m_aux_formant_erb && derived == 0) {
			return col.header + " [ERB]";
		}
		return col.header + " [Bark]";
	default:
		return col.header;
	}
}

String Concordance::aux_display_value(intptr_t c, intptr_t mi, intptr_t k) const
{
	auto &col = m_aux_columns[c];

	if (col.type == AuxColumnType::Text) {
		return (mi <= col.text_data.size()) ? col.text_data[mi] : String();
	}

	// All other types use num_data
	if (mi > col.num_data.size()) return String();
	double val = col.num_data[mi];

	if (k == 1) {
		// Base value
		if (std::isnan(val)) return "nan";
		if (col.type == AuxColumnType::IntensityDb)
			return String::format("%.1f", val);
		if (col.type == AuxColumnType::Numeric) {
			if (std::isfinite(val) && val == std::floor(val))
				return String::convert(intptr_t(val));
			return String::convert(val);
		}
		// FormantHz, BandwidthHz, PitchHz — format with hz_decimals
		int hz_dec = 1;
		try { hz_dec = Settings::get_int("display", "hz_decimals"); }
		catch (...) { }
		char fmt[16];
		std::snprintf(fmt, sizeof(fmt), "%%.%df", hz_dec);
		return String::format(fmt, val);
	}

	// Derived value
	if (!std::isfinite(val) || val <= 0) return "nan";

	int derived = (int)(k - 2);

	if (col.type == AuxColumnType::PitchHz) {
		if (m_aux_pitch_st && derived == 0) {
			return String::format("%.2f", speech::hertz_to_semitones(val, col.semitone_ref));
		}
		return String::format("%.2f", speech::hertz_to_erb(val));
	}

	if (col.type == AuxColumnType::FormantHz || col.type == AuxColumnType::BandwidthHz) {
		if (m_aux_formant_erb && derived == 0) {
			return String::format("%.3f", speech::hertz_to_erb(val));
		}
		return String::format("%.3f", speech::hertz_to_bark(val));
	}

	return "nan";
}

bool Concordance::has_aux_pitch() const
{
	for (intptr_t c = 1; c <= m_aux_columns.size(); c++) {
		if (m_aux_columns[c].type == AuxColumnType::PitchHz) return true;
	}
	return false;
}

bool Concordance::has_aux_formant() const
{
	for (intptr_t c = 1; c <= m_aux_columns.size(); c++) {
		auto t = m_aux_columns[c].type;
		if (t == AuxColumnType::FormantHz || t == AuxColumnType::BandwidthHz) return true;
	}
	return false;
}

void Concordance::set_aux_pitch_semitones(bool b) { m_aux_pitch_st = b; }
void Concordance::set_aux_pitch_erb(bool b) { m_aux_pitch_erb = b; }
void Concordance::set_aux_formant_erb(bool b) { m_aux_formant_erb = b; }
void Concordance::set_aux_formant_bark(bool b) { m_aux_formant_bark = b; }

// ── Measurement column introspection (for merge) ────────────────────────────

bool Concordance::is_stored_measurement(intptr_t col) const
{
	if (!is_measurement_column(col)) return false;
	intptr_t extra_j = col - (FILE_INFO_COLUMN_COUNT + m_target_count + context_column_count() + duration_column_count());
	return stored_index_for_column(extra_j, 1) >= 0;
}

Concordance::AuxColumnType Concordance::get_measurement_type(intptr_t col) const
{
	if (!is_measurement_column(col)) return AuxColumnType::Numeric;

	if (m_is_intensity) return AuxColumnType::IntensityDb;
	if (m_is_pitch) return AuxColumnType::PitchHz;

	// Formant: determine if formant Hz or bandwidth Hz
	intptr_t extra_j = col - (FILE_INFO_COLUMN_COUNT + m_target_count + context_column_count() + duration_column_count());
	int d0 = (int)(extra_j - 1); // 0-based within extra columns
	int dfpp = display_fields_per_point();
	if (dfpp == 0) return AuxColumnType::Numeric;

	int within_group = d0 % dfpp;
	int nf = m_nformant;
	if (within_group < nf) return AuxColumnType::FormantHz;
	if (m_has_bandwidth && within_group < nf + nf) return AuxColumnType::BandwidthHz;

	return AuxColumnType::Numeric;
}

double Concordance::get_raw_measurement_value(intptr_t row, intptr_t col) const
{
	if (!is_measurement_column(col)) return std::nan("");

	intptr_t extra_j = col - (FILE_INFO_COLUMN_COUNT + m_target_count + context_column_count() + duration_column_count());
	intptr_t stored_idx = stored_index_for_column(extra_j, row);
	if (stored_idx < 0) return std::nan("");

	intptr_t mi = (m_layout == Layout::Long && has_measurement_data()) ? match_for_row(row) : row;
	auto &meas = m_matches[mi]->measurements;
	if (stored_idx < (intptr_t)meas.size()) return meas[stored_idx];
	return std::nan("");
}

bool Concordance::update_match(intptr_t i, intptr_t target)
{
	bool modified;
	auto result = m_matches[i]->update(target, modified);
	if (modified) modify();

	return result;
}

bool Concordance::is_layer(intptr_t col) const
{
	return col == 2;
}

void Concordance::update_context(intptr_t i)
{
	if (this->has_context())
	{
		if (m_context_type == Context::KWIC)
		{
			m_context[i] = get_kwic_context(*m_matches[i], EVENT_SEPARATOR);
			m_content_modified = true;
		}
		else if (m_context_type == Context::Labels)
		{
			m_context[i] = get_labels_context(*m_matches[i]);
			m_content_modified = true;
		}
	}
}

std::pair<String, String> Concordance::get_kwic_context(const Match &match, const String &sep) const
{
	auto target = match.reference_target();
	auto annot = match.annotation().get();
	std::pair<String, String> ctx;

	if (target)
	{
        auto i = annot->get_event_index(target->layer, target->start_time);
		assert(i != 0);
		auto offset = target->offset;
		ctx.first = annot->left_context(target->layer, i, offset, m_context_length, sep);
		offset += target->value.size();
		ctx.second = annot->right_context(target->layer, i, offset, m_context_length, sep);
	}

	return ctx;
}

std::pair<String, String> Concordance::get_labels_context(const Match &match) const
{
	auto target = match.reference_target();
	std::pair<String, String> ctx;

	if (target)
	{
		auto &annot = *match.annotation();
		auto &events = annot.get_layer_events(target->layer);
        auto i = annot.get_event_index(target->layer, target->start_time);
		assert(i != 0);
        ctx.first = (i == 1) ? String() : events[i-1].text;
        ctx.second = (i == events.size()) ? String() : events[i+1].text;
	}

	return ctx;
}

std::pair<String, String> Concordance::get_context(intptr_t i) const
{
	if (has_context()) {
		return m_context[i];
	}

	return std::pair<String, String>();
}

intptr_t Concordance::effective_extra_count() const
{
	if (m_layout == Layout::Long && has_measurement_data())
	{
		// Step + Time + one group of base headers (un-suffixed)
		return 2 + m_base_headers.size();
	}
	return m_extra_headers.size();
}

intptr_t Concordance::match_for_row(intptr_t i) const
{
	// i is 1-based display row. Each match expands to npoints rows.
	intptr_t npoints = m_measurement_points.size();
	return ((i - 1) / npoints) + 1; // 1-based match index
}

intptr_t Concordance::point_for_row(intptr_t i) const
{
	// i is 1-based display row. Returns 0-based point index within the match.
	intptr_t npoints = m_measurement_points.size();
	return (i - 1) % npoints;
}

} // namespace phonometrica
