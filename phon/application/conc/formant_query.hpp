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
 * Created: 27/03/2026                                                                                                 *
 *                                                                                                                     *
 * Purpose: Query subclass for formant measurements. First runs a text search (via Query::search()), then performs     *
 * LPC/formant analysis on each match. Supports manual and automatic (Weenink) parameter selection.                    *
 *                                                                                                                     *
 ***********************************************************************************************************************/

#ifndef PHONOMETRICA_FORMANT_QUERY_HPP
#define PHONOMETRICA_FORMANT_QUERY_HPP

#include <phon/application/conc/query.hpp>
#include <phon/hashmap.hpp>

namespace phonometrica {

class FormantQuery final : public Query
{
public:

	String class_name() const override { return "FormantQuery"; }

	enum class Method
	{
		Midpoint,   // measure at the temporal midpoint of the match
		NPoint,     // measure at user-specified percentages
		Step        // measure every `step` seconds across the match (long layout only)
	};

	// Automatic (data-driven) LPC parameter selection strategy, used when automatic() is true.
	//   Weenink   : legacy smoothness-only W criterion (kept so that existing queries still run and reproduce)
	//   Intrinsic : Phase-1 intrinsic ladder (roughness + instability + coverage + density + bandwidth + ordering)
	//   FastTrack : Barreda's Fast Track (see phon/analysis/fast_track.hpp): the ceiling whose formant tracks are
	//               best fitted by a smooth cosine model wins; formants are read from the smooth tracks by default.
	enum class AutoMethod
	{
		Weenink,
		Intrinsic,
		FastTrack
	};

	// Per-level parameter override. Zero in any field means "inherit the global setting".
	// `max_freq` applies in manual mode; `max_freq_low`/`max_freq_high` apply in automatic
	// (Weenink) mode and override the search range bounds.
	struct LevelOverride
	{
		double max_freq = 0;       // manual mode
		double max_freq_low = 0;   // automatic (Weenink) mode
		double max_freq_high = 0;  // automatic (Weenink) mode
	};

	FormantQuery(Directory *parent, String path);

	~FormantQuery() override = default;

	bool is_text_query() const override { return false; }

	bool is_formant_query() const override { return true; }

	Handle<Concordance> execute() override;

	Handle<Query> copy() const override;

	void clear() override;

	// ── Formant settings ─────────────────────────────────────────────────

	Method method() const { return m_method; }
	void set_method(Method m) { m_method = m; }

	int nformant() const { return m_nformant; }
	void set_nformant(int n) { m_nformant = n; }

	double window_size() const { return m_win_size; }
	void set_window_size(double s) { m_win_size = s; }

	double max_bandwidth() const { return m_max_bandwidth; }
	void set_max_bandwidth(double bw) { m_max_bandwidth = bw; }

	// ── Manual mode ──────────────────────────────────────────────────────

	bool automatic() const { return m_automatic; }
	void set_automatic(bool a) { m_automatic = a; }

	AutoMethod auto_method() const { return m_auto_method; }
	void set_auto_method(AutoMethod m) { m_auto_method = m; }

	// Consensus / cross-token shrinkage (Phase 2b). Requires automatic + Intrinsic. Groups matches into
	// (speaker x vowel) cells and nudges each token's parameter choice toward its cell centre.
	bool consensus() const { return m_consensus; }
	void set_consensus(bool c) { m_consensus = c; }
	const String &speaker_property() const { return m_speaker_property; }
	void set_speaker_property(String p) { m_speaker_property = std::move(p); }
	// Vowel/label side of the consensus cell: an annotation property if set, else the value of a chosen match target
	// (0 = the reference target, the default; >=1 = that target index).
	const String &label_property() const { return m_label_property; }
	void set_label_property(String p) { m_label_property = std::move(p); }
	int label_target() const { return m_label_target; }
	void set_label_target(int t) { m_label_target = t; }

	double max_frequency() const { return m_max_freq; }
	void set_max_frequency(double f) { m_max_freq = f; }

	int lpc_order() const { return m_lpc_order; }
	void set_lpc_order(int o) { m_lpc_order = o; }

	// ── Automatic mode (Weenink) ─────────────────────────────────────────

	double max_freq_low() const { return m_max_freq1; }
	void set_max_freq_low(double f) { m_max_freq1 = f; }

	double max_freq_high() const { return m_max_freq2; }
	void set_max_freq_high(double f) { m_max_freq2 = f; }

	double freq_step() const { return m_freq_step; }
	void set_freq_step(double s) { m_freq_step = s; }

