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
 * Purpose: Query subclass for intensity (dB) measurements. First runs a text search (via Query::search()), then       *
 * measures intensity on each match.                                                                                   *
 *                                                                                                                     *
 ***********************************************************************************************************************/

#ifndef PHONOMETRICA_INTENSITY_QUERY_HPP
#define PHONOMETRICA_INTENSITY_QUERY_HPP

#include <phon/application/conc/query.hpp>

namespace phonometrica {

class IntensityQuery final : public Query
{
public:

	enum class Method
	{
		Midpoint,   // measure at the temporal midpoint of the match
		NPoint      // measure at user-specified percentages
	};

	IntensityQuery(Directory *parent, String path);

	~IntensityQuery() override = default;

	bool is_text_query() const override { return false; }

	bool is_intensity_query() const override { return true; }

	Handle<Concordance> execute() override;

	Handle<Query> copy() const override;

	void clear() override;

	// ── Settings ─────────────────────────────────────────────────────────

	Method method() const { return m_method; }
	void set_method(Method m) { m_method = m; }

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

	int field_count() const;

	Array<String> build_headers() const;

	Array<String> build_base_headers() const;

	int fields_per_point() const { return 1; }

	int channel() const { return 1; }

protected:

	void load() override;

	void write() override;

	void measure_match(Match &match) const;

private:

	Method m_method = Method::Midpoint;
	Array<double> m_points;

	bool m_series = true;
	bool m_average = false;
	Concordance::Layout m_initial_layout = Concordance::Layout::Wide;
	bool m_output_time = false;    // add measurement-time column(s) to the concordance
};

namespace traits {
template<> struct maybe_cyclic<IntensityQuery> : std::false_type { };
}

} // namespace phonometrica

#endif // PHONOMETRICA_INTENSITY_QUERY_HPP
