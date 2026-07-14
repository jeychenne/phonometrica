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
 * Purpose: see header.                                                                                                *
 *                                                                                                                     *
 ***********************************************************************************************************************/

#include <phon/runtime.hpp>
#include <array>
#include <map>
#include <string>
#include <phon/application/conc/formant_query.hpp>
#include <phon/application/project.hpp>
#include <phon/application/property.hpp>
#include <phon/application/sound.hpp>
#include <phon/analysis/speech_utils.hpp>
#include <phon/analysis/weenink.hpp>
#include <phon/analysis/formant_selection.hpp>
#include <phon/utils/file_system.hpp>

namespace phonometrica {

FormantQuery::FormantQuery(Directory *parent, String path) :
		Query(meta::get_class<FormantQuery>(), parent, String()) // Pass empty path to avoid vtable issue
{
	// Set the path ourselves and call our own load(), since during base-class construction
	// the vtable still points to Query, not FormantQuery.
	m_path = std::move(path);
	if (!m_path.empty()) {
		load();
	}
}

// Only raw formant + bandwidth fields are stored per measurement point.
// ERB and Bark are computed on the fly by the concordance.
int FormantQuery::fields_per_point() const
{
	return m_nformant + (m_bandwidth ? m_nformant : 0);
}

int FormantQuery::field_count() const
{
	int fpp = fields_per_point();
	int n = 0;

	if (m_method == Method::Midpoint)
	{
		n = fpp;
	}
	else
	{
		int npoints = (int)m_points.size();
		if (npoints < 1) npoints = 1;
		if (m_series)  n += npoints * fpp;
		if (m_average) n += fpp;
	}

	if (m_automatic) n += 2; // max_freq, lpc_order per match
	else if (override_enabled()) n += 1; // effective max_freq per match (varies by file)
	return n;
}

// Build wide-mode column headers for one measurement point, with suffix.
// Only emits F and B headers (ERB/Bark are handled by the concordance).
Array<String> FormantQuery::build_headers() const
{
	Array<String> headers;

	auto emit_group = [&](const char *suffix)
	{
		for (intptr_t i = 1; i <= m_nformant; i++) {
			headers.append(String::format("F%d%s", (int)i, suffix));
		}
		if (m_bandwidth) {
			for (intptr_t i = 1; i <= m_nformant; i++) {
				headers.append(String::format("B%d%s", (int)i, suffix));
			}
		}
	};

	if (m_method == Method::Midpoint)
	{
		emit_group("");
	}
	else
	{
		// Time series: one group per measurement point with percentage suffix
		if (m_series)
		{
			for (intptr_t p = 0; p < m_points.size(); p++)
			{
				auto pct = (int)m_points[p];
				char suffix[16];
				snprintf(suffix, sizeof(suffix), "(%d%%)", pct);
				emit_group(suffix);
			}
		}

		// Average: one group with "(avg)" suffix
		if (m_average)
		{
			emit_group("(avg)");
		}
	}

	if (m_automatic) {
		headers.append("Max freq");
		headers.append("LPC order");
	}
	else if (override_enabled()) {
		headers.append("Max freq");
	}

	return headers;
}

// Un-suffixed column names for a single measurement point: F1, F2, ..., [B1, ...].
// Only raw stored columns (no ERB/Bark).
Array<String> FormantQuery::build_base_headers() const
{
	Array<String> headers;

	for (intptr_t i = 1; i <= m_nformant; i++) {
		headers.append(String::format("F%d", (int)i));
	}
	if (m_bandwidth) {
		for (intptr_t i = 1; i <= m_nformant; i++) {
			headers.append(String::format("B%d", (int)i));
		}
	}

	return headers;
}

void FormantQuery::clear()
{
	Query::clear();
	m_points.clear();
	m_method = Method::Midpoint;
	m_nformant = 3;
	m_win_size = 0.025;
	m_max_bandwidth = 400;
	m_max_freq = 5500;
	m_lpc_order = 11;
	m_automatic = false;
	m_auto_method = AutoMethod::Intrinsic;
	m_consensus = false;
	m_speaker_property = String();
	m_label_property = String();
	m_label_target = 0;
	m_lambda_s = 0.5;
	m_max_freq1 = 4000;
	m_max_freq2 = 6000;
	m_freq_step = 500;
	m_lpc_order1 = 10;
	m_lpc_order2 = 12;
	m_series = true;
	m_average = false;
	m_initial_layout = Concordance::Layout::Wide;
	m_bandwidth = false;
	m_erb = false;
	m_bark = false;
	m_output_time = false;
	m_override_category.clear();
	m_override_levels.clear();
}

Handle<Concordance> FormantQuery::execute()
{
	// Optional: warn about parameter-override coverage before doing any measurement.
	// Messages are routed through the runtime's error callback so they render in
	// red in the console panel (and fall back to print if no error sink is set).
	if (override_enabled())
	{
		auto &rt = Project::get()->runtime();
		auto emit_msg = [&](const String &msg) {
			if (rt.show_error) rt.show_error(msg);
			else if (rt.print) rt.print(msg);
		};

		auto known = Property::get_values(m_override_category);
		if (known.empty())
		{
			emit_msg(String::format(
				"Warning: parameter override category \"%s\" has no known values "
				"in the project; all matches will use the default parameters.",
				m_override_category.data()));
		}
		else
		{
			Array<String> uncovered;
			for (auto &v : known) {
				if (m_override_levels.find(v) == m_override_levels.end()) {
					uncovered.append(v);
				}
			}
			if (!uncovered.empty())
			{
				String msg = String::format(
					"Warning: parameter override is enabled on category \"%s\" "
					"but the following level(s) have no override and will fall "
					"back to the default parameters: ",
					m_override_category.data());
				for (intptr_t i = 0; i < uncovered.size(); i++) {
					if (i > 0) msg.append(", ");
					msg.append(uncovered[i]);
				}
				emit_msg(msg);
			}
		}
	}

	// Phase 1: text search (reuse the base class search engine)
	auto matches = search();

	// Phase 2: formant measurement on each match
	int count = (int)matches.size();

	if (m_automatic && m_auto_method == AutoMethod::Intrinsic && m_consensus)
	{
		// Corpus-level two-pass selection (cells + EM) before filling.
		measure_matches_with_consensus(matches);
	}
	else
	{
		for (int i = 0; i < count; i++)
		{
			query_progress(i, count);
			if (m_cancel_requested) break;

			try
			{
				measure_match(*matches[i]);
			}
			catch (std::exception &e)
			{
				// If measurement fails for a single match (e.g. sound file not bound),
				// fill with NaN and continue rather than aborting the whole query.
				auto &m = *matches[i];
				m.measurements.assign(field_count(), std::nan(""));
			}
		}
	}

	// Build concordance with formant metadata
	auto conc = make_handle<Concordance>(m_constraints.size(), m_context, m_context_length, std::move(matches), nullptr);

	// Duration columns
	if (m_include_duration) {
		conc->set_has_duration(true);
		conc->set_duration_in_ms(m_duration_in_ms);
	}

	// Set formant metadata — ERB/Bark are computed on the fly by the concordance
	conc->set_formant_meta(m_nformant, m_bandwidth, m_erb, m_bark, m_automatic);
	// Tell the concordance about the per-match Max freq trailing column when
	// we're in manual mode with per-property overrides active (auto mode already
	// emits Max freq + LPC order via set_formant_meta's last arg).
	conc->set_has_per_match_max_freq(!m_automatic && override_enabled() && m_show_params);
	conc->set_per_match_max_freq_available(!m_automatic && override_enabled());

	// Measurement-time column(s)
	conc->set_has_time(m_output_time);

	// Provide measurement metadata so the concordance can toggle between wide and long layout.
	if (m_method == Method::NPoint)
	{
		conc->set_measurement_info(m_points, m_average);
		conc->set_has_series(m_series);
		conc->set_layout(m_initial_layout);
	}

	// Build all display headers from the metadata
	conc->rebuild_extra_headers();

	auto lbl = this->label();
	if (lbl.starts_with("Query ")) {
		lbl = String::format("Concordance %d", Concordance::next_id());
	}
	conc->set_label(lbl, false);
	Project::get()->add_temp_concordance(conc);

	return conc;
}

// Fill match.measurements with raw formant and bandwidth values only.
// ERB and Bark are computed on the fly by Concordance::get_cell().
void FormantQuery::measure_match(QueryMatch &match, double forced_ceiling, int forced_order) const
{
	using namespace speech;

	auto annot = match.annotation();
	auto sound = annot->sound();
	if (!sound)
	{
		throw error("Cannot measure formants in annotation \"%\" because it is not bound to any sound file", annot->path());
	}

	// Get the reference target's time boundaries
	auto *target = match.reference_target();
	if (!target) {
		target = match.get(1);
	}
	double t1 = target->start_time;
	double t2 = target->end_time;

	// Per-file parameter override lookup. We compute *local* copies of the parameters
	// for this match so the const member fields stay untouched and concurrent matches
	// (should we parallelize later) remain safe.
	double local_max_freq   = m_max_freq;
	double local_max_freq1  = m_max_freq1;
	double local_max_freq2  = m_max_freq2;

	if (override_enabled())
	{
		auto value = annot->get_property_value(m_override_category);
		if (!value.empty())
		{
			auto it = m_override_levels.find(value);
			if (it != m_override_levels.end())
			{
				auto &ov = it->second;
				if (m_automatic) {
					if (ov.max_freq_low  > 0) local_max_freq1 = ov.max_freq_low;
					if (ov.max_freq_high > 0) local_max_freq2 = ov.max_freq_high;
				}
				else {
					if (ov.max_freq > 0) local_max_freq = ov.max_freq;
				}
			}
		}
	}

	// Determine LPC parameters (manual or automatic)
	double max_freq;
	int lpc_order;

	if (m_automatic)
	{
		if (forced_ceiling > 0)
		{
			// Parameters chosen upstream (e.g. by the consensus pass); skip per-match selection.
			max_freq = forced_ceiling;
			lpc_order = forced_order;
		}
		else
		{
			std::pair<double, double> params;
			if (m_auto_method == AutoMethod::Weenink)
			{
				params = find_lpc_parameters(sound.get(), channel(), m_nformant, m_win_size,
				                             t1, t2, local_max_freq1, local_max_freq2, m_freq_step,
				                             m_lpc_order1, m_lpc_order2);
			}
			else
			{
				params = speech::select_analysis_intrinsic(sound.get(), channel(), m_nformant, m_win_size,
				                                            t1, t2, local_max_freq1, local_max_freq2, m_freq_step,
				                                            m_lpc_order1, m_lpc_order2);
			}
			max_freq = params.first;
			lpc_order = (int)params.second;
		}
	}
	else
	{
		max_freq = local_max_freq;
		lpc_order = m_lpc_order;
	}

	auto nf = m_nformant;
	int total = field_count();
	match.measurements.resize(total, std::nan(""));

	// If Weenink's method failed to find suitable parameters, leave as NaN
	if (max_freq == 0)
	{
		return;
	}

	int idx = 0;

	// Helper: fill one measurement point's worth of columns from an nformant×2 data matrix.
	// Layout per point: F1..Fn, [B1..Bn]. No ERB/Bark — they are computed on the fly.
	auto fill_point = [&](const Array<double> &data)
	{
		for (int i = 0; i < nf; i++) {
			match.measurements[idx++] = data(i, 0);
		}
		if (m_bandwidth) {
			for (int i = 0; i < nf; i++) {
				match.measurements[idx++] = data(i, 1);
			}
		}
	};

	if (m_method == Method::Midpoint)
	{
		double t = (t1 + t2) / 2.0;
		auto data = sound->get_formants(channel(), t, nf, max_freq, m_win_size, lpc_order);
		fill_point(data);
	}
	else
	{
		// Measure at each point individually
		double duration = t2 - t1;
		int npoints = (int)m_points.size();

		// We always need per-point data (either for the series output or to compute the average).
		// Collect all per-point matrices.
		Array<Array<double>> point_data;
		for (auto p : m_points)
		{
			double t = t1 + (p / 100.0) * duration;
			point_data.append(sound->get_formants(channel(), t, nf, max_freq, m_win_size, lpc_order));
		}

		// Time series: output each point's data
		if (m_series)
		{
			for (intptr_t k = 0; k < npoints; k++) {
				fill_point(point_data[k]);
			}
		}

		// Average: compute mean across the per-point data and output one group
		if (m_average)
		{
			// Build an averaged nformant×2 matrix
			Array<double> avg(nf, 2, 0.0);
			for (intptr_t k = 0; k < npoints; k++)
			{
				for (int i = 0; i < nf; i++) {
					for (int j = 0; j < 2; j++) {
						avg(i, j) += point_data[k](i, j);
					}
				}
			}
			for (int i = 0; i < nf; i++) {
				for (int j = 0; j < 2; j++) {
					avg(i, j) /= npoints;
				}
			}
			fill_point(avg);
		}
	}

	// Per-match LPC parameters (automatic mode) or effective Max freq (manual + override)
	if (m_automatic) {
		match.measurements[idx++] = max_freq;
		match.measurements[idx++] = (double)lpc_order;
	}
	else if (override_enabled()) {
		match.measurements[idx++] = max_freq;
	}
}

// Two-pass corpus-level measurement with cross-token consensus (Phase 2b).
//   Pass 0  build a scored candidate cache per match; provisionally pick the intrinsic argmin.
//   EM      estimate a robust, partially-pooled centre per (speaker x vowel) cell, then re-pick each match as
//           argmin(intrinsic badness + lambda_s * ERB distance to its cell centre); repeat.
//   Fill    measure each match with its finally-selected <ceiling, order> (reuses measure_match's fill logic).
// A light pull (lambda_s ~ 0.5) rescues tokens whose formants scattered off a good cell centre without welding to it;
// it is neutral-to-harmless when there is nothing to pull. It cannot correct systematic errors (a whole cell biased
// the same way) — those remain candidate-generation problems.
void FormantQuery::measure_matches_with_consensus(Array<AutoMatch> &matches)
{
	using namespace speech;
	int count = (int)matches.size();

	struct Entry {
		std::vector<AnalysisCandidate> cands;
		int    sel = -1;              // index of currently selected candidate
		std::string speaker, vowel;   // cell-key parts
		bool   ok = false;
	};
	std::vector<Entry> entries(count);
	IntrinsicWeights w; // shipping defaults (lambdas = 1)

	// ---- Pass 0: build caches + provisional intrinsic selection + resolve cells ----
	for (int i = 0; i < count; i++)
	{
		query_progress(i, count);
		if (m_cancel_requested) return;
		auto &match = *matches[i];
		auto &e = entries[i];
		try
		{
			auto annot = match.annotation();
			auto sound = annot->sound();
			if (!sound) continue;

			auto *target = match.reference_target();
			if (!target) target = match.get(1);
			double t1 = target->start_time, t2 = target->end_time, tmid = 0.5 * (t1 + t2);

			double c_lo = m_max_freq1, c_hi = m_max_freq2;
			if (override_enabled())
			{
				auto value = annot->get_property_value(m_override_category);
				if (!value.empty())
				{
					auto it = m_override_levels.find(value);
					if (it != m_override_levels.end())
					{
						if (it->second.max_freq_low  > 0) c_lo = it->second.max_freq_low;
						if (it->second.max_freq_high > 0) c_hi = it->second.max_freq_high;
					}
				}
			}

			e.cands = build_intrinsic_candidates(sound.get(), channel(), m_nformant, m_win_size,
			                                     t1, t2, tmid, c_lo, c_hi, m_freq_step,
			                                     m_lpc_order1, m_lpc_order2, w);
			if (e.cands.empty()) continue;

			double best = (std::numeric_limits<double>::max)();
			for (int k = 0; k < (int)e.cands.size(); ++k)
				if (e.cands[k].score.badness < best) { best = e.cands[k].score.badness; e.sel = k; }

			if (!m_label_property.empty())
			{
				e.vowel = std::string(annot->get_property_value(m_label_property).data());
			}
			else
			{
				auto *lt = (m_label_target >= 1) ? match.get(m_label_target) : target;
				e.vowel = lt ? std::string(lt->value.data()) : std::string();
			}
			e.speaker = m_speaker_property.empty()
			          ? std::string(annot->path().data())
			          : std::string(annot->get_property_value(m_speaker_property).data());
			e.ok = (e.sel >= 0);
		}
		catch (...) { e.ok = false; }
	}

	// ---- EM: pooled cell centres, re-selection ----
	const int em_iters = 3;
	const double kappa = 2.0, kappa2 = 2.0; // shrinkage toward vowel- and speaker-level pools (helps sparse cells)
	auto key = [](const Entry &e) { return e.speaker + "\x1f" + e.vowel; };
	auto median = [](std::vector<double> v) -> double {
		if (v.empty()) return std::nan("");
		auto m = v.begin() + v.size() / 2;
		std::nth_element(v.begin(), m, v.end());
		double x = *m;
		if (v.size() % 2 == 0) x = 0.5 * (x + *std::max_element(v.begin(), m));
		return x;
	};

	for (int it = 0; it < em_iters; ++it)
	{
		std::map<std::string, std::array<std::vector<double>, 3>> cellv, vowv, spkv;
		for (int i = 0; i < count; ++i)
		{
			auto &e = entries[i];
			if (!e.ok) continue;
			const auto &c = e.cands[e.sel];
			for (int k = 0; k < 3; ++k)
			{
				double f = c.formants[k];
				if (std::isnan(f)) continue;
				cellv[key(e)][k].push_back(f);
				vowv[e.vowel][k].push_back(f);
				spkv[e.speaker][k].push_back(f);
			}
		}

		for (int i = 0; i < count; ++i)
		{
			auto &e = entries[i];
			if (!e.ok) continue;
			double mu[3];
			for (int k = 0; k < 3; ++k)
			{
				auto &cv = cellv[key(e)][k];
				double mc = median(cv), mv = median(vowv[e.vowel][k]), ms = median(spkv[e.speaker][k]);
				double n = (double)cv.size(), num = 0, den = 0;
				if (!std::isnan(mc)) { num += n * mc;      den += n; }
				if (!std::isnan(mv)) { num += kappa * mv;   den += kappa; }
				if (!std::isnan(ms)) { num += kappa2 * ms;  den += kappa2; }
				mu[k] = den > 0 ? num / den : std::nan("");
			}

			double best = (std::numeric_limits<double>::max)();
			int bi = e.sel;
			for (int c = 0; c < (int)e.cands.size(); ++c)
			{
				double b = e.cands[c].score.badness;
				for (int k = 0; k < 3; ++k)
				{
					double f = e.cands[c].formants[k];
					if (!std::isnan(f) && !std::isnan(mu[k]))
						b += m_lambda_s * std::abs(hertz_to_erb(f) - hertz_to_erb(mu[k]));
				}
				if (b < best) { best = b; bi = c; }
			}
			e.sel = bi;
		}
	}

	// ---- Final fill: measure each match with its selected parameters ----
	for (int i = 0; i < count; i++)
	{
		auto &match = *matches[i];
		auto &e = entries[i];
		if (!e.ok) { match.measurements.assign(field_count(), std::nan("")); continue; }
		try
		{
			const auto &win = e.cands[e.sel];
			measure_match(match, win.ceiling, win.lpc_order);
		}
		catch (...) { match.measurements.assign(field_count(), std::nan("")); }
	}
}

Handle<Query> FormantQuery::copy() const
{
	auto c = make_handle<FormantQuery>(this->parent(), String());

	// Copy base class fields
	c->m_constraints = m_constraints;
	c->m_metaconstraints = m_metaconstraints;
	c->selected_annotations = selected_annotations;
	c->m_label = m_label;
	c->m_context = m_context;
	c->m_context_length = m_context_length;
	c->m_ref_constraint = m_ref_constraint;
	c->m_include_duration = m_include_duration;
	c->m_duration_in_ms = m_duration_in_ms;

	// Copy formant-specific fields
	c->m_method = m_method;
	c->m_points = m_points;
	c->m_nformant = m_nformant;
	c->m_win_size = m_win_size;
	c->m_max_bandwidth = m_max_bandwidth;
	c->m_max_freq = m_max_freq;
	c->m_lpc_order = m_lpc_order;
	c->m_automatic = m_automatic;
	c->m_auto_method = m_auto_method;
	c->m_consensus = m_consensus;
	c->m_speaker_property = m_speaker_property;
	c->m_label_property = m_label_property;
	c->m_label_target = m_label_target;
	c->m_lambda_s = m_lambda_s;
	c->m_max_freq1 = m_max_freq1;
	c->m_max_freq2 = m_max_freq2;
	c->m_freq_step = m_freq_step;
	c->m_lpc_order1 = m_lpc_order1;
	c->m_lpc_order2 = m_lpc_order2;
	c->m_series = m_series;
	c->m_average = m_average;
	c->m_initial_layout = m_initial_layout;
	c->m_bandwidth = m_bandwidth;
	c->m_erb = m_erb;
	c->m_bark = m_bark;
	c->m_output_time = m_output_time;
	c->m_override_category = m_override_category;
	c->m_override_levels = m_override_levels;
	c->m_show_params = m_show_params;
	c->m_content_modified = true;

	return c;
}

// ── XML serialization ────────────────────────────────────────────────────────

void FormantQuery::load()
{
	xml_document doc;
	xml_node root;
	using str = std::string_view;

	try {
		root = read_xml(doc, m_path);
	}
	catch (...) {
		throw error("Cannot open formant query \"%\"", m_path);
	}

	if (root.name() != str("Phonometrica")) {
		throw error("Invalid XML root in %", m_path);
	}

	auto attr = root.attribute("label");
	if (attr) {
		set_label(attr.value(), false);
	}
	else {
		set_label(filesystem::base_name(m_path), false);
	}

	for (auto node = root.first_child(); node; node = node.next_sibling())
	{
		if (node.name() == str("Metadata"))
		{
			metadata_from_xml(node);
		}
		else if (node.name() == str("MetaConstraints"))
		{
			parse_metaconstraints_from_xml(node);
		}
		else if (node.name() == str("Constraints"))
		{
			parse_constraints_from_xml(node);
		}
		else if (node.name() == str("Options"))
		{
			parse_options_from_xml(node);
		}
		else if (node.name() == str("FormantSettings"))
		{
			// Parse formant-specific settings
			for (auto child = node.first_child(); child; child = child.next_sibling())
			{
				if (child.name() == str("Method"))
				{
					auto val = str(child.text().get());
					m_method = (val == "npoint") ? Method::NPoint : Method::Midpoint;
				}
				else if (child.name() == str("Points"))
				{
					m_points.clear();
					auto text = String(child.text().get());
					auto parts = text.split(" ");
					for (auto &p : parts)
					{
						bool ok;
						double v = p.to_float(&ok);
						if (ok) m_points.append(v);
					}
				}
				else if (child.name() == str("Nformant"))
				{
					m_nformant = child.text().as_int(3);
				}
				else if (child.name() == str("WindowSize"))
				{
					m_win_size = child.text().as_double(0.025);
				}
				else if (child.name() == str("MaxBandwidth"))
				{
					m_max_bandwidth = child.text().as_double(400);
				}
				else if (child.name() == str("Automatic"))
				{
					m_automatic = child.text().as_bool(false);
				}
				else if (child.name() == str("AutoMethod"))
				{
					auto val = str(child.text().get());
					m_auto_method = (val == "weenink") ? AutoMethod::Weenink : AutoMethod::Intrinsic;
				}
				else if (child.name() == str("Consensus"))
				{
					m_consensus = child.text().as_bool(false);
				}
				else if (child.name() == str("SpeakerProperty"))
				{
					m_speaker_property = child.text().get();
				}
				else if (child.name() == str("LabelProperty"))
				{
					m_label_property = child.text().get();
				}
				else if (child.name() == str("LabelTarget"))
				{
					m_label_target = child.text().as_int(0);
				}
				else if (child.name() == str("LambdaS"))
				{
					m_lambda_s = child.text().as_double(0.5);
				}
				else if (child.name() == str("MaxFrequency"))
				{
					m_max_freq = child.text().as_double(5500);
				}
				else if (child.name() == str("LpcOrder"))
				{
					m_lpc_order = child.text().as_int(11);
				}
				else if (child.name() == str("MaxFreqLow"))
				{
					m_max_freq1 = child.text().as_double(4000);
				}
				else if (child.name() == str("MaxFreqHigh"))
				{
					m_max_freq2 = child.text().as_double(6000);
				}
				else if (child.name() == str("FreqStep"))
				{
					m_freq_step = child.text().as_double(500);
				}
				else if (child.name() == str("LpcOrderLow"))
				{
					m_lpc_order1 = child.text().as_int(10);
				}
				else if (child.name() == str("LpcOrderHigh"))
				{
					m_lpc_order2 = child.text().as_int(12);
				}
				else if (child.name() == str("Series"))
				{
					m_series = child.text().as_bool(true);
				}
				else if (child.name() == str("NPointAverage"))
				{
					m_average = child.text().as_bool(false);
				}
				else if (child.name() == str("Layout"))
				{
					auto val = str(child.text().get());
					m_initial_layout = (val == "long") ? Concordance::Layout::Long : Concordance::Layout::Wide;
				}
				else if (child.name() == str("Bandwidth"))
				{
					m_bandwidth = child.text().as_bool(false);
				}
				else if (child.name() == str("ERB"))
				{
					m_erb = child.text().as_bool(false);
				}
				else if (child.name() == str("Bark"))
				{
					m_bark = child.text().as_bool(false);
				}
				else if (child.name() == str("OutputTime"))
				{
					m_output_time = child.text().as_bool(false);
				}
			}
		}
		else if (node.name() == str("ParameterOverride"))
		{
			auto cat_attr = node.attribute("category");
			if (cat_attr) {
				m_override_category = String(cat_attr.value());
			}
			auto show_attr = node.attribute("show");
			if (show_attr) {
				m_show_params = show_attr.as_bool(true);
			}
			m_override_levels.clear();
			for (auto level = node.first_child(); level; level = level.next_sibling())
			{
				if (level.name() != str("Level")) continue;
				auto val_attr = level.attribute("value");
				if (!val_attr) continue;
				String value(val_attr.value());
				LevelOverride ov;
				for (auto param = level.first_child(); param; param = param.next_sibling())
				{
					if (param.name() != str("Param")) continue;
					auto name_attr = param.attribute("name");
					if (!name_attr) continue;
					std::string_view name = name_attr.value();
					double v = param.text().as_double(0);
					if      (name == "MaxFrequency") ov.max_freq      = v;
					else if (name == "MaxFreqLow")   ov.max_freq_low  = v;
					else if (name == "MaxFreqHigh")  ov.max_freq_high = v;
				}
				m_override_levels[value] = ov;
			}
		}
	}

	m_loaded = true;
}

void FormantQuery::write()
{
	xml_document doc;

	auto root = doc.append_child("Phonometrica");
	root.append_attribute("class").set_value("FormantQuery");
	root.append_attribute("label").set_value(m_label.data());

	auto metadata_node = root.append_child("Metadata");
	metadata_to_xml(metadata_node);

	// MetaConstraints (reuse base class pattern)
	auto meta_node = root.append_child("MetaConstraints");
	auto file_sel_node = meta_node.append_child("FileSelection");
	for (auto &file : selected_annotations) {
		add_data_node(file_sel_node, "File", file->path());
	}
	for (auto &mc : m_metaconstraints) {
		mc->to_xml(meta_node);
	}

	// Options (context)
	auto option_node = root.append_child("Options");
	auto ctx_node = option_node.append_child("Context");
	auto type_attr = ctx_node.append_attribute("type");
	ctx_node.append_attribute("ref").set_value(m_ref_constraint);
	switch (m_context)
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
		case Context::WithinEvent:
		{
			type_attr.set_value("event");
		} break;
		default:
			type_attr.set_value("none");
	}

