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
 * Created: 29/03/2026                                                                                                 *
 *                                                                                                                     *
 * Purpose: Query subclass for pitch (F0) measurements. First runs a text search (via Query::search()), then performs  *
 * pitch tracking on each match. Supports multiple pitch algorithms (Reaper, RAPT, SWIPE, Harvest, Praat).             *
 *                                                                                                                     *
 ***********************************************************************************************************************/

#ifndef PHONOMETRICA_PITCH_QUERY_HPP
#define PHONOMETRICA_PITCH_QUERY_HPP

#include <phon/application/conc/query.hpp>
#include <phon/analysis/signal_processing.hpp>

namespace phonometrica {

class PitchQuery final : public Query
{
public:

	enum class Method
	{
		Midpoint,   // measure at the temporal midpoint of the match
		NPoint      // measure at user-specified percentages
	};

	PitchQuery(Directory *parent, String path);

	~PitchQuery() override = default;

	bool is_text_query() const override { return false; }

	bool is_pitch_query() const override { return true; }

	Handle<Concordance> execute() override;

	Handle<Query> copy() const override;

	void clear() override;

	// ── Pitch settings ──────────────────────────────────────────────────

	Method method() const { return m_method; }
	void set_method(Method m) { m_method = m; }

	speech::PitchTracker algorithm() const { return m_algorithm; }
	void set_algorithm(speech::PitchTracker a) { m_algorithm = a; }

	double min_pitch() const { return m_min_pitch; }
	void set_min_pitch(double f) { m_min_pitch = f; }

	double max_pitch() const { return m_max_pitch; }
	void set_max_pitch(double f) { m_max_pitch = f; }

	double voicing_threshold() const { return m_voicing_threshold; }
	void set_voicing_threshold(double t) { m_voicing_threshold = t; }

	double time_step() const { return m_time_step; }
	void set_time_step(double s) { m_time_step = s; }

	// ── Praat-specific parameters ────────────────────────────────────────

	double octave_jump_cost() const { return m_octave_jump_cost; }
	void set_octave_jump_cost(double c) { m_octave_jump_cost = c; }

	double voicing_cost() const { return m_voicing_cost; }
	void set_voicing_cost(double c) { m_voicing_cost = c; }

	double silence_threshold() const { return m_silence_threshold; }
	void set_silence_threshold(double t) { m_silence_threshold = t; }

	double octave_cost() const { return m_octave_cost; }
	void set_octave_cost(double c) { m_octave_cost = c; }

	// ── Measurement points (for n-point methods) ─────────────────────────

	const Array<double> &measurement_points() const { return m_points; }
	void set_measurement_points(Array<double> pts) { m_points = std::move(pts); }

	// ── N-point output mode ──────────────────────────────────────────────
	// When method == NPoint, at least one of these must be true.

	bool output_series() const { return m_series; }
	void set_output_series(bool b) { m_series = b; }

	bool output_average() const { return m_average; }
	void set_output_average(bool b) { m_average = b; }

	// Initial layout for the resulting concordance (wide or long).
	Concordance::Layout initial_layout() const { return m_initial_layout; }
	void set_initial_layout(Concordance::Layout l) { m_initial_layout = l; }

	// ── Output options ───────────────────────────────────────────────────

	bool output_semitones() const { return m_semitones; }
	void set_output_semitones(bool b) { m_semitones = b; }

	double semitone_reference() const { return m_semitone_ref; }
	void set_semitone_reference(double f) { m_semitone_ref = f; }

	bool output_erb() const { return m_erb; }
	void set_output_erb(bool b) { m_erb = b; }

	// Add measurement time (absolute seconds) as an extra column. See FormantQuery.
	bool output_time() const { return m_output_time; }
	void set_output_time(bool b) { m_output_time = b; }

	// ── Column helpers ───────────────────────────────────────────────────

	// Number of numeric fields per match (F0 values × points + optional computed columns)
	int field_count() const;

	// Build the column header strings for wide mode
	Array<String> build_headers() const;

	// Build un-suffixed column names for one measurement point — used by long layout.
	Array<String> build_base_headers() const;

	// Number of stored columns per measurement point (always 1: F0 in Hz).
	int fields_per_point() const { return 1; }

	// Channel used for measurement (always 1 for now)
	int channel() const { return 1; }

protected:

	void load() override;

	void write() override;

	// Run pitch tracking on a single match and fill its measurement vector.
	void measure_match(Match &match) const;

private:

	// Measurement method
	Method m_method = Method::Midpoint;

	// Measurement points as percentages (0–100), used when method == NPoint
	Array<double> m_points;

	// Pitch algorithm
	speech::PitchTracker m_algorithm = speech::PitchTracker::Reaper;

	// Pitch detection settings
	double m_min_pitch = 75;          // Hz
	double m_max_pitch = 600;         // Hz
	double m_voicing_threshold = 0.5;
	double m_time_step = 0.01;        // seconds

	// Praat-specific parameters
	double m_octave_jump_cost = 0.35;
	double m_voicing_cost = 0.45;
	double m_silence_threshold = 0.03;
	double m_octave_cost = 0.01;

	// N-point output mode (both can be true simultaneously)
	bool m_series = true;          // output per-point columns
	bool m_average = false;        // output averaged columns

	// Initial layout for the concordance view
	Concordance::Layout m_initial_layout = Concordance::Layout::Wide;

	// Output options — semitones and ERB are computed on the fly by the concordance
	bool m_semitones = false;
	double m_semitone_ref = 100;   // Hz (reference for semitone conversion)
	bool m_erb = false;
	bool m_output_time = false;    // add measurement-time column(s) to the concordance
};

namespace traits {
template<> struct maybe_cyclic<PitchQuery> : std::false_type { };
}

} // namespace phonometrica

#endif // PHONOMETRICA_PITCH_QUERY_HPP
