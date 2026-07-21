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
 * Created: 11/04/2026                                                                                                 *
 *                                                                                                                     *
 * Purpose: Query subclass for spectral moment measurements (centre of gravity, spread, skewness, kurtosis).           *
 *          First runs a text search (via Query::search()), then computes spectral moments on each match.              *
 *                                                                                                                     *
 ***********************************************************************************************************************/

#ifndef PHONOMETRICA_SPECTRAL_MOMENTS_QUERY_HPP
#define PHONOMETRICA_SPECTRAL_MOMENTS_QUERY_HPP

#include <phon/application/conc/query.hpp>
#include <phon/analysis/signal_processing.hpp>

namespace phonometrica {

class SpectralMomentsQuery final : public Query
{
public:

	String class_name() const override { return "SpectralMomentsQuery"; }

	enum class Method
	{
		Midpoint,   // measure at the temporal midpoint of the match
		NPoint      // measure at user-specified percentages
	};

	SpectralMomentsQuery(Directory *parent, String path);

	~SpectralMomentsQuery() override = default;

	bool is_text_query() const override { return false; }

	bool is_spectral_moments_query() const override { return true; }

	Handle<Concordance> execute() override;

	Handle<Query> copy() const override;

	void clear() override;

	// ── Settings ─────────────────────────────────────────────────────────

	Method method() const { return m_method; }
	void set_method(Method m) { m_method = m; }

	double window_duration() const { return m_window_duration; }
	void set_window_duration(double d) { m_window_duration = d; }

	speech::WindowType window_type() const { return m_window_type; }
	void set_window_type(speech::WindowType w) { m_window_type = w; }

	double min_frequency() const { return m_min_freq; }
	void set_min_frequency(double f) { m_min_freq = f; }

	double max_frequency() const { return m_max_freq; }
	void set_max_frequency(double f) { m_max_freq = f; }

	double preemphasis() const { return m_preemph; }
	void set_preemphasis(double p) { m_preemph = p; }

	bool use_preemphasis() const { return m_use_preemph; }
	void set_use_preemphasis(bool b) { m_use_preemph = b; }

	/// Which moments to include in the output.
	bool output_cog() const { return m_out_cog; }
	void set_output_cog(bool b) { m_out_cog = b; }

	bool output_spread() const { return m_out_spread; }
	void set_output_spread(bool b) { m_out_spread = b; }

	bool output_skewness() const { return m_out_skewness; }
	void set_output_skewness(bool b) { m_out_skewness = b; }

	bool output_kurtosis() const { return m_out_kurtosis; }
	void set_output_kurtosis(bool b) { m_out_kurtosis = b; }

	// ── Measurement points (for n-point methods) ─────────────────────────

	const Array<double> &measurement_points() const { return m_points; }
	void set_measurement_points(Array<double> pts) { m_points = std::move(pts); }

	// ── N-point output mode ──────────────────────────────────────────────

	bool output_series() const { return m_series; }
	void set_output_series(bool b) { m_series = b; }

	bool output_average() const { return m_average; }
	void set_output_average(bool b) { m_average = b; }

	Concordance::Layout initial_layout() const { return m_initial_layout; }
	void set_initial_layout(Concordance::Layout l) { m_initial_layout = l; }

	// Add measurement time (absolute seconds) as an extra column. See FormantQuery.
	bool output_time() const { return m_output_time; }
	void set_output_time(bool b) { m_output_time = b; }

	// ── Column helpers ───────────────────────────────────────────────────

	/// Number of enabled moments (1–4).
	int moment_count() const;

	int field_count() const;

	Array<String> build_headers() const;

	Array<String> build_base_headers() const;

	int fields_per_point() const { return moment_count(); }

	int channel() const { return 1; }

protected:

	void load() override;

	void write() override;

	void measure_match(QueryMatch &match) const;

private:

	Method m_method = Method::Midpoint;
	Array<double> m_points;

	// Analysis settings
	double m_window_duration = 0.025;   // seconds (25 ms, typical for fricatives)
	speech::WindowType m_window_type = speech::WindowType::Gaussian;
	double m_min_freq = 0;              // Hz
	double m_max_freq = 0;              // Hz (0 = Nyquist)
	double m_preemph = 50.0;            // Hz threshold for pre-emphasis
	bool m_use_preemph = true;

	// Output selection
	bool m_out_cog      = true;
	bool m_out_spread   = true;
	bool m_out_skewness = true;
	bool m_out_kurtosis = true;

	// N-point output mode
	bool m_series = true;
	bool m_average = false;
	Concordance::Layout m_initial_layout = Concordance::Layout::Wide;
	bool m_output_time = false;    // add measurement-time column(s) to the concordance
};


} // namespace phonometrica

#endif // PHONOMETRICA_SPECTRAL_MOMENTS_QUERY_HPP