	if (m_include_duration) {
		auto dur_node = option_node.append_child("Duration");
		dur_node.append_attribute("enabled").set_value(true);
		dur_node.append_attribute("unit").set_value(m_duration_in_ms ? "ms" : "s");
	}

	// Constraints
	auto data_node = root.append_child("Constraints");
	for (auto &constraint : m_constraints) {
		constraint.to_xml(data_node);
	}

	// Formant settings
	auto fs_node = root.append_child("FormantSettings");
	add_data_node(fs_node, "Method", m_method == Method::NPoint ? "npoint" : "midpoint");

	if (m_method == Method::NPoint && !m_points.empty())
	{
		String pts;
		for (intptr_t i = 0; i < m_points.size(); i++)
		{
			if (i > 0) pts.append(' ');
			pts.append(String::format("%.1f", m_points[i]));
		}
		add_data_node(fs_node, "Points", pts);
		add_data_node(fs_node, "Series", String::convert(m_series));
		add_data_node(fs_node, "NPointAverage", String::convert(m_average));
		add_data_node(fs_node, "Layout", m_initial_layout == Concordance::Layout::Long ? "long" : "wide");
	}

	add_data_node(fs_node, "Nformant", String::convert((intptr_t)m_nformant));
	add_data_node(fs_node, "WindowSize", String::format("%.4f", m_win_size));
	add_data_node(fs_node, "MaxBandwidth", String::format("%.1f", m_max_bandwidth));
	add_data_node(fs_node, "Automatic", String::convert(m_automatic));

