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
 * Created: 17/05/2026                                                                                                 *
 *                                                                                                                     *
 * Purpose: Query subclass for voice quality measurements (jitter, shimmer, HNR, pulse summary). Runs a text search    *
 *          first (via Query::search()), then computes the full voice report on each match using REAPER-based pulse    *
 *          detection. The measurement is always over the entire interval — there is no midpoint / n-point machinery.  *
 *          Matches whose target is an instant (start_time == end_time) cannot be measured; their cells become NaN    *
 *          and the transient `instant_target_count()` lets the GUI raise a one-shot warning after execute().          *
 *                                                                                                                     *
 ***********************************************************************************************************************/

#ifndef PHONOMETRICA_VOICE_QUALITY_QUERY_HPP
#define PHONOMETRICA_VOICE_QUALITY_QUERY_HPP

#include <phon/application/conc/query.hpp>
#include <phon/hashmap.hpp>

namespace phonometrica {

class VoiceQualityQuery final : public Query
{
public:

	String class_name() const override { return "VoiceQualityQuery"; }

	VoiceQualityQuery(Directory *parent, String path);

	~VoiceQualityQuery() override = default;

	bool is_text_query() const override { return false; }

	bool is_voice_quality_query() const override { return true; }

	Handle<Concordance> execute() override;

	Handle<Query> copy() const override;

	void clear() override;

	// ── F0 range (passed straight to Sound::compute_voice_report) ────────

	double f0_min() const { return m_f0_min; }
	void set_f0_min(double v) { m_f0_min = v; }

	double f0_max() const { return m_f0_max; }
	void set_f0_max(double v) { m_f0_max = v; }

	// ── Per-file F0-range override by property level ─────────────────────
	// Mirrors PitchQuery: when `override_category` is non-empty, the query
	// looks up that property on each annotation and, if the value matches an
	// entry in `override_levels`, replaces f0_min and/or f0_max with the
	// per-level value(s) for that match. Missing values or uncovered levels
	// silently fall through to the global defaults; execute() prints a one-shot
	// warning listing uncovered levels at the start of measurement.
	// Both bounds are overridable: children and women typically have a higher
	// floor than the project-wide default (e.g. 100 Hz vs 75 Hz) as well as a
	// higher ceiling.
	struct LevelOverride
	{
		double f0_min = 0;   // Hz, 0 = inherit m_f0_min
		double f0_max = 0;   // Hz, 0 = inherit m_f0_max
	};

	const String &override_category() const { return m_override_category; }
	void set_override_category(String c) { m_override_category = std::move(c); }
	bool override_enabled() const { return !m_override_category.empty(); }

	const Hashmap<String, LevelOverride> &override_levels() const { return m_override_levels; }
	void set_override_level(const String &value, LevelOverride params) { m_override_levels[value] = params; }
	void clear_override_levels() { m_override_levels.clear(); }

	// Controls the initial display state of the per-match F0-range columns on
	// the resulting concordance. Data is always stored when override is active;
	// this flag controls only the default display state. Users can re-show
	// via the concordance's Display menu.
	bool show_params() const { return m_show_params; }
	void set_show_params(bool b) { m_show_params = b; }

	// ── Feature selection (14 fields, matching speech::VoiceReport) ──────

	bool output_num_pulses() const       { return m_out_num_pulses; }
	void set_output_num_pulses(bool b)   { m_out_num_pulses = b; }

	bool output_voicing() const          { return m_out_voicing; }
	void set_output_voicing(bool b)      { m_out_voicing = b; }

	bool output_mean_period() const      { return m_out_mean_period; }
	void set_output_mean_period(bool b)  { m_out_mean_period = b; }

	bool output_mean_f0() const          { return m_out_mean_f0; }
	void set_output_mean_f0(bool b)      { m_out_mean_f0 = b; }

	bool output_jitter_local() const     { return m_out_jitter_local; }
	void set_output_jitter_local(bool b) { m_out_jitter_local = b; }

