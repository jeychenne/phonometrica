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
 * Purpose: see header.                                                                                                *
 *                                                                                                                     *
 ***********************************************************************************************************************/

#include <cmath>
#include <unordered_map>
#include <stdexcept>
#include <phon/application/vowel_normalizer.hpp>

namespace phonometrica {

// ─────────────────────────────────────────────────────────
// Helpers
// ─────────────────────────────────────────────────────────

namespace {

struct PerFormantStats
{
	double sum = 0;
	double sum_sq = 0;
	int n = 0;

	void add(double x) { sum += x; sum_sq += x * x; ++n; }
	double mean() const { return n > 0 ? sum / n : 0; }
	double sd() const
	{
		if (n < 2) return 1.0; // avoid division by zero
		double m = mean();
		double var = (sum_sq - n * m * m) / (n - 1);
		return var > 0 ? std::sqrt(var) : 1.0;
	}
};

// Build a mapping from speaker label → row indices.
std::unordered_map<std::string, std::vector<size_t>>
group_by_speaker(const std::vector<std::string> &speakers)
{
	std::unordered_map<std::string, std::vector<size_t>> groups;
	for (size_t i = 0; i < speakers.size(); i++) {
		groups[speakers[i]].push_back(i);
	}
	return groups;
}

void check_lengths(const std::vector<std::span<const double>> &formants,
                   const std::vector<std::string> &speakers)
{
	if (formants.empty()) {
		throw std::runtime_error("No formant columns provided");
	}
	auto n = formants[0].size();
	for (size_t f = 1; f < formants.size(); f++) {
		if (formants[f].size() != n) {
			throw std::runtime_error("Formant columns have different lengths");
		}
	}
	if (speakers.size() != n) {
		throw std::runtime_error("Speaker column length does not match formant columns");
	}
}

} // anonymous namespace

// ─────────────────────────────────────────────────────────
// Lobanov (1971)
// ─────────────────────────────────────────────────────────

std::vector<std::vector<double>> VowelNormalizer::lobanov(
	const std::vector<std::span<const double>> &formants,
	const std::vector<std::string> &speakers)
{
	check_lengths(formants, speakers);
	auto n = formants[0].size();
	auto nf = formants.size();
	auto groups = group_by_speaker(speakers);

	// Compute per-speaker, per-formant mean and sd.
	// Key: speaker; Value: vector of PerFormantStats (one per formant).
	std::unordered_map<std::string, std::vector<PerFormantStats>> stats;
	for (auto &[spk, rows] : groups) {
		auto &s = stats[spk];
		s.resize(nf);
		for (size_t f = 0; f < nf; f++) {
			for (size_t i : rows) {
				s[f].add(formants[f][i]);
			}
		}
	}

	// Normalize.
	std::vector<std::vector<double>> result(nf, std::vector<double>(n));
	for (auto &[spk, rows] : groups) {
		auto &s = stats[spk];
		for (size_t f = 0; f < nf; f++) {
			double m = s[f].mean();
			double sd = s[f].sd();
			for (size_t i : rows) {
				result[f][i] = (formants[f][i] - m) / sd;
			}
		}
	}

	return result;
}

// ─────────────────────────────────────────────────────────
// Nearey 1 (1978) — per-formant extrinsic
// ─────────────────────────────────────────────────────────

std::vector<std::vector<double>> VowelNormalizer::nearey1(
	const std::vector<std::span<const double>> &formants,
	const std::vector<std::string> &speakers)
{
	check_lengths(formants, speakers);
	auto n = formants[0].size();
	auto nf = formants.size();
	auto groups = group_by_speaker(speakers);

	// Compute per-speaker, per-formant mean of log(F).
	std::unordered_map<std::string, std::vector<PerFormantStats>> stats;
	for (auto &[spk, rows] : groups) {
		auto &s = stats[spk];
		s.resize(nf);
		for (size_t f = 0; f < nf; f++) {
			for (size_t i : rows) {
				double v = formants[f][i];
				if (v > 0) s[f].add(std::log(v));
			}
		}
	}

	std::vector<std::vector<double>> result(nf, std::vector<double>(n));
	for (auto &[spk, rows] : groups) {
		auto &s = stats[spk];
		for (size_t f = 0; f < nf; f++) {
			double log_mean = s[f].mean();
			for (size_t i : rows) {
				double v = formants[f][i];
				result[f][i] = v > 0 ? std::log(v) - log_mean : 0;
			}
		}
	}

	return result;
}

// ─────────────────────────────────────────────────────────
// Nearey 2 (1978) — uniform (grand log-mean)
// ─────────────────────────────────────────────────────────

std::vector<std::vector<double>> VowelNormalizer::nearey2(
	const std::vector<std::span<const double>> &formants,
	const std::vector<std::string> &speakers)
{
	check_lengths(formants, speakers);
	auto n = formants[0].size();
	auto nf = formants.size();
	auto groups = group_by_speaker(speakers);

	// Compute per-speaker grand mean of log(F) across all formants.
	std::unordered_map<std::string, PerFormantStats> stats;
	for (auto &[spk, rows] : groups) {
		auto &s = stats[spk];
		for (size_t f = 0; f < nf; f++) {
			for (size_t i : rows) {
				double v = formants[f][i];
				if (v > 0) s.add(std::log(v));
			}
		}
	}

	std::vector<std::vector<double>> result(nf, std::vector<double>(n));
	for (auto &[spk, rows] : groups) {
		double grand_mean = stats[spk].mean();
		for (size_t f = 0; f < nf; f++) {
			for (size_t i : rows) {
				double v = formants[f][i];
				result[f][i] = v > 0 ? std::log(v) - grand_mean : 0;
			}
		}
	}

	return result;
}

// ─────────────────────────────────────────────────────────
// Watt & Fabricius (2002)
// ─────────────────────────────────────────────────────────

std::vector<std::vector<double>> VowelNormalizer::watt_fabricius(
	const std::vector<std::span<const double>> &formants,
	const std::vector<std::string> &speakers,
	const std::vector<std::string> &vowels,
	const std::string &label_i,
	const std::string &label_a,
	const std::string &label_u)
{
	if (formants.size() != 2) {
		throw std::runtime_error("Watt & Fabricius normalization requires exactly 2 formant columns (F1 and F2)");
	}
	check_lengths(formants, speakers);
	if (vowels.size() != formants[0].size()) {
		throw std::runtime_error("Vowel column length does not match formant columns");
	}

	auto n = formants[0].size();
	auto groups = group_by_speaker(speakers);

	// For each speaker, compute the centroid S from point vowels.
	struct Centroid { double S1 = 0; double S2 = 0; };
	std::unordered_map<std::string, Centroid> centroids;

	for (auto &[spk, rows] : groups)
	{
		// Collect mean F1, F2 for each point vowel within this speaker.
		PerFormantStats i_f1, i_f2, a_f1, a_f2, u_f2;

		for (size_t idx : rows) {
			auto &v = vowels[idx];
			if (v == label_i) {
				i_f1.add(formants[0][idx]);
				i_f2.add(formants[1][idx]);
			}
			else if (v == label_a) {
				a_f1.add(formants[0][idx]);
				a_f2.add(formants[1][idx]);
			}
			else if (v == label_u) {
				u_f2.add(formants[1][idx]);
			}
		}

		if (i_f1.n == 0 || a_f1.n == 0 || u_f2.n == 0) {
			throw std::runtime_error(
				std::string("Speaker \"") + spk +
				"\" is missing tokens for one or more point vowels (/i/, /a/, /u/)");
		}

		// Watt & Fabricius (2002): F1_u is estimated, not measured.
		// F1[u'] = F1[i] (i.e. the mean F1 of /i/ for this speaker).
		double f1_i = i_f1.mean();
		double f2_i = i_f2.mean();
		double f1_a = a_f1.mean();
		double f2_a = a_f2.mean();
		double f1_u = f1_i;        // estimated F1 for /u/
		double f2_u = u_f2.mean();

		// S = centroid of the triangle (i, a, u').
		centroids[spk] = { (f1_i + f1_a + f1_u) / 3.0,
		                   (f2_i + f2_a + f2_u) / 3.0 };
	}

	// Normalize: F_norm = (F - S) / S  (i.e. proportional distance from centroid).
	std::vector<std::vector<double>> result(2, std::vector<double>(n));
	for (auto &[spk, rows] : groups) {
		auto &c = centroids[spk];
		for (size_t idx : rows) {
			result[0][idx] = c.S1 > 0 ? (formants[0][idx] - c.S1) / c.S1 : 0;
			result[1][idx] = c.S2 > 0 ? (formants[1][idx] - c.S2) / c.S2 : 0;
		}
	}

	return result;
}

// ─────────────────────────────────────────────────────────
// Metadata helpers
// ─────────────────────────────────────────────────────────

const char *VowelNormalizer::method_suffix(VowelNormMethod method)
{
	switch (method) {
		case VowelNormMethod::Lobanov:       return "lob";
		case VowelNormMethod::Nearey1:       return "nr1";
		case VowelNormMethod::Nearey2:       return "nr2";
		case VowelNormMethod::WattFabricius: return "wf";
	}
	return "norm";
}

const char *VowelNormalizer::method_name(VowelNormMethod method)
{
	switch (method) {
		case VowelNormMethod::Lobanov:       return "Lobanov";
		case VowelNormMethod::Nearey1:       return "Nearey 1 (per-formant)";
		case VowelNormMethod::Nearey2:       return "Nearey 2 (uniform)";
		case VowelNormMethod::WattFabricius: return "Watt & Fabricius";
	}
	return "Unknown";
}

} // namespace phonometrica