	if (m_automatic)
	{
		add_data_node(fs_node, "AutoMethod", m_auto_method == AutoMethod::Weenink ? "weenink" : "intrinsic");
		add_data_node(fs_node, "Consensus", m_consensus ? "true" : "false");
		if (!m_speaker_property.empty()) add_data_node(fs_node, "SpeakerProperty", m_speaker_property);
		if (!m_label_property.empty()) add_data_node(fs_node, "LabelProperty", m_label_property);
		add_data_node(fs_node, "LabelTarget", String::format("%d", m_label_target));
		add_data_node(fs_node, "LambdaS", String::format("%.3f", m_lambda_s));
		add_data_node(fs_node, "MaxFreqLow", String::format("%.1f", m_max_freq1));
		add_data_node(fs_node, "MaxFreqHigh", String::format("%.1f", m_max_freq2));
		add_data_node(fs_node, "FreqStep", String::format("%.1f", m_freq_step));
		add_data_node(fs_node, "LpcOrderLow", String::convert((intptr_t)m_lpc_order1));
		add_data_node(fs_node, "LpcOrderHigh", String::convert((intptr_t)m_lpc_order2));
	}
	else
	{
		add_data_node(fs_node, "MaxFrequency", String::format("%.1f", m_max_freq));
		add_data_node(fs_node, "LpcOrder", String::convert((intptr_t)m_lpc_order));
	}

	add_data_node(fs_node, "Bandwidth", String::convert(m_bandwidth));
	add_data_node(fs_node, "ERB", String::convert(m_erb));
	add_data_node(fs_node, "Bark", String::convert(m_bark));
	add_data_node(fs_node, "OutputTime", String::convert(m_output_time));

	// Per-file parameter override
	if (override_enabled())
	{
		auto ov_node = root.append_child("ParameterOverride");
		ov_node.append_attribute("category").set_value(m_override_category.data());
		ov_node.append_attribute("show").set_value(m_show_params ? "true" : "false");
		for (auto &entry : m_override_levels)
		{
			auto level_node = ov_node.append_child("Level");
			level_node.append_attribute("value").set_value(entry.first.data());
			auto emit_param = [&](const char *name, double v) {
				if (v > 0) {
					auto p = level_node.append_child("Param");
					p.append_attribute("name").set_value(name);
					p.text().set(String::format("%.1f", v).data());
				}
			};
			emit_param("MaxFrequency",  entry.second.max_freq);
			emit_param("MaxFreqLow",    entry.second.max_freq_low);
			emit_param("MaxFreqHigh",   entry.second.max_freq_high);
		}
	}

	write_xml(doc, m_path);
}

} // namespace phonometrica