	bool output_jitter_local_abs() const     { return m_out_jitter_local_abs; }
	void set_output_jitter_local_abs(bool b) { m_out_jitter_local_abs = b; }

	bool output_jitter_rap() const       { return m_out_jitter_rap; }
	void set_output_jitter_rap(bool b)   { m_out_jitter_rap = b; }

	bool output_jitter_ppq5() const      { return m_out_jitter_ppq5; }
	void set_output_jitter_ppq5(bool b)  { m_out_jitter_ppq5 = b; }

	bool output_jitter_ddp() const       { return m_out_jitter_ddp; }
	void set_output_jitter_ddp(bool b)   { m_out_jitter_ddp = b; }

	bool output_shimmer_local() const     { return m_out_shimmer_local; }
	void set_output_shimmer_local(bool b) { m_out_shimmer_local = b; }

	bool output_shimmer_local_db() const     { return m_out_shimmer_local_db; }
	void set_output_shimmer_local_db(bool b) { m_out_shimmer_local_db = b; }

	bool output_shimmer_apq3() const     { return m_out_shimmer_apq3; }
	void set_output_shimmer_apq3(bool b) { m_out_shimmer_apq3 = b; }

	bool output_shimmer_apq5() const     { return m_out_shimmer_apq5; }
	void set_output_shimmer_apq5(bool b) { m_out_shimmer_apq5 = b; }

	bool output_shimmer_apq11() const     { return m_out_shimmer_apq11; }
	void set_output_shimmer_apq11(bool b) { m_out_shimmer_apq11 = b; }

	bool output_hnr() const     { return m_out_hnr; }
	void set_output_hnr(bool b) { m_out_hnr = b; }

	// ── Column helpers ───────────────────────────────────────────────────

	/// Number of enabled features (1-14).
	int feature_count() const;

	/// Total number of stored fields per match: feature_count() plus the
	/// trailing per-match parameter columns (Min/Max pitch) when override is
	/// enabled. Used to size match.measurements.
	int field_count() const { return feature_count() + (override_enabled() ? 2 : 0); }

	Array<String> build_headers() const;

	int fields_per_point() const { return feature_count(); }

	int channel() const { return 1; }

	// Transient — reset at the start of execute(), incremented by measure_match()
	// for every match whose reference target is an instant. The GUI reads it once
	// after execute() to issue a single warning rather than per-row diagnostics.
	intptr_t instant_target_count() const { return m_instant_target_count; }

protected:

	void load() override;

	void write() override;

	void measure_match(QueryMatch &match) const;

private:

	// Analysis settings
	double m_f0_min = 75.0;   // Hz
	double m_f0_max = 600.0;  // Hz

	// Output selection — defaults are the "essentials" reported in voice-report studies.
	bool m_out_num_pulses       = true;
	bool m_out_voicing          = true;   // proportion of voiced frames in the pitch contour
	bool m_out_mean_period      = false;
	bool m_out_mean_f0          = true;
	bool m_out_jitter_local     = true;
	bool m_out_jitter_local_abs = false;
	bool m_out_jitter_rap       = false;
	bool m_out_jitter_ppq5      = false;
	bool m_out_jitter_ddp       = false;
	bool m_out_shimmer_local    = true;
	bool m_out_shimmer_local_db = false;
	bool m_out_shimmer_apq3     = false;
	bool m_out_shimmer_apq5     = false;
	bool m_out_shimmer_apq11    = false;
	bool m_out_hnr              = true;

	// Counter populated during execute() — mutable because measure_match() is const.
	mutable intptr_t m_instant_target_count = 0;

	// Per-file F0-range override (empty category = disabled)
	String m_override_category;
	Hashmap<String, LevelOverride> m_override_levels;
	bool m_show_params = true;     // show per-match parameter columns by default
};


} // namespace phonometrica

#endif // PHONOMETRICA_VOICE_QUALITY_QUERY_HPP
