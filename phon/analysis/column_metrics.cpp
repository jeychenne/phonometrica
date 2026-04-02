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
 * Created: 02/04/2026                                                                                                 *
 *                                                                                                                     *
 * Purpose: see header.                                                                                                *
 *                                                                                                                     *
 ***********************************************************************************************************************/

#include <cmath>
#include <algorithm>
#include <numeric>
#include <map>
#include <phon/analysis/column_metrics.hpp>

namespace phonometrica { namespace stats {

// ─── Internal helpers ───────────────────────────────────────────────

namespace {

// Collect non-NaN values from the given indices.
std::vector<double> collect_valid(const std::vector<double> &values, const std::vector<size_t> &indices)
{
	std::vector<double> result;
	result.reserve(indices.size());
	for (auto i : indices) {
		double v = values[i];
		if (!std::isnan(v)) result.push_back(v);
	}
	return result;
}

// Median of a sorted vector (must be non-empty).
double sorted_median(const std::vector<double> &v)
{
	size_t n = v.size();
	if (n % 2 == 1)
		return v[n / 2];
	return (v[n / 2 - 1] + v[n / 2]) / 2.0;
}

// Median absolute deviation: median(|x_i - median(x)|).
double compute_mad(const std::vector<double> &sorted_vals, double median)
{
	std::vector<double> abs_dev;
	abs_dev.reserve(sorted_vals.size());
	for (double v : sorted_vals) {
		abs_dev.push_back(std::abs(v - median));
	}
	std::sort(abs_dev.begin(), abs_dev.end());
	return sorted_median(abs_dev);
}

struct GroupStats
{
	double mean = 0;
	double sd = 0;
	double median = 0;
	double mad = 0;
	size_t n = 0;
	std::vector<double> sorted_vals; // kept for percentile
};

GroupStats compute_group_stats(const std::vector<double> &values, const std::vector<size_t> &indices)
{
	GroupStats gs;
	gs.sorted_vals = collect_valid(values, indices);
	gs.n = gs.sorted_vals.size();

	if (gs.n == 0) return gs;

	std::sort(gs.sorted_vals.begin(), gs.sorted_vals.end());

	// Mean
	double sum = 0;
	for (double v : gs.sorted_vals) sum += v;
	gs.mean = sum / gs.n;

	// Standard deviation (sample)
	if (gs.n > 1) {
		double ss = 0;
		for (double v : gs.sorted_vals) {
			double d = v - gs.mean;
			ss += d * d;
		}
		gs.sd = std::sqrt(ss / (gs.n - 1));
	}

	// Median
	gs.median = sorted_median(gs.sorted_vals);

	// MAD
	gs.mad = compute_mad(gs.sorted_vals, gs.median);

	return gs;
}

double compute_zscore(double x, const GroupStats &gs)
{
	if (gs.n < 2 || gs.sd == 0) return std::nan("");
	return (x - gs.mean) / gs.sd;
}

// Modified z-score per Iglewicz & Hoaglin (1993):
//   M_i = 0.6745 * (x_i - median) / MAD
// The constant 0.6745 makes M_i comparable to the standard z-score
// under normality (it's the 75th percentile of the standard normal).
double compute_modified_zscore(double x, const GroupStats &gs)
{
	if (gs.n < 2 || gs.mad == 0) return std::nan("");
	return 0.6745 * (x - gs.median) / gs.mad;
}

double compute_percentile(double x, const GroupStats &gs)
{
	if (gs.n == 0) return std::nan("");
	// Count values strictly less than x, plus half the count of values equal to x.
	size_t below = 0;
	size_t equal = 0;
	for (double v : gs.sorted_vals) {
		if (v < x) below++;
		else if (v == x) equal++;
	}
	return (below + 0.5 * equal) / gs.n;
}

} // anonymous namespace

// ─── Public API ─────────────────────────────────────────────────────

std::vector<double> compute_column_metric(
	const std::vector<double> &values,
	const std::vector<std::string> &groups,
	ColumnMetric metric)
{
	size_t n = values.size();
	std::vector<double> result(n, std::nan(""));

	bool has_groups = !groups.empty();

	// Build group → row index mapping.
	std::map<std::string, std::vector<size_t>> group_map;

	if (has_groups) {
		for (size_t i = 0; i < n; i++) {
			group_map[groups[i]].push_back(i);
		}
	}
	else {
		// Single group with all indices.
		auto &all = group_map[""];
		all.resize(n);
		std::iota(all.begin(), all.end(), 0);
	}

	// Compute stats per group, then fill in result.
	for (auto &[key, indices] : group_map)
	{
		GroupStats gs = compute_group_stats(values, indices);

		for (auto i : indices) {
			double x = values[i];
			if (std::isnan(x)) continue;

			double v;
			switch (metric) {
			case ColumnMetric::ZScore:
				v = compute_zscore(x, gs);
				break;
			case ColumnMetric::ModifiedZScore:
				v = compute_modified_zscore(x, gs);
				break;
			case ColumnMetric::AbsZScore:
				v = compute_zscore(x, gs);
				if (!std::isnan(v)) v = std::abs(v);
				break;
			case ColumnMetric::AbsModifiedZScore:
				v = compute_modified_zscore(x, gs);
				if (!std::isnan(v)) v = std::abs(v);
				break;
			case ColumnMetric::Percentile:
				v = compute_percentile(x, gs);
				break;
			default:
				v = std::nan("");
				break;
			}

			result[i] = v;
		}
	}

	return result;
}

bool is_multivariate(ColumnMetric metric)
{
	return metric == ColumnMetric::EuclideanDistance ||
	       metric == ColumnMetric::MahalanobisDistance;
}

double default_threshold(ColumnMetric metric)
{
	switch (metric) {
	case ColumnMetric::ZScore:
	case ColumnMetric::AbsZScore:
		return 2.5;
	case ColumnMetric::ModifiedZScore:
	case ColumnMetric::AbsModifiedZScore:
		return 3.5;  // Iglewicz & Hoaglin recommendation
	case ColumnMetric::Percentile:
		return 0.99;
	case ColumnMetric::EuclideanDistance:
		return 3.0;
	case ColumnMetric::MahalanobisDistance:
		return 3.0;
	}
	return 3.0;
}

const char *metric_name(ColumnMetric metric)
{
	switch (metric) {
	case ColumnMetric::ZScore:            return "Z-score";
	case ColumnMetric::ModifiedZScore:    return "Modified z-score (robust)";
	case ColumnMetric::AbsZScore:         return "Absolute z-score";
	case ColumnMetric::AbsModifiedZScore: return "Absolute modified z-score (robust)";
	case ColumnMetric::Percentile:        return "Percentile rank";
	case ColumnMetric::EuclideanDistance:  return "Euclidean distance";
	case ColumnMetric::MahalanobisDistance: return "Mahalanobis distance";
	}
	return "";
}

const char *metric_suffix(ColumnMetric metric)
{
	switch (metric) {
	case ColumnMetric::ZScore:            return "z";
	case ColumnMetric::ModifiedZScore:    return "mz";
	case ColumnMetric::AbsZScore:         return "absz";
	case ColumnMetric::AbsModifiedZScore: return "absmz";
	case ColumnMetric::Percentile:        return "pct";
	case ColumnMetric::EuclideanDistance:  return "euclid";
	case ColumnMetric::MahalanobisDistance: return "mahal";
	}
	return "metric";
}

// ─── Multivariate metrics ───────────────────────────────────────────

std::vector<double> compute_multivariate_metric(
	const std::vector<std::vector<double>> &columns,
	const std::vector<std::string> &groups,
	ColumnMetric metric)
{
	if (columns.empty()) return {};

	size_t n = columns[0].size();
	size_t p = columns.size(); // number of variables
	std::vector<double> result(n, std::nan(""));

	bool has_groups = !groups.empty();

	// Build group → row index mapping.
	std::map<std::string, std::vector<size_t>> group_map;
	if (has_groups) {
		for (size_t i = 0; i < n; i++)
			group_map[groups[i]].push_back(i);
	}
	else {
		auto &all = group_map[""];
		all.resize(n);
		std::iota(all.begin(), all.end(), 0);
	}

	for (auto &[key, indices] : group_map)
	{
		// Collect rows where ALL columns are non-NaN.
		std::vector<size_t> valid;
		for (auto i : indices) {
			bool ok = true;
			for (size_t j = 0; j < p; j++) {
				if (std::isnan(columns[j][i])) { ok = false; break; }
			}
			if (ok) valid.push_back(i);
		}

		size_t nv = valid.size();
		if (nv < 2) continue;

		// Compute per-column mean and sd.
		std::vector<double> mean(p, 0), sd(p, 0);
		for (size_t j = 0; j < p; j++) {
			double s = 0;
			for (auto i : valid) s += columns[j][i];
			mean[j] = s / nv;
		}
		for (size_t j = 0; j < p; j++) {
			double ss = 0;
			for (auto i : valid) {
				double d = columns[j][i] - mean[j];
				ss += d * d;
			}
			sd[j] = std::sqrt(ss / (nv - 1));
		}

		if (metric == ColumnMetric::EuclideanDistance)
		{
			// Euclidean distance = L2 norm of z-scores across columns.
			for (auto i : valid) {
				double sum_sq = 0;
				bool ok = true;
				for (size_t j = 0; j < p; j++) {
					if (sd[j] == 0) { ok = false; break; }
					double z = (columns[j][i] - mean[j]) / sd[j];
					sum_sq += z * z;
				}
				result[i] = ok ? std::sqrt(sum_sq) : std::nan("");
			}
		}
		else if (metric == ColumnMetric::MahalanobisDistance)
		{
			// Build covariance matrix and invert.
			// Using simple arrays — p is typically 2–5 for formants.
			std::vector<std::vector<double>> cov(p, std::vector<double>(p, 0));
			for (size_t j1 = 0; j1 < p; j1++) {
				for (size_t j2 = j1; j2 < p; j2++) {
					double s = 0;
					for (auto i : valid) {
						s += (columns[j1][i] - mean[j1]) * (columns[j2][i] - mean[j2]);
					}
					cov[j1][j2] = s / (nv - 1);
					cov[j2][j1] = cov[j1][j2];
				}
			}

			// Cholesky decomposition of cov: L L^T = cov
			// Then solve via forward/back substitution.
			std::vector<std::vector<double>> L(p, std::vector<double>(p, 0));
			bool pd = true; // positive definite
			for (size_t i = 0; i < p && pd; i++) {
				for (size_t j = 0; j <= i; j++) {
					double s = cov[i][j];
					for (size_t k = 0; k < j; k++)
						s -= L[i][k] * L[j][k];
					if (i == j) {
						if (s <= 0) { pd = false; break; }
						L[i][j] = std::sqrt(s);
					}
					else {
						L[i][j] = s / L[j][j];
					}
				}
			}

			if (!pd) continue; // singular covariance, skip group

			// For each row: d² = (x-μ)ᵀ Σ⁻¹ (x-μ) via L.
			// Solve L y = (x - μ), then d² = y^T y.
			for (auto i : valid) {
				std::vector<double> diff(p), y(p);
				for (size_t j = 0; j < p; j++)
					diff[j] = columns[j][i] - mean[j];

				// Forward substitution: L y = diff
				for (size_t j = 0; j < p; j++) {
					double s = diff[j];
					for (size_t k = 0; k < j; k++)
						s -= L[j][k] * y[k];
					y[j] = s / L[j][j];
				}

				double d2 = 0;
				for (size_t j = 0; j < p; j++)
					d2 += y[j] * y[j];

				result[i] = std::sqrt(d2);
			}
		}
	}

	return result;
}

}} // namespace phonometrica::stats