	int lpc_order_low() const { return m_lpc_order1; }
	void set_lpc_order_low(int o) { m_lpc_order1 = o; }

	int lpc_order_high() const { return m_lpc_order2; }
	void set_lpc_order_high(int o) { m_lpc_order2 = o; }

	// ── Automatic mode (Fast Track) ──────────────────────────────────────
	// The ceiling range is max_freq_low()..max_freq_high(); the LPC order is fixed (11, i.e. 5.5 formants).

	// Number of ceilings analysed between the two bounds (inclusive).
	int fast_track_steps() const { return m_ft_steps; }
	void set_fast_track_steps(int n) { m_ft_steps = n; }

	// Number of cosine terms in each formant's smooth model (plus an intercept).
	int fast_track_coefficients() const { return m_ft_coeffs; }
	void set_fast_track_coefficients(int n) { m_ft_coeffs = n; }

	// If true, formant frequencies are read from the winning analysis's smooth tracks (Fast Track's output);
	// otherwise they are the raw LPC values at the winning ceiling. Bandwidths are always raw LPC values.
	bool fast_track_smooth() const { return m_ft_smooth; }
	void set_fast_track_smooth(bool b) { m_ft_smooth = b; }

	// ── Fast Track range check ───────────────────────────────────────────
	// When many matches of a speaker are measured with the lowest or the highest maximum frequency of the range, the
	// range probably does not suit that speaker (Barreda's advice is to move it). After execute() with Fast Track,
	// range_edge_report() lists the files (one speaker per file is assumed) in which at least range_edge_min_matches
	// matches were measured and at least range_edge_min_share of them got the same edge of their range. Transient: it
	// is not saved with the query.
	struct RangeEdgeReport
	{
		String file;              // the annotation's label
		intptr_t matches = 0;     // matches measured with Fast Track in this file
		intptr_t at_lowest = 0;   // ... whose maximum frequency is the lowest of the range
		intptr_t at_highest = 0;  // ... whose maximum frequency is the highest of the range
		double lowest = 0;        // lowest and highest maximum frequencies of the file's range (Hz)
		double highest = 0;
	};
	static constexpr intptr_t range_edge_min_matches = 5;
	static constexpr double range_edge_min_share = 0.25;
	const std::vector<RangeEdgeReport> &range_edge_report() const { return m_range_edges; }

	// ── Measurement points (for n-point methods) ─────────────────────────

	const Array<double> &measurement_points() const { return m_points; }
	void set_measurement_points(Array<double> pts) { m_points = std::move(pts); }

	// Distance between measurement points in seconds, used when method == Step.
	double measurement_step() const { return m_step; }
	void set_measurement_step(double s) { m_step = s; }

	// ── Per-file parameter override by property level ────────────────────
	// When `override_category` is non-empty, the query looks up that property
	// on each annotation and, if the value matches an entry in `override_levels`,
	// uses the corresponding ceiling(s) for that match instead of the global
	// settings. Missing values or uncovered levels silently fall through to
	// the global defaults; execute() prints a one-shot warning listing
	// uncovered levels at the start of measurement.

	const String &override_category() const { return m_override_category; }
	void set_override_category(String c) { m_override_category = std::move(c); }
	bool override_enabled() const { return !m_override_category.empty(); }

	const Hashmap<String, LevelOverride> &override_levels() const { return m_override_levels; }
	void set_override_level(const String &value, LevelOverride params) { m_override_levels[value] = params; }
	void clear_override_levels() { m_override_levels.clear(); }

	// Controls the initial display state of the per-match parameter columns on
	// the resulting concordance. Only meaningful when override is enabled; the
	// data is always written to the measurement vector when override is active,
	// regardless of this flag, so the user can re-show the columns later via
	// the concordance's Display menu without re-running the query.
	bool show_params() const { return m_show_params; }
	void set_show_params(bool b) { m_show_params = b; }

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

	bool output_bandwidth() const { return m_bandwidth; }
	void set_output_bandwidth(bool b) { m_bandwidth = b; }

	bool output_erb() const { return m_erb; }
	void set_output_erb(bool b) { m_erb = b; }

	bool output_bark() const { return m_bark; }
	void set_output_bark(bool b) { m_bark = b; }

	// Add measurement time (absolute seconds) as an extra column. Applies to both
	// Midpoint and NPoint methods: Midpoint gives the event midpoint, NPoint gives
	// one Time column per measurement point (no Time column for the average group).
	bool output_time() const { return m_output_time; }
	void set_output_time(bool b) { m_output_time = b; }

	// ── Column helpers ───────────────────────────────────────────────────

