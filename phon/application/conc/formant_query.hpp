/***********************************************************************************************************************
 *                                                                                                                     *
 * Copyright (C) 2019-2026 Julien Eychenne                                                                             *
 *                                                                                                                     *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0. If a copy of the MPL was not   *
 * distributed with this file, You can obtain one at https://mozilla.org/MPL/2.0/.                                     *
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

namespace phonometrica {

class FormantQuery final : public Query
{
public:

	enum class Method
	{
		Midpoint,   // measure at the temporal midpoint of the match
		NPoint      // measure at user-specified percentages
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

	bool output_bandwidth() const { return m_bandwidth; }
	void set_output_bandwidth(bool b) { m_bandwidth = b; }

	bool output_erb() const { return m_erb; }
	void set_output_erb(bool b) { m_erb = b; }

	bool output_bark() const { return m_bark; }
	void set_output_bark(bool b) { m_bark = b; }

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

	// Run LPC/formant analysis on a single match and fill its measurement vector.
	void measure_match(Match &match) const;

private:

	// Measurement method
	Method m_method = Method::Midpoint;

	// Measurement points as percentages (0–100), used when method == NPoint
	Array<double> m_points;

	// Shared LPC settings
	int m_nformant = 3;
	double m_win_size = 0.025;     // seconds
	double m_max_bandwidth = 400;  // Hz

	// Manual mode
	double m_max_freq = 5500;      // Hz (Nyquist)
	int m_lpc_order = 11;

	// Automatic mode (Weenink)
	bool m_automatic = false;
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
};

namespace traits {
template<> struct maybe_cyclic<FormantQuery> : std::false_type { };
}

} // namespace phonometrica

#endif // PHONOMETRICA_FORMANT_QUERY_HPP
