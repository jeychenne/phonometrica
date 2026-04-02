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
 * Purpose: Compute per-column distance metrics for outlier detection in phonetic data. Supports grouping              *
 *          (e.g. per speaker, per vowel) so that metrics are computed within each group. NaN values in the             *
 *          input are propagated as NaN in the output.                                                                 *
 *                                                                                                                     *
 ***********************************************************************************************************************/

#ifndef PHONOMETRICA_COLUMN_METRICS_HPP
#define PHONOMETRICA_COLUMN_METRICS_HPP

#include <vector>
#include <string>

namespace phonometrica { namespace stats {

enum class ColumnMetric
{
	// ── Univariate (single column) ──
	ZScore,              // (x - mean) / sd
	ModifiedZScore,      // 0.6745 * (x - median) / MAD  (Iglewicz & Hoaglin)
	AbsZScore,           // |z-score|
	AbsModifiedZScore,   // |modified z-score|
	Percentile,          // empirical percentile rank in [0, 1]

	// ── Multivariate (multiple columns) ──
	EuclideanDistance,    // sqrt(sum of squared z-scores across columns)
	MahalanobisDistance   // sqrt((x-μ)ᵀ Σ⁻¹ (x-μ))
};

/// True if the metric operates on multiple columns.
bool is_multivariate(ColumnMetric metric);

/// Compute a metric for a numeric column, optionally grouped.
///
/// @param values    Numeric data (NaN for missing values).
/// @param groups    Group labels, one per row. Pass an empty vector for no grouping.
///                  When non-empty, the metric is computed independently within each group.
/// @param metric    Which metric to compute.
/// @return          Result vector (same length as values). NaN where input was NaN.
std::vector<double> compute_column_metric(
	const std::vector<double> &values,
	const std::vector<std::string> &groups,
	ColumnMetric metric);

/// Compute a multivariate metric across multiple columns, optionally grouped.
///
/// @param columns   Each inner vector is one column of numeric data (same length, NaN for missing).
/// @param groups    Group labels, one per row. Pass an empty vector for no grouping.
/// @param metric    Must be EuclideanDistance or MahalanobisDistance.
/// @return          Result vector (same length as each column). NaN where any input column was NaN.
std::vector<double> compute_multivariate_metric(
	const std::vector<std::vector<double>> &columns,
	const std::vector<std::string> &groups,
	ColumnMetric metric);

/// Suggest a default threshold for flagging outliers with the given metric.
/// Returns a positive value; the caller applies it as |metric| > threshold.
double default_threshold(ColumnMetric metric);

/// Human-readable name for a metric (for UI display).
const char *metric_name(ColumnMetric metric);

/// Short suffix for auto-generated column names (e.g. "z", "mz", "absz", "absmz", "pct").
const char *metric_suffix(ColumnMetric metric);

}} // namespace phonometrica::stats

#endif // PHONOMETRICA_COLUMN_METRICS_HPP