	// Number of numeric fields per match (F1..Fn + optional B1..Bn + E1..En + z1..zn + auto params)
	int field_count() const;

	// Build the column header strings (F1, F2, ..., B1, ..., E1, ..., z1, ..., Max freq, LPC order)
	Array<String> build_headers() const;

	// Build un-suffixed column names for one measurement point (F1, F2, F3, B1, ...) — used by long layout.
	Array<String> build_base_headers() const;

	// Number of columns per measurement point (Fn + optional Bn + En + zn).
	int fields_per_point() const;

	// Channel used for measurement (always 1 for now)
	int channel() const { return 1; }

protected:

	void load() override;

	void write() override;

	// Range of maximum frequencies searched in automatic mode for the matches of an annotation: the global range, or
	// the one set for the annotation's level of the override property.
	std::pair<double,double> automatic_range(const Annotation &annot) const;

	// Fill m_range_edges after a Fast Track run (see range_edge_report()).
	void check_range_edges(const Array<AutoMatch> &matches);

	// Run LPC/formant analysis on a single match and fill its measurement vector.
	// Measure one match. In automatic mode, forced_ceiling > 0 skips selection and uses the given parameters
	// (used by the consensus pass, which selects parameters corpus-wide before filling).
	void measure_match(QueryMatch &match, double forced_ceiling = -1.0, int forced_order = -1) const;

	// Fill the measurement vector of a match that could not be measured with NaN, keeping the layout
	// (number of points, time of the first point for the Step method) that measure_match() would produce.
	void fill_missing(QueryMatch &match) const;

	// Number of per-match trailing values stored after the point groups: Max freq + LPC order in automatic mode,
	// Max freq alone in manual mode with a parameter override. Must agree with Concordance::stored_tail_count().
	int step_tail_count() const { return m_automatic ? 2 : (override_enabled() ? 1 : 0); }


	// Two-pass corpus-level measurement: build a candidate cache per match, estimate pooled (speaker x vowel) cell
	// centres, re-select each match toward its centre (EM), then fill. Preserves the output/concordance format.
	void measure_matches_with_consensus(Array<AutoMatch> &matches);

private:

	// Measurement method
	Method m_method = Method::Midpoint;

	// Measurement points as percentages (0–100), used when method == NPoint
	Array<double> m_points;
	double m_step = 0.005;         // seconds, used when method == Step

	// Shared LPC settings
	int m_nformant = 3;
	double m_win_size = 0.025;     // seconds
	double m_max_bandwidth = 400;  // Hz

	// Manual mode
	double m_max_freq = 5500;      // Hz (Nyquist)
	int m_lpc_order = 11;

	// Automatic mode (Weenink)
	bool m_automatic = false;
	AutoMethod m_auto_method = AutoMethod::FastTrack; // selection strategy when m_automatic is true
	int m_ft_steps = 20;           // Fast Track: number of ceilings
	int m_ft_coeffs = 5;           // Fast Track: cosine terms per formant
	bool m_ft_smooth = true;       // Fast Track: report the smooth tracks
	std::vector<RangeEdgeReport> m_range_edges; // result of the last Fast Track run's range check
	bool m_consensus = false;            // cross-token consensus (Phase 2b); requires automatic + Intrinsic
	String m_speaker_property;           // annotation property naming the speaker; empty => one file = one speaker
	String m_label_property;             // annotation property naming the vowel/label; empty => use a match target
	int m_label_target = 0;              // when m_label_property is empty: 0 = reference target, >=1 = that target
	double m_lambda_s = 0.5;             // consensus pull strength (validated ~0.5)
	double m_max_freq1 = 4000;     // search range lower bound
	double m_max_freq2 = 6000;     // search range upper bound
	double m_freq_step = 500;      // step
	int m_lpc_order1 = 10;         // LPC order search range
	int m_lpc_order2 = 12;

	// N-point output mode (both can be true simultaneously)
	bool m_series = true;          // output per-point columns
	bool m_average = false;        // output averaged columns

	// Initial layout for the concordance view
	Concordance::Layout m_initial_layout = Concordance::Layout::Wide;

	// Output options
	bool m_bandwidth = false;
	bool m_erb = false;
	bool m_bark = false;
	bool m_output_time = false;    // add measurement-time column(s) to the concordance

	// Per-file parameter override (empty category = disabled)
	String m_override_category;
	Hashmap<String, LevelOverride> m_override_levels;
	bool m_show_params = true;     // show per-match parameter columns by default
};


} // namespace phonometrica

#endif // PHONOMETRICA_FORMANT_QUERY_HPP
