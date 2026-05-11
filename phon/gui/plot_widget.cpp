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
 * Created: 30/03/2026                                                                                                 *
 *                                                                                                                     *
 * Purpose: see header.                                                                                                *
 *                                                                                                                     *
 ***********************************************************************************************************************/

#include <cmath>
#include <algorithm>
#include <map>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPrinter>
#include <QSvgGenerator>
#include <phon/gui/plot_widget.hpp>

namespace phonometrica {

// ── Colors and layout ───────────────────────────────────────────────

static const QColor POINT_COLOR(0, 80, 180, 160);
static const QColor BOX_FILL(180, 210, 240, 180);
static const QColor BOX_BORDER(0, 80, 180);
static const QColor BAR_FILL(100, 160, 220, 200);
static const QColor BAR_BORDER(40, 80, 140);
static const QColor REFLINE_COLOR(200, 60, 60);
static const QColor REGLINE_COLOR(220, 80, 40);
static const QColor DENSITY_COLOR(180, 50, 160);
static const QColor GRID_COLOR(220, 220, 220);
static const QColor AXIS_COLOR(60, 60, 60);
static const QColor BG_COLOR(255, 255, 255);

// Categorical color palette (10 distinguishable colors, D3 Category10).
static const QColor GROUP_PALETTE[] = {
	QColor(31, 119, 180),    // blue
	QColor(255, 127, 14),    // orange
	QColor(44, 160, 44),     // green
	QColor(214, 39, 40),     // red
	QColor(148, 103, 189),   // purple
	QColor(140, 86, 75),     // brown
	QColor(227, 119, 194),   // pink
	QColor(127, 127, 127),   // gray
	QColor(188, 189, 34),    // olive
	QColor(23, 190, 207),    // cyan
};
static constexpr int NUM_PALETTE_COLORS = 10;

// Pen styles for the style dimension (condition).
static const Qt::PenStyle STYLE_PENS[] = {
	Qt::SolidLine, Qt::DashLine, Qt::DashDotLine, Qt::DotLine
};
static constexpr int NUM_STYLE_PENS = 4;

// Fill alpha values per style index (decreasing so overlapping ellipses remain readable).
static const int FILL_ALPHAS[] = { 30, 18, 12, 8 };

static constexpr int MARGIN_LEFT   = 65;
static constexpr int MARGIN_RIGHT  = 20;
static constexpr int MARGIN_TOP    = 30;
static constexpr int MARGIN_BOTTOM = 45;

static constexpr double POINT_RADIUS = 2.5;
static constexpr double MEAN_MARKER_SIZE = 5.0;

static constexpr int ELLIPSE_SEGMENTS = 64;


// ── Axis tick helpers ───────────────────────────────────────────────

static double nice_tick(double range)
{
	if (range <= 0) return 1;
	double rough = range / 5.0;
	double mag = std::pow(10.0, std::floor(std::log10(rough)));
	double frac = rough / mag;

	if (frac <= 1.5) return mag;
	if (frac <= 3.5) return 2 * mag;
	if (frac <= 7.5) return 5 * mag;
	return 10 * mag;
}

static void axis_range(const std::vector<double> &vals, double &lo, double &hi)
{
	if (vals.empty()) { lo = 0; hi = 1; return; }
	lo = *std::min_element(vals.begin(), vals.end());
	hi = *std::max_element(vals.begin(), vals.end());

	double range = hi - lo;
	if (range < 1e-10) range = 1.0;
	double pad = range * 0.06;
	lo -= pad;
	hi += pad;
}


// ── Constructor ─────────────────────────────────────────────────────

PlotWidget::PlotWidget(QWidget *parent) : QWidget(parent)
{
	setMinimumSize(200, 150);
	// Enable hover tracking so we can change the cursor when over a clickable
	// point even before the user presses a button.
	setMouseTracking(true);
	setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
}


// ── Marker shapes ───────────────────────────────────────────────────

void PlotWidget::drawMarker(QPainter &p, double px, double py, double r, int style_index)
{
	switch (style_index % NUM_STYLE_PENS)
	{
	case 0: // circle
		p.drawEllipse(QPointF(px, py), r, r);
		break;
	case 1: // triangle-up
	{
		double h = r * 1.15;
		QPointF pts[3] = {
			{px, py - h},
			{px - h, py + h * 0.6},
			{px + h, py + h * 0.6}
		};
		p.drawPolygon(pts, 3);
		break;
	}
	case 2: // diamond
	{
		double d = r * 1.1;
		QPointF pts[4] = {
			{px, py - d},
			{px + d, py},
			{px, py + d},
			{px - d, py}
		};
		p.drawPolygon(pts, 4);
		break;
	}
	case 3: // square
	{
		double s = r * 0.9;
		p.drawRect(QRectF(px - s, py - s, 2 * s, 2 * s));
		break;
	}
	}
}


// ── Public setters ──────────────────────────────────────────────────

void PlotWidget::setData(std::vector<double> x, std::vector<double> y,
                          const QString &x_label, const QString &y_label,
                          const QString &title, RefLine ref,
                          bool reverse_x, bool reverse_y,
                          std::vector<QString> point_labels,
                          std::vector<intptr_t> source_rows)
{
	m_mode = Mode::Scatter;
	m_x = std::move(x);
	m_y = std::move(y);
	m_point_labels = std::move(point_labels);
	m_source_rows = std::move(source_rows);
	m_x_label = x_label;
	m_y_label = y_label;
	m_title = title;
	m_ref_line = ref;
	m_reverse_x = reverse_x;
	m_reverse_y = reverse_y;
	m_boxes.clear();
	m_box_secondary_labels.clear();
	m_bins.clear();
	m_group_data.clear();
	m_color_labels.clear();
	m_style_labels.clear();
	m_use_labels = !m_point_labels.empty();
	m_cache_valid = false;
	update();
}

void PlotWidget::setGroupedScatterData(std::vector<QString> groups,
                                        std::vector<double> x, std::vector<double> y,
                                        const QString &x_label, const QString &y_label,
                                        const QString &title,
                                        bool show_means, bool show_ellipses,
                                        double chi2_scale,
                                        bool reverse_x, bool reverse_y,
                                        std::vector<QString> point_labels,
                                        std::vector<QString> style_groups,
                                        std::vector<intptr_t> source_rows,
                                        bool show_regression_lines)
{
	m_mode = Mode::GroupedScatter;
	m_group_data = buildGroups(groups, x, y, chi2_scale, point_labels,
	                           style_groups, source_rows);
	m_show_means = show_means;
	m_show_ellipses = show_ellipses;
	m_show_group_regression = show_regression_lines;
	m_use_labels = !point_labels.empty();
	m_reverse_x = reverse_x;
	m_reverse_y = reverse_y;
	m_x_label = x_label;
	m_y_label = y_label;
	m_title = title;
	m_x.clear();
	m_y.clear();
	m_point_labels.clear();
	m_source_rows.clear();
	m_boxes.clear();
	m_box_secondary_labels.clear();
	m_bins.clear();
	m_hist_group_labels.clear();
	m_bar_labels.clear();
	m_bar_counts.clear();
	m_bar_grouped_counts.clear();
	m_bar_group_labels.clear();
	m_show_regression = false;
	m_show_density = false;
	m_density_x.clear();
	m_density_y.clear();

	// Build legend label lists.
	m_color_labels.clear();
	m_style_labels.clear();

	if (!style_groups.empty())
	{
		// Extract unique base-group labels and style labels preserving insertion order.
		// We reconstruct from the group_data's color_index and style_index.
		std::map<int, QString> color_map, style_map;
		for (auto &gd : m_group_data) {
			if (color_map.find(gd.color_index) == color_map.end()) {
				// The label contains "base\x1Fstyle"; extract the base part.
				int sep = gd.label.indexOf(QChar(0x1F));
				color_map[gd.color_index] = (sep >= 0) ? gd.label.left(sep) : gd.label;
			}
			if (style_map.find(gd.style_index) == style_map.end()) {
				int sep = gd.label.indexOf(QChar(0x1F));
				style_map[gd.style_index] = (sep >= 0) ? gd.label.mid(sep + 1) : gd.label;
			}
		}
		// Convert maps (sorted by index) to vectors.
		for (auto &kv : color_map) m_color_labels.push_back(kv.second);
		for (auto &kv : style_map) m_style_labels.push_back(kv.second);
	}

	m_cache_valid = false;
	update();
}

void PlotWidget::setBoxPlotData(std::vector<QString> groups, std::vector<double> values,
                                 const QString &x_label, const QString &y_label,
                                 const QString &title,
                                 std::vector<intptr_t> source_rows,
                                 std::vector<QString> style_groups)
{
	m_mode = Mode::BoxPlot;
	m_boxes = computeBoxStats(groups, values, source_rows, style_groups);

	// Discover unique secondary levels in first-seen order — this drives the
	// legend and the color palette indices. Empty when no style_groups were
	// supplied (the renderer falls back to the single-color BOX_FILL path).
	m_box_secondary_labels.clear();
	if (!style_groups.empty())
	{
		std::map<QString, int> idx;
		size_t n = std::min(groups.size(), std::min(values.size(), style_groups.size()));
		for (size_t i = 0; i < n; i++) {
			if (idx.find(style_groups[i]) == idx.end()) {
				idx[style_groups[i]] = (int)m_box_secondary_labels.size();
				m_box_secondary_labels.push_back(style_groups[i]);
			}
		}
	}

	m_x_label = x_label;
	m_y_label = y_label;
	m_title = title;
	m_x.clear();
	m_y.clear();
	m_bins.clear();
	m_hist_group_labels.clear();
	m_source_rows.clear();
	m_group_data.clear();
	m_color_labels.clear();
	m_style_labels.clear();
	m_cache_valid = false;
	update();
}

void PlotWidget::setHistogramData(std::vector<double> values,
                                   const QString &x_label, const QString &y_label,
                                   const QString &title, int nbins)
{
	m_mode = Mode::Histogram;
	m_bins = computeBins(values, nbins);
	m_x_label = x_label;
	m_y_label = y_label;
	m_title = title;
	m_x.clear();
	m_y.clear();
	m_source_rows.clear();
	m_boxes.clear();
	m_box_secondary_labels.clear();
	m_group_data.clear();
	m_color_labels.clear();
	m_style_labels.clear();
	m_hist_group_labels.clear();
	m_show_density = false;
	m_density_x.clear();
	m_density_y.clear();
	m_cache_valid = false;
	update();
}

void PlotWidget::setHistogramData(std::vector<double> values, std::vector<QString> groups,
                                   const QString &x_label, const QString &y_label,
                                   const QString &title, int nbins)
{
	m_mode = Mode::Histogram;

	// Build shared bin edges from the pooled value range; Sturges' rule on
	// pooled n. This is what makes the overlaid histograms directly
	// comparable across groups.
	m_bins = computeBins(values, nbins);

	// Discover unique groups in first-seen order.
	m_hist_group_labels.clear();
	std::map<QString, int> idx;
	size_t n = std::min(values.size(), groups.size());
	for (size_t i = 0; i < n; i++) {
		if (idx.find(groups[i]) == idx.end()) {
			idx[groups[i]] = (int)m_hist_group_labels.size();
			m_hist_group_labels.push_back(groups[i]);
		}
	}
	int ng = (int)m_hist_group_labels.size();

	// Allocate per-group counters on every bin.
	for (auto &b : m_bins) {
		b.group_counts.assign(ng, 0);
	}

	// Tally per-group counts against the shared bin grid. Mirrors the
	// classification logic in computeBins (clamped to [0, nbins-1]).
	if (!m_bins.empty()) {
		double lo = m_bins.front().lo;
		double hi = m_bins.back().hi;
		int nb = (int)m_bins.size();
		double bin_width = (hi - lo) / nb;
		if (bin_width > 0)
		{
			for (size_t i = 0; i < n; i++) {
				auto it = idx.find(groups[i]);
				if (it == idx.end()) continue;
				int g = it->second;
				int bi = (int)((values[i] - lo) / bin_width);
				if (bi < 0) bi = 0;
				if (bi >= nb) bi = nb - 1;
				m_bins[bi].group_counts[g]++;
			}
		}
	}

	m_x_label = x_label;
	m_y_label = y_label;
	m_title = title;
	m_x.clear();
	m_y.clear();
	m_source_rows.clear();
	m_boxes.clear();
	m_box_secondary_labels.clear();
	m_group_data.clear();
	m_color_labels.clear();
	m_style_labels.clear();
	m_show_density = false;
	m_density_x.clear();
	m_density_y.clear();
	m_cache_valid = false;
	update();
}

void PlotWidget::setBarChartData(std::vector<QString> labels, std::vector<int> counts,
                                  const QString &x_label, const QString &y_label,
                                  const QString &title)
{
	m_mode = Mode::BarChart;
	m_bar_labels = std::move(labels);
	m_bar_counts = std::move(counts);
	m_bar_grouped_counts.clear();
	m_bar_group_labels.clear();
	m_x_label = x_label;
	m_y_label = y_label;
	m_title = title;
	m_x.clear();
	m_y.clear();
	m_source_rows.clear();
	m_boxes.clear();
	m_box_secondary_labels.clear();
	m_bins.clear();
	m_hist_group_labels.clear();
	m_group_data.clear();
	m_color_labels.clear();
	m_style_labels.clear();
	m_show_regression = false;
	m_cache_valid = false;
	update();
}

void PlotWidget::setBarChartData(std::vector<QString> labels,
                                  std::vector<QString> group_labels,
                                  std::vector<std::vector<int>> counts_by_group,
                                  const QString &x_label, const QString &y_label,
                                  const QString &title)
{
	m_mode = Mode::BarChart;
	m_bar_labels = std::move(labels);
	m_bar_group_labels = std::move(group_labels);
	m_bar_grouped_counts = std::move(counts_by_group);

	// Build a fallback m_bar_counts (sum across groups) so any code path
	// that reads from m_bar_counts (e.g. legacy y-range computation when
	// m_bar_grouped_counts is empty by mistake) still has something sensible.
	// renderBarChart prefers m_bar_grouped_counts when non-empty.
	m_bar_counts.assign(m_bar_labels.size(), 0);
	for (size_t g = 0; g < m_bar_grouped_counts.size(); g++) {
		for (size_t c = 0; c < m_bar_grouped_counts[g].size() && c < m_bar_counts.size(); c++) {
			m_bar_counts[c] += m_bar_grouped_counts[g][c];
		}
	}

	m_x_label = x_label;
	m_y_label = y_label;
	m_title = title;
	m_x.clear();
	m_y.clear();
	m_source_rows.clear();
	m_boxes.clear();
	m_box_secondary_labels.clear();
	m_bins.clear();
	m_hist_group_labels.clear();
	m_group_data.clear();
	m_color_labels.clear();
	m_style_labels.clear();
	m_show_regression = false;
	m_cache_valid = false;
	update();
}

void PlotWidget::setLinePlotData(std::vector<LineCurve> curves,
                                  const QString &x_label, const QString &y_label,
                                  const QString &title)
{
	m_mode = Mode::LinePlot;
	m_line_curves = std::move(curves);
	m_x_label = x_label;
	m_y_label = y_label;
	m_title = title;
	m_x.clear();
	m_y.clear();
	m_source_rows.clear();
	m_boxes.clear();
	m_box_secondary_labels.clear();
	m_bins.clear();
	m_hist_group_labels.clear();
	m_bar_labels.clear();
	m_bar_counts.clear();
	m_bar_grouped_counts.clear();
	m_bar_group_labels.clear();
	m_group_data.clear();
	m_color_labels.clear();
	m_style_labels.clear();
	m_ppc_points.clear();
	m_show_regression = false;
	m_cache_valid = false;
	update();
}

void PlotWidget::setPpcDiscreteData(std::vector<PpcBarPoint> points,
                                     const QString &x_label, const QString &y_label,
                                     const QString &title,
                                     bool integer_x_ticks)
{
	m_mode = Mode::PpcDiscrete;
	m_ppc_points = std::move(points);
	m_ppc_integer_ticks = integer_x_ticks;
	m_x_label = x_label;
	m_y_label = y_label;
	m_title = title;
	m_x.clear();
	m_y.clear();
	m_source_rows.clear();
	m_boxes.clear();
	m_box_secondary_labels.clear();
	m_bins.clear();
	m_hist_group_labels.clear();
	m_bar_labels.clear();
	m_bar_counts.clear();
	m_bar_grouped_counts.clear();
	m_bar_group_labels.clear();
	m_group_data.clear();
	m_color_labels.clear();
	m_style_labels.clear();
	m_line_curves.clear();
	m_show_regression = false;
	m_cache_valid = false;
	update();
}

void PlotWidget::setRegressionLine(double intercept, double slope, double r2)
{
	m_show_regression = true;
	m_reg_intercept = intercept;
	m_reg_slope = slope;
	m_reg_r2 = r2;
	m_cache_valid = false;
	update();
}

void PlotWidget::clearRegressionLine()
{
	m_show_regression = false;
	m_cache_valid = false;
	update();
}

void PlotWidget::setDensityCurve(std::vector<double> curve_x, std::vector<double> curve_y)
{
	m_show_density = true;
	m_density_x = std::move(curve_x);
	m_density_y = std::move(curve_y);
	m_cache_valid = false;
	update();
}

void PlotWidget::clearDensityCurve()
{
	m_show_density = false;
	m_density_x.clear();
	m_density_y.clear();
	m_cache_valid = false;
	update();
}

void PlotWidget::setFixedYTicks(std::vector<double> ticks)
{
	m_fixed_y_ticks = std::move(ticks);
	m_cache_valid = false;
	update();
}

void PlotWidget::clearFixedYTicks()
{
	m_fixed_y_ticks.clear();
	m_cache_valid = false;
	update();
}

void PlotWidget::setEffectsPlotData(std::vector<EffectsCurve> curves,
                                     const QString &x_label, const QString &y_label,
                                     const QString &title,
                                     const QString &caption,
                                     std::vector<QString> level_labels,
                                     bool show_ci,
                                     bool show_legend)
{
	m_mode = Mode::EffectsPlot;
	m_eff_curves = std::move(curves);
	m_eff_level_labels = std::move(level_labels);
	m_eff_caption = caption;
	m_eff_show_ci = show_ci;
	m_eff_show_legend = show_legend;
	m_x_label = x_label;
	m_y_label = y_label;
	m_title = title;

	// Wipe any state that other modes might have left behind, so a tab
	// switch from (e.g.) GroupedScatter to EffectsPlot doesn't show stale
	// ellipses or legend entries.
	m_x.clear();
	m_y.clear();
	m_point_labels.clear();
	m_source_rows.clear();
	m_boxes.clear();
	m_box_secondary_labels.clear();
	m_bins.clear();
	m_hist_group_labels.clear();
	m_bar_labels.clear();
	m_bar_counts.clear();
	m_bar_grouped_counts.clear();
	m_bar_group_labels.clear();
	m_group_data.clear();
	m_color_labels.clear();
	m_style_labels.clear();
	m_line_curves.clear();
	m_ppc_points.clear();
	m_show_regression = false;
	m_show_density = false;
	m_cache_valid = false;
	update();
}

void PlotWidget::setFacetedData(std::vector<FacetCell> cells,
                                 FacetInnerMode inner_mode,
                                 const QString &x_label, const QString &y_label,
                                 const QString &title,
                                 const QString &facet_var_name,
                                 std::pair<double, double> x_range,
                                 std::pair<double, double> y_range,
                                 bool shared_y_count,
                                 std::vector<QString> hist_group_labels,
                                 std::vector<QString> box_secondary_labels,
                                 std::vector<QString> bar_group_labels,
                                 std::vector<QString> color_labels,
                                 std::vector<QString> style_labels,
                                 bool reverse_x, bool reverse_y,
                                 bool show_means, bool show_ellipses,
                                 bool show_group_regression)
{
	m_mode = Mode::Facet;
	m_facet_cells = std::move(cells);
	m_facet_inner_mode = inner_mode;
	m_facet_var_name = facet_var_name;
	m_facet_x_range = x_range;
	m_facet_y_range = y_range;
	m_facet_shared_y_count = shared_y_count;
	m_facet_hist_group_labels = std::move(hist_group_labels);
	m_facet_box_secondary_labels = std::move(box_secondary_labels);
	m_facet_bar_group_labels = std::move(bar_group_labels);
	m_facet_color_labels = std::move(color_labels);
	m_facet_style_labels = std::move(style_labels);
	m_reverse_x = reverse_x;
	m_reverse_y = reverse_y;
	m_show_means = show_means;
	m_show_ellipses = show_ellipses;
	m_show_group_regression = show_group_regression;
	m_x_label = x_label;
	m_y_label = y_label;
	m_title = title;

	// Wipe per-type buffers; renderFacetGrid will swap cell data into them
	// during the inner pass.
	m_x.clear();
	m_y.clear();
	m_point_labels.clear();
	m_source_rows.clear();
	m_boxes.clear();
	m_box_secondary_labels.clear();
	m_bins.clear();
	m_hist_group_labels.clear();
	m_bar_labels.clear();
	m_bar_counts.clear();
	m_bar_grouped_counts.clear();
	m_bar_group_labels.clear();
	m_group_data.clear();
	m_color_labels.clear();
	m_style_labels.clear();
	m_line_curves.clear();
	m_ppc_points.clear();
	m_show_regression = false;
	m_show_density = false;
	m_density_x.clear();
	m_density_y.clear();
	m_cache_valid = false;
	update();
}


void PlotWidget::clear()
{
	m_mode = Mode::Empty;
	m_x.clear();
	m_y.clear();
	m_point_labels.clear();
	m_source_rows.clear();
	m_boxes.clear();
	m_box_secondary_labels.clear();
	m_bins.clear();
	m_hist_group_labels.clear();
	m_bar_labels.clear();
	m_bar_counts.clear();
	m_bar_grouped_counts.clear();
	m_bar_group_labels.clear();
	m_group_data.clear();
	m_color_labels.clear();
	m_style_labels.clear();
	m_title.clear();
	m_show_regression = false;
	m_show_density = false;
	m_show_means = false;
	m_show_ellipses = false;
	m_show_group_regression = false;
	m_use_labels = false;
	m_reverse_x = false;
	m_reverse_y = false;
	m_density_x.clear();
	m_density_y.clear();
	m_line_curves.clear();
	m_eff_curves.clear();
	m_eff_level_labels.clear();
	m_eff_caption.clear();
	m_ppc_points.clear();
	m_ppc_integer_ticks = false;
	m_fixed_y_ticks.clear();
	m_facet_cells.clear();
	m_facet_hist_group_labels.clear();
	m_facet_box_secondary_labels.clear();
	m_facet_bar_group_labels.clear();
	m_facet_color_labels.clear();
	m_facet_style_labels.clear();
	m_facet_var_name.clear();
	m_facet_render_active = false;
	m_facet_panel_title.clear();
	m_forced_xrange.reset();
	m_forced_yrange.reset();
	m_cache_valid = false;
	m_hit_targets.clear();
	update();
}

bool PlotWidget::hasData() const
{
	return m_mode != Mode::Empty;
}


// ── Statistics ──────────────────────────────────────────────────────

double PlotWidget::quantile_sorted(const std::vector<double> &sorted, double p)
{
	if (sorted.empty()) return 0;
	double index = p * (sorted.size() - 1);
	size_t lo = (size_t)std::floor(index);
	size_t hi = (size_t)std::ceil(index);
	if (lo == hi || hi >= sorted.size()) return sorted[lo];
	double frac = index - lo;
	return sorted[lo] * (1.0 - frac) + sorted[hi] * frac;
}

std::vector<PlotWidget::BoxStats> PlotWidget::computeBoxStats(
	const std::vector<QString> &groups, const std::vector<double> &values,
	const std::vector<intptr_t> &source_rows,
	const std::vector<QString> &style_groups)
{
	bool has_rows = !source_rows.empty();
	bool has_secondary = !style_groups.empty();

	// (value, source_row) pairs let us keep the row association across the
	// per-group sort that whisker/outlier extraction needs. INVALID_ROW is
	// used as a fill value when the caller didn't supply rows or when a
	// point's index runs past source_rows.size().
	using Pair = std::pair<double, intptr_t>;

	// Composite key when secondary grouping is active: "primary\x1Fsecondary"
	// (0x1F is the ASCII unit separator — same trick as the grouped-scatter
	// composite key in buildGroups). Order is preserved as first-seen.
	struct CellKey {
		QString primary;
		QString secondary;
		QString composite;  // for map lookup; primary alone when no secondary
		bool operator<(const CellKey &o) const { return composite < o.composite; }
	};

	std::vector<CellKey> order;
	std::map<QString, std::vector<Pair>> grouped;

	// Track secondary-level → palette index in first-seen order. The same
	// secondary level always gets the same color across all primaries.
	std::map<QString, int> secondary_idx;

	size_t n = std::min(groups.size(), values.size());
	if (has_secondary) n = std::min(n, style_groups.size());

	for (size_t i = 0; i < n; i++)
	{
		QString primary = groups[i];
		QString secondary = has_secondary ? style_groups[i] : QString();
		QString composite = has_secondary
			? (primary + QChar(0x1F) + secondary)
			: primary;

		if (grouped.find(composite) == grouped.end()) {
			CellKey ck;
			ck.primary = primary;
			ck.secondary = secondary;
			ck.composite = composite;
			order.push_back(ck);
		}
		if (has_secondary && secondary_idx.find(secondary) == secondary_idx.end()) {
			secondary_idx[secondary] = (int)secondary_idx.size();
		}
		intptr_t row = (has_rows && i < source_rows.size()) ? source_rows[i] : INVALID_ROW;
		grouped[composite].push_back({values[i], row});
	}

	std::vector<BoxStats> result;
	for (auto &ck : order)
	{
		auto &pairs = grouped[ck.composite];
		if (pairs.empty()) continue;

		std::sort(pairs.begin(), pairs.end(),
		          [](const Pair &a, const Pair &b) { return a.first < b.first; });

		// Build a value-only view for the existing whisker / quantile logic.
		std::vector<double> vals;
		vals.reserve(pairs.size());
		for (auto &p : pairs) vals.push_back(p.first);

		BoxStats bs;
		bs.label = ck.composite;          // display fallback; renderer uses
		                                  // primary_label for cluster axis ticks
		bs.primary_label = ck.primary;
		bs.secondary_label = ck.secondary;
		bs.secondary_index = has_secondary ? secondary_idx[ck.secondary] : -1;
		bs.median = quantile_sorted(vals, 0.5);
		bs.q1 = quantile_sorted(vals, 0.25);
		bs.q3 = quantile_sorted(vals, 0.75);

		double iqr = bs.q3 - bs.q1;
		double fence_lo = bs.q1 - 1.5 * iqr;
		double fence_hi = bs.q3 + 1.5 * iqr;

		// Whiskers extend to the most extreme data point within the fences.
		bs.whisker_lo = bs.q1;
		bs.whisker_hi = bs.q3;
		for (double v : vals)
		{
			if (v >= fence_lo) {
				bs.whisker_lo = v;
				break;
			}
		}
		for (auto it = vals.rbegin(); it != vals.rend(); ++it)
		{
			if (*it <= fence_hi) {
				bs.whisker_hi = *it;
				break;
			}
		}

		// Outliers — keep the source-row association so click-to-source can
		// dispatch correctly when the user clicks an outlier dot.
		for (auto &pair : pairs)
		{
			if (pair.first < fence_lo || pair.first > fence_hi) {
				bs.outliers.push_back(pair.first);
				bs.outlier_rows.push_back(pair.second);
			}
		}

		result.push_back(std::move(bs));
	}

	return result;
}

std::vector<PlotWidget::HistBin> PlotWidget::computeBins(const std::vector<double> &values, int nbins)
{
	if (values.empty()) return {};

	double lo = *std::min_element(values.begin(), values.end());
	double hi = *std::max_element(values.begin(), values.end());

	if (hi - lo < 1e-10) {
		lo -= 0.5;
		hi += 0.5;
	}

	// Sturges' rule if not specified
	if (nbins <= 0)
		nbins = std::max(5, (int)std::ceil(std::log2(values.size()) + 1));
	double bin_width = (hi - lo) / nbins;

	std::vector<HistBin> bins(nbins);
	for (int i = 0; i < nbins; i++)
	{
		bins[i].lo = lo + i * bin_width;
		bins[i].hi = lo + (i + 1) * bin_width;
		bins[i].count = 0;
	}

	for (double v : values)
	{
		int idx = (int)((v - lo) / bin_width);
		if (idx < 0) idx = 0;
		if (idx >= nbins) idx = nbins - 1;
		bins[idx].count++;
	}

	return bins;
}

std::vector<PlotWidget::HistBin> PlotWidget::computeGroupedBins(
	const std::vector<double> &values,
	const std::vector<QString> &groups,
	std::vector<QString> &group_labels_out,
	int nbins)
{
	// Same bin edges as the ungrouped case: based on the pooled value range.
	// This is what makes the overlay panels directly comparable.
	auto bins = computeBins(values, nbins);
	if (bins.empty()) {
		group_labels_out.clear();
		return bins;
	}

	// Discover unique groups in first-seen order, and assign each value its
	// group index. We DO NOT zero `count`: callers may consume either the
	// total or the per-group breakdown.
	group_labels_out.clear();
	std::map<QString, int> idx_map;
	size_t n = std::min(values.size(), groups.size());

	for (size_t i = 0; i < n; i++) {
		if (idx_map.find(groups[i]) == idx_map.end()) {
			idx_map[groups[i]] = (int)group_labels_out.size();
			group_labels_out.push_back(groups[i]);
		}
	}
	int ng = (int)group_labels_out.size();

	for (auto &b : bins) {
		b.group_counts.assign(ng, 0);
	}

	double lo = bins.front().lo;
	double hi = bins.back().hi;
	int nb = (int)bins.size();
	double bin_width = (hi - lo) / nb;
	if (bin_width <= 0) return bins;

	for (size_t i = 0; i < n; i++) {
		auto it = idx_map.find(groups[i]);
		if (it == idx_map.end()) continue;
		int g = it->second;
		int bi = (int)((values[i] - lo) / bin_width);
		if (bi < 0) bi = 0;
		if (bi >= nb) bi = nb - 1;
		bins[bi].group_counts[g]++;
	}

	return bins;
}


// ── Grouped scatter helpers ─────────────────────────────────────────

std::vector<PlotWidget::GroupData> PlotWidget::buildGroups(
	const std::vector<QString> &labels,
	const std::vector<double> &x,
	const std::vector<double> &y,
	double chi2_scale,
	const std::vector<QString> &point_labels,
	const std::vector<QString> &style_groups,
	const std::vector<intptr_t> &source_rows)
{
	bool has_source_rows = !source_rows.empty();

	// Partition points into groups, preserving first-seen order.
	std::vector<QString> order;
	std::map<QString, size_t> index_map;

	// Track unique base groups and style groups for index assignment.
	bool has_style = !style_groups.empty();
	std::vector<QString> base_order;
	std::map<QString, int> base_index_map;
	std::vector<QString> style_order;
	std::map<QString, int> style_index_map;

	std::vector<GroupData> groups;

	bool has_labels = !point_labels.empty();
	size_t n = std::min({labels.size(), x.size(), y.size()});
	for (size_t i = 0; i < n; i++)
	{
		// Build the composite group key.
		QString key;
		if (has_style && i < style_groups.size()) {
			key = labels[i] + QChar(0x1F) + style_groups[i];
		} else {
			key = labels[i];
		}

		auto it = index_map.find(key);
		size_t gi;
		if (it == index_map.end()) {
			gi = groups.size();
			index_map[key] = gi;
			groups.emplace_back();
			groups.back().label = key;

			// Assign color_index and style_index.
			if (has_style && i < style_groups.size()) {
				auto &base = labels[i];
				auto bit = base_index_map.find(base);
				if (bit == base_index_map.end()) {
					int ci = (int)base_order.size();
					base_order.push_back(base);
					base_index_map[base] = ci;
					groups.back().color_index = ci;
				} else {
					groups.back().color_index = bit->second;
				}

				auto &sty = style_groups[i];
				auto sit = style_index_map.find(sty);
				if (sit == style_index_map.end()) {
					int si = (int)style_order.size();
					style_order.push_back(sty);
					style_index_map[sty] = si;
					groups.back().style_index = si;
				} else {
					groups.back().style_index = sit->second;
				}
			} else {
				groups.back().color_index = (int)gi;
				groups.back().style_index = 0;
			}
		} else {
			gi = it->second;
		}
		groups[gi].x.push_back(x[i]);
		groups[gi].y.push_back(y[i]);
		if (has_labels && i < point_labels.size())
			groups[gi].symbols.push_back(point_labels[i]);
		if (has_source_rows) {
			intptr_t row = (i < source_rows.size()) ? source_rows[i] : INVALID_ROW;
			groups[gi].source_rows.push_back(row);
		}
	}

	// Compute per-group statistics: mean, OLS regression, and (n≥3) covariance → ellipse.
	for (auto &gd : groups)
	{
		size_t gn = gd.x.size();
		if (gn == 0) continue;

		// Mean
		double sx = 0, sy = 0;
		for (size_t i = 0; i < gn; i++) { sx += gd.x[i]; sy += gd.y[i]; }
		gd.mean_x = sx / gn;
		gd.mean_y = sy / gn;

		if (gn < 2) {
			gd.ellipse_valid = false;
			gd.reg_valid = false;
			continue;
		}

		// Sums of squared deviations — used by both the ellipse (after
		// dividing by n−1 to get the sample covariance) and the regression
		// (slope = sxy/sxx; R² = sxy²/(sxx·syy)). Computed once, used twice.
		double sxx = 0, syy = 0, sxy = 0;
		for (size_t i = 0; i < gn; i++) {
			double dx = gd.x[i] - gd.mean_x;
			double dy = gd.y[i] - gd.mean_y;
			sxx += dx * dx;
			syy += dy * dy;
			sxy += dx * dy;
		}

		// ── Regression. Valid whenever there is non-zero variation in x
		// (otherwise the slope is undefined). When y is constant, R² is 0
		// by convention rather than 0/0.
		if (sxx > 1e-15) {
			gd.reg_slope = sxy / sxx;
			gd.reg_intercept = gd.mean_y - gd.reg_slope * gd.mean_x;
			gd.reg_r2 = (syy > 1e-15) ? (sxy * sxy) / (sxx * syy) : 0.0;
			gd.reg_valid = true;
		} else {
			gd.reg_valid = false;
		}

		// ── Ellipse. Needs at least 3 points and a non-degenerate covariance.
		if (gn < 3) {
			gd.ellipse_valid = false;
			continue;
		}

		double cxx = sxx / (gn - 1);
		double cyy = syy / (gn - 1);
		double cxy = sxy / (gn - 1);

		// Eigenvalues of 2×2 symmetric matrix [[cxx, cxy], [cxy, cyy]].
		double trace = cxx + cyy;
		double det = cxx * cyy - cxy * cxy;
		double disc = trace * trace * 0.25 - det;

		if (disc < 0 || trace < 1e-15) {
			gd.ellipse_valid = false;
			continue;
		}

		double sqrt_disc = std::sqrt(disc);
		double lambda1 = trace * 0.5 + sqrt_disc;
		double lambda2 = trace * 0.5 - sqrt_disc;

		if (lambda1 < 1e-15 || lambda2 < 0) {
			// Degenerate — nearly collinear points.
			gd.ellipse_valid = false;
			continue;
		}
		// Clamp lambda2 to a tiny positive value for near-singular cases.
		if (lambda2 < 1e-15) lambda2 = 1e-15;

		// Rotation angle of the principal axis.
		gd.ellipse_angle = 0.5 * std::atan2(2.0 * cxy, cxx - cyy);

		// Semi-axes scaled by the chi-squared quantile for the desired confidence level.
		gd.ellipse_a = std::sqrt(lambda1 * chi2_scale);
		gd.ellipse_b = std::sqrt(lambda2 * chi2_scale);
		gd.ellipse_valid = true;
	}

	return groups;
}


// ── Rendering ───────────────────────────────────────────────────────

void PlotWidget::renderPlot(QPainter &p, int w, int h)
{
	int pw = w - MARGIN_LEFT - MARGIN_RIGHT;
	int ph = h - MARGIN_TOP - MARGIN_BOTTOM;
	if (pw <= 0 || ph <= 0) return;

	int left   = MARGIN_LEFT;
	int top    = MARGIN_TOP;

	p.setRenderHint(QPainter::Antialiasing);
	p.fillRect(0, 0, w, h, BG_COLOR);

	switch (m_mode)
	{
	case Mode::Scatter:        renderScatter(p, left, top, pw, ph, 0, 0, 0, 0); break;
	case Mode::GroupedScatter: renderGroupedScatter(p, left, top, pw, ph); break;
	case Mode::BoxPlot:        renderBoxPlot(p, left, top, pw, ph); break;
	case Mode::Histogram:      renderHistogram(p, left, top, pw, ph); break;
	case Mode::BarChart:       renderBarChart(p, left, top, pw, ph); break;
	case Mode::LinePlot:       renderLinePlot(p, left, top, pw, ph); break;
	case Mode::PpcDiscrete:    renderPpcDiscrete(p, left, top, pw, ph); break;
	case Mode::EffectsPlot:    renderEffectsPlot(p, left, top, pw, ph); break;
	case Mode::Facet:          renderFacetGrid(p, left, top, pw, ph); break;
	default: break;
	}
}

void PlotWidget::renderScatter(QPainter &p, int left, int top, int pw, int ph,
                                double, double, double, double)
{
	int bottom = top + ph;
	int right  = left + pw;

	double xlo, xhi, ylo, yhi;
	axis_range(m_x, xlo, xhi);
	axis_range(m_y, ylo, yhi);

	if (m_forced_xrange.has_value()) {
		xlo = m_forced_xrange->first;
		xhi = m_forced_xrange->second;
	}
	if (m_forced_yrange.has_value()) {
		ylo = m_forced_yrange->first;
		yhi = m_forced_yrange->second;
	}

	if (m_ref_line == RefLine::Diagonal)
	{
		double lo = std::min(xlo, ylo);
		double hi = std::max(xhi, yhi);
		xlo = ylo = lo;
		xhi = yhi = hi;
	}

	double xrange = xhi - xlo;
	double yrange = yhi - ylo;
	if (xrange <= 0) xrange = 1;
	if (yrange <= 0) yrange = 1;

	// Data-to-pixel mapping, with optional axis reversal (for formant charts).
	auto dataToX = [&](double v) -> double {
		if (m_reverse_x)
			return left + ((xhi - v) / xrange) * pw;
		return left + ((v - xlo) / xrange) * pw;
	};
	auto dataToY = [&](double v) -> double {
		if (m_reverse_y)
			return top + ((v - ylo) / yrange) * ph;
		return bottom - ((v - ylo) / yrange) * ph;
	};

	// Frame
	p.setPen(QPen(AXIS_COLOR, 1));
	p.setBrush(Qt::NoBrush);
	p.drawRect(left, top, pw, ph);

	// Axes
	QFont font;
	font.setPixelSize(11);
	p.setFont(font);
	QFontMetrics fm(font);

	// X grid + ticks
	double xtick = nice_tick(xrange);
	double x0 = std::ceil(xlo / xtick) * xtick;

	p.setPen(QPen(GRID_COLOR, 1, Qt::DotLine));
	for (double v = x0; v <= xhi; v += xtick) {
		double x = dataToX(v);
		if (x > left + 1 && x < right - 1)
			p.drawLine(QPointF(x, top), QPointF(x, bottom));
	}

	p.setPen(QPen(AXIS_COLOR, 1));
	for (double v = x0; v <= xhi; v += xtick) {
		double x = dataToX(v);
		if (x < left - 2 || x > right + 2) continue;
		p.drawLine(QPointF(x, bottom), QPointF(x, bottom + 4));
		QString label = QString::number(v, 'g', 4);
		int lw = fm.horizontalAdvance(label);
		p.drawText(int(x) - lw / 2, bottom + 4 + fm.ascent() + 2, label);
	}
	{ int tw = fm.horizontalAdvance(m_x_label); p.drawText(left + (pw - tw) / 2, bottom + MARGIN_BOTTOM - 4, m_x_label); }

	// Y grid + ticks
	std::vector<double> y_tick_values;
	if (!m_fixed_y_ticks.empty())
	{
		for (double v : m_fixed_y_ticks) {
			if (v >= ylo && v <= yhi) y_tick_values.push_back(v);
		}
	}
	else
	{
		double ytick = nice_tick(yrange);
		double y0 = std::ceil(ylo / ytick) * ytick;
		for (double v = y0; v <= yhi; v += ytick) {
			y_tick_values.push_back(v);
		}
	}

	p.setPen(QPen(GRID_COLOR, 1, Qt::DotLine));
	for (double v : y_tick_values) {
		double y = dataToY(v);
		if (y > top + 1 && y < bottom - 1)
			p.drawLine(QPointF(left, y), QPointF(right, y));
	}

	p.setPen(QPen(AXIS_COLOR, 1));
	for (double v : y_tick_values) {
		double y = dataToY(v);
		if (y < top - 2 || y > bottom + 2) continue;
		p.drawLine(QPointF(left - 4, y), QPointF(left, y));
		QString label = QString::number(v, 'g', 4);
		int lw = fm.horizontalAdvance(label);
		p.drawText(left - 6 - lw, int(y) + fm.ascent() / 2 - 1, label);
	}
	{ p.save(); p.translate(14, top + ph / 2); p.rotate(-90);
	  int tw = fm.horizontalAdvance(m_y_label); p.drawText(-tw / 2, 0, m_y_label); p.restore(); }

	// Title
	renderTitle(p, left, pw, top);

	// Reference line
	p.setClipRect(left, top, pw, ph);
	if (m_ref_line == RefLine::HorizontalAtZero)
	{
		double y = dataToY(0);
		p.setPen(QPen(REFLINE_COLOR, 1.5, Qt::DashLine));
		p.drawLine(QPointF(left, y), QPointF(right, y));
	}
	else if (m_ref_line == RefLine::HorizontalAtHalf)
	{
		double y = dataToY(0.5);
		p.setPen(QPen(REFLINE_COLOR, 1.5, Qt::DashLine));
		p.drawLine(QPointF(left, y), QPointF(right, y));
	}
	else if (m_ref_line == RefLine::Diagonal)
	{
		double lo = std::max(xlo, ylo);
		double hi = std::min(xhi, yhi);
		p.setPen(QPen(REFLINE_COLOR, 1.5, Qt::DashLine));
		p.drawLine(QPointF(dataToX(lo), dataToY(lo)), QPointF(dataToX(hi), dataToY(hi)));
	}

	// Regression line overlay
	if (m_show_regression)
	{
		// Compute line endpoints at the x-axis limits
		double y_at_xlo = m_reg_intercept + m_reg_slope * xlo;
		double y_at_xhi = m_reg_intercept + m_reg_slope * xhi;
		p.setPen(QPen(REGLINE_COLOR, 1.8));
		p.drawLine(QPointF(dataToX(xlo), dataToY(y_at_xlo)),
		           QPointF(dataToX(xhi), dataToY(y_at_xhi)));

		// R² annotation in the top-right corner of the plot area
		QFont ann_font;
		ann_font.setPixelSize(11);
		ann_font.setItalic(true);
		p.setFont(ann_font);
		p.setPen(REGLINE_COLOR);
		QString r2_text = QStringLiteral("R\u00B2 = %1").arg(m_reg_r2, 0, 'f', 4);
		QFontMetrics afm(ann_font);
		int tw = afm.horizontalAdvance(r2_text);
		p.drawText(left + pw - tw - 6, top + afm.ascent() + 4, r2_text);

		// Restore base font
		QFont base_font;
		base_font.setPixelSize(11);
		p.setFont(base_font);
	}

	// Points
	bool track_hits = m_collect_hits && !m_source_rows.empty();
	size_t n = std::min(m_x.size(), m_y.size());
	if (track_hits)
		m_hit_targets.reserve(m_hit_targets.size() + n);

	if (m_use_labels && !m_point_labels.empty())
	{
		QFont label_font;
		label_font.setPixelSize(10);
		label_font.setBold(true);
		p.setFont(label_font);
		QFontMetrics lfm(label_font);
		p.setPen(POINT_COLOR);
		p.setBrush(Qt::NoBrush);
		for (size_t i = 0; i < n; i++) {
			double x = dataToX(m_x[i]);
			double y = dataToY(m_y[i]);
			const auto &sym = (i < m_point_labels.size()) ? m_point_labels[i] : QString();
			int tw = lfm.horizontalAdvance(sym);
			p.drawText(int(x) - tw / 2, int(y) + lfm.ascent() / 2, sym);
			if (track_hits && i < m_source_rows.size()
			    && m_source_rows[i] != INVALID_ROW) {
				HitTarget ht;
				ht.pos = QPointF(x, y);
				ht.source_row = m_source_rows[i];
				m_hit_targets.push_back(ht);
			}
		}
	}
	else
	{
		p.setPen(Qt::NoPen);
		p.setBrush(POINT_COLOR);
		for (size_t i = 0; i < n; i++) {
			double x = dataToX(m_x[i]);
			double y = dataToY(m_y[i]);
			p.drawEllipse(QPointF(x, y), POINT_RADIUS, POINT_RADIUS);
			if (track_hits && i < m_source_rows.size()
			    && m_source_rows[i] != INVALID_ROW) {
				HitTarget ht;
				ht.pos = QPointF(x, y);
				ht.source_row = m_source_rows[i];
				m_hit_targets.push_back(ht);
			}
		}
	}
	p.setClipping(false);
}


// ── Grouped scatter rendering ───────────────────────────────────────

void PlotWidget::renderGroupedScatter(QPainter &p, int left, int top, int pw, int ph)
{
	if (m_group_data.empty()) return;

	int bottom = top + ph;
	int right  = left + pw;

	bool styled = !m_style_labels.empty();

	// Compute global axis ranges from all group data.
	double xlo = 1e30, xhi = -1e30, ylo = 1e30, yhi = -1e30;
	for (auto &gd : m_group_data) {
		for (double v : gd.x) { xlo = std::min(xlo, v); xhi = std::max(xhi, v); }
		for (double v : gd.y) { ylo = std::min(ylo, v); yhi = std::max(yhi, v); }
	}
	// Pad the ellipses into the axis range if they would extend beyond data bounds.
	if (m_show_ellipses)
	{
		for (auto &gd : m_group_data) {
			if (!gd.ellipse_valid) continue;
			// Conservative bound: semi-axis a is the largest possible extent.
			double extent = std::max(gd.ellipse_a, gd.ellipse_b);
			xlo = std::min(xlo, gd.mean_x - extent);
			xhi = std::max(xhi, gd.mean_x + extent);
			ylo = std::min(ylo, gd.mean_y - extent);
			yhi = std::max(yhi, gd.mean_y + extent);
		}
	}

	double xrange = xhi - xlo;
	double yrange = yhi - ylo;
	if (xrange < 1e-10) xrange = 1.0;
	if (yrange < 1e-10) yrange = 1.0;
	double xpad = xrange * 0.06;
	double ypad = yrange * 0.06;
	xlo -= xpad; xhi += xpad;
	ylo -= ypad; yhi += ypad;
	if (m_forced_xrange.has_value()) {
		xlo = m_forced_xrange->first;
		xhi = m_forced_xrange->second;
	}
	if (m_forced_yrange.has_value()) {
		ylo = m_forced_yrange->first;
		yhi = m_forced_yrange->second;
	}
	xrange = xhi - xlo;
	yrange = yhi - ylo;

	auto dataToX = [&](double v) -> double {
		if (m_reverse_x)
			return left + ((xhi - v) / xrange) * pw;
		return left + ((v - xlo) / xrange) * pw;
	};
	auto dataToY = [&](double v) -> double {
		if (m_reverse_y)
			return top + ((v - ylo) / yrange) * ph;
		return bottom - ((v - ylo) / yrange) * ph;
	};

	// Frame
	p.setPen(QPen(AXIS_COLOR, 1));
	p.setBrush(Qt::NoBrush);
	p.drawRect(left, top, pw, ph);

	// Axes
	QFont font;
	font.setPixelSize(11);
	p.setFont(font);
	QFontMetrics fm(font);

	// X grid + ticks
	double xtick = nice_tick(xrange);
	double x0 = std::ceil(xlo / xtick) * xtick;

	p.setPen(QPen(GRID_COLOR, 1, Qt::DotLine));
	for (double v = x0; v <= xhi; v += xtick) {
		double x = dataToX(v);
		if (x > left + 1 && x < right - 1)
			p.drawLine(QPointF(x, top), QPointF(x, bottom));
	}

	p.setPen(QPen(AXIS_COLOR, 1));
	for (double v = x0; v <= xhi; v += xtick) {
		double x = dataToX(v);
		if (x < left - 2 || x > right + 2) continue;
		p.drawLine(QPointF(x, bottom), QPointF(x, bottom + 4));
		QString label = QString::number(v, 'g', 4);
		int lw = fm.horizontalAdvance(label);
		p.drawText(int(x) - lw / 2, bottom + 4 + fm.ascent() + 2, label);
	}
	{ int tw = fm.horizontalAdvance(m_x_label);
	  p.drawText(left + (pw - tw) / 2, bottom + MARGIN_BOTTOM - 4, m_x_label); }

	// Y grid + ticks
	double ytick = nice_tick(yrange);
	double y0 = std::ceil(ylo / ytick) * ytick;

	p.setPen(QPen(GRID_COLOR, 1, Qt::DotLine));
	for (double v = y0; v <= yhi; v += ytick) {
		double y = dataToY(v);
		if (y > top + 1 && y < bottom - 1)
			p.drawLine(QPointF(left, y), QPointF(right, y));
	}

	p.setPen(QPen(AXIS_COLOR, 1));
	for (double v = y0; v <= yhi; v += ytick) {
		double y = dataToY(v);
		if (y < top - 2 || y > bottom + 2) continue;
		p.drawLine(QPointF(left - 4, y), QPointF(left, y));
		QString label = QString::number(v, 'g', 4);
		int lw = fm.horizontalAdvance(label);
		p.drawText(left - 6 - lw, int(y) + fm.ascent() / 2 - 1, label);
	}
	{ p.save(); p.translate(14, top + ph / 2); p.rotate(-90);
	  int tw = fm.horizontalAdvance(m_y_label);
	  p.drawText(-tw / 2, 0, m_y_label); p.restore(); }

	// Title
	renderTitle(p, left, pw, top);

	// Clip to plot area
	p.setClipRect(left, top, pw, ph);

	int ngroups = (int)m_group_data.size();

	// Draw ellipses first (behind points).
	if (m_show_ellipses)
	{
		for (int g = 0; g < ngroups; g++)
		{
			auto &gd = m_group_data[g];
			if (!gd.ellipse_valid) continue;

			QColor ec = GROUP_PALETTE[gd.color_index % NUM_PALETTE_COLORS];
			Qt::PenStyle ps = styled ? STYLE_PENS[gd.style_index % NUM_STYLE_PENS] : Qt::SolidLine;
			int alpha = styled ? FILL_ALPHAS[gd.style_index % NUM_STYLE_PENS] : 30;

			// Build parametric ellipse path in data coordinates,
			// then map to pixel coordinates.
			QPainterPath path;
			double ca = std::cos(gd.ellipse_angle);
			double sa = std::sin(gd.ellipse_angle);

			for (int i = 0; i <= ELLIPSE_SEGMENTS; i++)
			{
				double t = 2.0 * M_PI * i / ELLIPSE_SEGMENTS;
				double ex = gd.ellipse_a * std::cos(t);
				double ey = gd.ellipse_b * std::sin(t);
				// Rotate by ellipse angle
				double dx = ex * ca - ey * sa;
				double dy = ex * sa + ey * ca;

				double px = dataToX(gd.mean_x + dx);
				double py = dataToY(gd.mean_y + dy);

				if (i == 0)
					path.moveTo(px, py);
				else
					path.lineTo(px, py);
			}

			// Semi-transparent fill + styled border
			QColor fill_color = ec;
			fill_color.setAlpha(alpha);
			p.setBrush(fill_color);
			p.setPen(QPen(ec, 1.5, ps));
			p.drawPath(path);
		}
	}

	// Draw points per group.
	for (int g = 0; g < ngroups; g++)
	{
		auto &gd = m_group_data[g];
		QColor pc = GROUP_PALETTE[gd.color_index % NUM_PALETTE_COLORS];
		pc.setAlpha(180);

		size_t gn = std::min(gd.x.size(), gd.y.size());
		bool has_symbols = !gd.symbols.empty();

		// Click-to-source is enabled per group only when buildGroups received
		// a source_rows vector (i.e. setGroupedScatterData was called with
		// non-empty source_rows). Pooled-aggregate plots leave it empty.
		bool track_hits = m_collect_hits && !gd.source_rows.empty();

		if (has_symbols)
		{
			// Render text labels at each data point.
			QFont label_font;
			label_font.setPixelSize(10);
			label_font.setBold(true);
			p.setFont(label_font);
			QFontMetrics lfm(label_font);
			p.setPen(pc);
			p.setBrush(Qt::NoBrush);

			for (size_t i = 0; i < gn; i++) {
				double px = dataToX(gd.x[i]);
				double py = dataToY(gd.y[i]);
				const auto &sym = (i < gd.symbols.size()) ? gd.symbols[i] : gd.label;
				int tw = lfm.horizontalAdvance(sym);
				p.drawText(int(px) - tw / 2, int(py) + lfm.ascent() / 2, sym);
				if (track_hits && i < gd.source_rows.size()
				    && gd.source_rows[i] != INVALID_ROW) {
					HitTarget ht;
					ht.pos = QPointF(px, py);
					ht.source_row = gd.source_rows[i];
					m_hit_targets.push_back(ht);
				}
			}
		}
		else
		{
			// Draw markers (shape varies by style_index when styled).
			p.setPen(Qt::NoPen);
			p.setBrush(pc);
			for (size_t i = 0; i < gn; i++) {
				double px = dataToX(gd.x[i]);
				double py = dataToY(gd.y[i]);
				if (styled) {
					drawMarker(p, px, py, POINT_RADIUS, gd.style_index);
				} else {
					p.drawEllipse(QPointF(px, py), POINT_RADIUS, POINT_RADIUS);
				}
				if (track_hits && i < gd.source_rows.size()
				    && gd.source_rows[i] != INVALID_ROW) {
					HitTarget ht;
					ht.pos = QPointF(px, py);
					ht.source_row = gd.source_rows[i];
					m_hit_targets.push_back(ht);
				}
			}
		}
	}

	// Per-group OLS regression lines, drawn on top of points so the
	// trend is clearly visible against the scatter, but under the mean
	// markers (which always sit on top). Each line is clipped to its
	// own group's [min(x), max(x)] in data coordinates so disjoint
	// groups don't extrapolate across the whole panel — same convention
	// as ggplot's geom_smooth(method="lm").
	if (m_show_group_regression)
	{
		for (int g = 0; g < ngroups; g++)
		{
			auto &gd = m_group_data[g];
			if (!gd.reg_valid || gd.x.empty()) continue;

			double xmin = gd.x[0];
			double xmax = gd.x[0];
			for (size_t i = 1; i < gd.x.size(); i++) {
				if (gd.x[i] < xmin) xmin = gd.x[i];
				if (gd.x[i] > xmax) xmax = gd.x[i];
			}
			if (xmax <= xmin) continue; // pathological — all x identical

			double y0 = gd.reg_intercept + gd.reg_slope * xmin;
			double y1 = gd.reg_intercept + gd.reg_slope * xmax;

			QColor lc = GROUP_PALETTE[gd.color_index % NUM_PALETTE_COLORS].darker(110);
			Qt::PenStyle ps = styled ? STYLE_PENS[gd.style_index % NUM_STYLE_PENS] : Qt::SolidLine;
			p.setPen(QPen(lc, 2.0, ps));
			p.drawLine(QPointF(dataToX(xmin), dataToY(y0)),
			           QPointF(dataToX(xmax), dataToY(y1)));
		}
	}

	// Draw mean markers on top.
	if (m_show_means)
	{
		for (int g = 0; g < ngroups; g++)
		{
			auto &gd = m_group_data[g];
			QColor mc = GROUP_PALETTE[gd.color_index % NUM_PALETTE_COLORS];

			double px = dataToX(gd.mean_x);
			double py = dataToY(gd.mean_y);

			if (m_use_labels)
			{
				// Render mean as a large bold text label.
				// For styled groups, show the base group label (before the separator).
				QString display_label = gd.label;
				if (styled) {
					int sep = display_label.indexOf(QChar(0x1F));
					if (sep >= 0) display_label = display_label.left(sep);
				}

				QFont mean_font;
				mean_font.setPixelSize(16);
				mean_font.setBold(true);
				p.setFont(mean_font);
				QFontMetrics mfm(mean_font);
				int tw = mfm.horizontalAdvance(display_label);

				// White halo for readability.
				QPainterPath text_path;
				text_path.addText(px - tw / 2.0, py + mfm.ascent() / 2.0, mean_font, display_label);
				p.setPen(QPen(Qt::white, 3.0, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
				p.setBrush(Qt::NoBrush);
				p.drawPath(text_path);

				// Filled text.
				p.setPen(Qt::NoPen);
				p.setBrush(mc);
				p.drawPath(text_path);
			}
			else
			{
				// Draw a cross marker (+).
				p.setPen(QPen(mc, 2.5));
				p.drawLine(QPointF(px - MEAN_MARKER_SIZE, py),
				           QPointF(px + MEAN_MARKER_SIZE, py));
				p.drawLine(QPointF(px, py - MEAN_MARKER_SIZE),
				           QPointF(px, py + MEAN_MARKER_SIZE));

				// Filled marker behind the cross for emphasis.
				QColor fc = mc;
				fc.setAlpha(220);
				p.setPen(QPen(mc.darker(130), 1.2));
				p.setBrush(fc);
				if (styled) {
					drawMarker(p, px, py, POINT_RADIUS + 1.5, gd.style_index);
				} else {
					p.drawEllipse(QPointF(px, py), POINT_RADIUS + 1.5, POINT_RADIUS + 1.5);
				}
			}
		}
	}

	p.setClipping(false);

	// Legend
	if (!m_facet_render_active)
		renderLegend(p, left, top, pw, ph);
}


// ── Legend ───────────────────────────────────────────────────────────

void PlotWidget::renderLegend(QPainter &p, int left, int top, int pw, int ph)
{
	if (m_group_data.empty()) return;

	QFont font;
	font.setPixelSize(10);
	p.setFont(font);
	QFontMetrics fm(font);

	int swatch = 10;
	int spacing = 4;
	int line_h = std::max(fm.height(), swatch) + 2;
	int padding = 6;

	bool styled = !m_style_labels.empty();

	if (!styled)
	{
		// ── Single-factor legend (original behavior) ──
		int ngroups = (int)m_group_data.size();

		int max_label_w = 0;
		for (auto &gd : m_group_data)
			max_label_w = std::max(max_label_w, fm.horizontalAdvance(gd.label));

		int legend_w = padding + swatch + spacing + max_label_w + padding;
		int legend_h = padding + ngroups * line_h + padding;

		int lx = left + pw - legend_w - 8;
		int ly = top + 8;

		QColor bg(255, 255, 255, 210);
		p.setPen(QPen(GRID_COLOR, 1));
		p.setBrush(bg);
		p.drawRoundedRect(lx, ly, legend_w, legend_h, 3, 3);

		for (int g = 0; g < ngroups; g++)
		{
			QColor c = GROUP_PALETTE[g % NUM_PALETTE_COLORS];
			int ey = ly + padding + g * line_h;

			p.setPen(Qt::NoPen);
			p.setBrush(c);
			p.drawEllipse(QPointF(lx + padding + swatch / 2.0, ey + line_h / 2.0),
			              swatch / 2.0, swatch / 2.0);

			p.setPen(AXIS_COLOR);
			p.drawText(lx + padding + swatch + spacing,
			           ey + (line_h + fm.ascent() - fm.descent()) / 2,
			           m_group_data[g].label);
		}
	}
	else
	{
		// ── Two-factor legend: color section + style section ──
		int ncolors = (int)m_color_labels.size();
		int nstyles = (int)m_style_labels.size();

		// Measure widths.
		int max_color_w = 0;
		for (auto &lbl : m_color_labels)
			max_color_w = std::max(max_color_w, fm.horizontalAdvance(lbl));
		int max_style_w = 0;
		for (auto &lbl : m_style_labels)
			max_style_w = std::max(max_style_w, fm.horizontalAdvance(lbl));

		int line_sample_w = 24; // width of the line-style sample
		int content_w = std::max(swatch + spacing + max_color_w,
		                         line_sample_w + spacing + max_style_w);
		int legend_w = padding + content_w + padding;

		// Height: color entries + separator + style entries.
		int sep_h = 6;
		int legend_h = padding + ncolors * line_h + sep_h + nstyles * line_h + padding;

		int lx = left + pw - legend_w - 8;
		int ly = top + 8;

		QColor bg(255, 255, 255, 210);
		p.setPen(QPen(GRID_COLOR, 1));
		p.setBrush(bg);
		p.drawRoundedRect(lx, ly, legend_w, legend_h, 3, 3);

		// Color entries.
		for (int i = 0; i < ncolors; i++)
		{
			QColor c = GROUP_PALETTE[i % NUM_PALETTE_COLORS];
			int ey = ly + padding + i * line_h;

			p.setPen(Qt::NoPen);
			p.setBrush(c);
			p.drawEllipse(QPointF(lx + padding + swatch / 2.0, ey + line_h / 2.0),
			              swatch / 2.0, swatch / 2.0);

			p.setPen(AXIS_COLOR);
			p.drawText(lx + padding + swatch + spacing,
			           ey + (line_h + fm.ascent() - fm.descent()) / 2,
			           m_color_labels[i]);
		}

		// Separator line.
		int sep_y = ly + padding + ncolors * line_h + sep_h / 2;
		p.setPen(QPen(GRID_COLOR, 1));
		p.drawLine(lx + padding, sep_y, lx + legend_w - padding, sep_y);

		// Style entries (line-style sample + marker + label).
		for (int i = 0; i < nstyles; i++)
		{
			int ey = ly + padding + ncolors * line_h + sep_h + i * line_h;
			int cy = ey + line_h / 2;

			// Draw a short line segment with the appropriate pen style.
			p.setPen(QPen(AXIS_COLOR, 1.5, STYLE_PENS[i % NUM_STYLE_PENS]));
			p.setBrush(Qt::NoBrush);
			p.drawLine(lx + padding, cy, lx + padding + line_sample_w, cy);

			// Draw a small marker in the middle of the line segment.
			p.setPen(Qt::NoPen);
			p.setBrush(AXIS_COLOR);
			drawMarker(p, lx + padding + line_sample_w / 2.0, cy, 3.0, i);

			// Label
			p.setPen(AXIS_COLOR);
			p.drawText(lx + padding + line_sample_w + spacing,
			           ey + (line_h + fm.ascent() - fm.descent()) / 2,
			           m_style_labels[i]);
		}
	}
}


void PlotWidget::renderBoxPlot(QPainter &p, int left, int top, int pw, int ph)
{
	if (m_boxes.empty()) return;

	int bottom = top + ph;
	bool has_secondary = !m_box_secondary_labels.empty();

	// Cluster boxes by primary_label, preserving first-seen order. In the
	// non-dodged path each primary has exactly one box; in the dodged path
	// each primary holds N boxes, one per secondary level present.
	std::vector<QString> cluster_order;
	std::map<QString, std::vector<int>> cluster_members;  // primary → indices into m_boxes
	for (int i = 0; i < (int)m_boxes.size(); i++)
	{
		auto &pk = m_boxes[i].primary_label.isEmpty() ? m_boxes[i].label : m_boxes[i].primary_label;
		if (cluster_members.find(pk) == cluster_members.end())
			cluster_order.push_back(pk);
		cluster_members[pk].push_back(i);
	}
	int nclusters = (int)cluster_order.size();
	if (nclusters == 0) return;

	// Y range: union of all whiskers and outliers.
	double ylo = 1e30, yhi = -1e30;
	for (auto &b : m_boxes)
	{
		ylo = std::min(ylo, b.whisker_lo);
		yhi = std::max(yhi, b.whisker_hi);
		for (double o : b.outliers) {
			ylo = std::min(ylo, o);
			yhi = std::max(yhi, o);
		}
	}
	double range = yhi - ylo;
	if (range < 1e-10) range = 1;
	double pad = range * 0.06;
	ylo -= pad;
	yhi += pad;
	if (m_forced_yrange.has_value()) {
		ylo = m_forced_yrange->first;
		yhi = m_forced_yrange->second;
	}
	double yrange = yhi - ylo;

	auto dataToY = [&](double v) -> double { return bottom - ((v - ylo) / yrange) * ph; };

	// Frame
	p.setPen(QPen(AXIS_COLOR, 1));
	p.setBrush(Qt::NoBrush);
	p.drawRect(left, top, pw, ph);

	QFont font;
	font.setPixelSize(11);
	p.setFont(font);
	QFontMetrics fm(font);

	// Y grid + ticks
	double ytick = nice_tick(yrange);
	double y0 = std::ceil(ylo / ytick) * ytick;

	p.setPen(QPen(GRID_COLOR, 1, Qt::DotLine));
	for (double v = y0; v <= yhi; v += ytick) {
		double y = dataToY(v);
		if (y > top + 1 && y < bottom - 1)
			p.drawLine(QPointF(left, y), QPointF(left + pw, y));
	}

	p.setPen(QPen(AXIS_COLOR, 1));
	for (double v = y0; v <= yhi; v += ytick) {
		double y = dataToY(v);
		if (y < top - 2 || y > bottom + 2) continue;
		p.drawLine(QPointF(left - 4, y), QPointF(left, y));
		QString label = QString::number(v, 'g', 4);
		int lw = fm.horizontalAdvance(label);
		p.drawText(left - 6 - lw, int(y) + fm.ascent() / 2 - 1, label);
	}
	{ p.save(); p.translate(14, top + ph / 2); p.rotate(-90);
	  int tw = fm.horizontalAdvance(m_y_label); p.drawText(-tw / 2, 0, m_y_label); p.restore(); }

	// X-axis label
	{ int tw = fm.horizontalAdvance(m_x_label); p.drawText(left + (pw - tw) / 2, bottom + MARGIN_BOTTOM - 4, m_x_label); }

	// Title
	renderTitle(p, left, pw, top);

	// Draw boxes — geometry split by cluster width and per-cluster member
	// count for dodged layout.
	p.setClipRect(left, top, pw, ph);

	double cluster_width = (double)pw / nclusters;

	for (int c = 0; c < nclusters; c++)
	{
		auto &members = cluster_members[cluster_order[c]];
		int nmembers = (int)members.size();
		// Within a cluster, reserve 70% of the cluster width for the sub-boxes
		// (15% padding on each side). Sub-boxes are evenly spaced.
		double inner_w = cluster_width * 0.70;
		double inner_left = left + cluster_width * c + (cluster_width - inner_w) * 0.5;
		// Each sub-slot is inner_w / nmembers wide; the actual box occupies
		// 75% of the slot. For nmembers==1 (ungrouped path) the box width
		// reduces to ~52% of cluster_width, matching the previous look.
		double slot_w = inner_w / nmembers;
		double box_width = slot_w * 0.75;

		for (int k = 0; k < nmembers; k++)
		{
			auto &b = m_boxes[members[k]];
			double cx = inner_left + slot_w * (k + 0.5);

			double ymed = dataToY(b.median);
			double yq1  = dataToY(b.q1);
			double yq3  = dataToY(b.q3);
			double ywlo = dataToY(b.whisker_lo);
			double ywhi = dataToY(b.whisker_hi);

			// Pick colors: single-color path uses BOX_FILL/BOX_BORDER; dodged
			// path uses the secondary-index palette slot with a translucent fill.
			QColor fill = BOX_FILL;
			QColor border = BOX_BORDER;
			if (has_secondary && b.secondary_index >= 0)
			{
				QColor c0 = GROUP_PALETTE[b.secondary_index % NUM_PALETTE_COLORS];
				fill = c0;
				fill.setAlpha(110);
				border = c0;
			}

			// Box (Q1 to Q3)
			p.setPen(QPen(border, 1.5));
			p.setBrush(fill);
			p.drawRect(QRectF(cx - box_width / 2, yq3, box_width, yq1 - yq3));

			// Median
			p.setPen(QPen(border, 2));
			p.drawLine(QPointF(cx - box_width / 2, ymed), QPointF(cx + box_width / 2, ymed));

			// Whiskers
			p.setPen(QPen(AXIS_COLOR, 1));
			p.drawLine(QPointF(cx, yq3), QPointF(cx, ywhi));    // top whisker
			p.drawLine(QPointF(cx, yq1), QPointF(cx, ywlo));    // bottom whisker
			double cap = box_width * 0.3;
			p.drawLine(QPointF(cx - cap, ywhi), QPointF(cx + cap, ywhi));
			p.drawLine(QPointF(cx - cap, ywlo), QPointF(cx + cap, ywlo));

			// Outliers (use secondary color when present, POINT_COLOR otherwise)
			QColor outlier_color = (has_secondary && b.secondary_index >= 0) ? border : POINT_COLOR;
			p.setPen(Qt::NoPen);
			p.setBrush(outlier_color);
			bool track_hits = m_collect_hits && !b.outlier_rows.empty();
			for (size_t kk = 0; kk < b.outliers.size(); kk++) {
				double oy = dataToY(b.outliers[kk]);
				p.drawEllipse(QPointF(cx, oy), POINT_RADIUS, POINT_RADIUS);
				if (track_hits && kk < b.outlier_rows.size()
				    && b.outlier_rows[kk] != INVALID_ROW) {
					HitTarget ht;
					ht.pos = QPointF(cx, oy);
					ht.source_row = b.outlier_rows[kk];
					m_hit_targets.push_back(ht);
				}
			}
		}

		// Cluster label (below x-axis) — one label per primary, centered on
		// the cluster.
		p.setClipping(false);
		p.setPen(QPen(AXIS_COLOR, 1));
		double cx_cluster = left + cluster_width * (c + 0.5);
		const QString &cluster_lbl = cluster_order[c];
		int lw = fm.horizontalAdvance(cluster_lbl);
		p.drawText(int(cx_cluster) - lw / 2, bottom + 4 + fm.ascent() + 2, cluster_lbl);
		p.setClipRect(left, top, pw, ph);
	}

	p.setClipping(false);

	// Legend for the secondary grouping (when active).
	if (has_secondary && !m_facet_render_active)
	{
		int swatch = 10;
		int spacing = 4;
		int line_h = std::max(fm.height(), swatch) + 2;
		int padding = 6;
		int nlevels = (int)m_box_secondary_labels.size();

		int max_lbl = 0;
		for (auto &lbl : m_box_secondary_labels)
			max_lbl = std::max(max_lbl, fm.horizontalAdvance(lbl));

		int legend_w = padding + swatch + spacing + max_lbl + padding;
		int legend_h = padding + nlevels * line_h + padding;
		int lx = left + pw - legend_w - 8;
		int ly = top + 8;
		(void)legend_h;

		for (int i = 0; i < nlevels; i++)
		{
			QColor c0 = GROUP_PALETTE[i % NUM_PALETTE_COLORS];
			int ey = ly + padding + i * line_h;
			p.setPen(Qt::NoPen);
			p.setBrush(c0);
			p.drawRect(QRectF(lx + padding, ey + (line_h - swatch) / 2.0, swatch, swatch));
			p.setPen(AXIS_COLOR);
			p.drawText(lx + padding + swatch + spacing,
			           ey + (line_h + fm.ascent() - fm.descent()) / 2,
			           m_box_secondary_labels[i]);
		}
	}
}


void PlotWidget::renderHistogram(QPainter &p, int left, int top, int pw, int ph)
{
	if (m_bins.empty()) return;

	int bottom = top + ph;
	int nbins = (int)m_bins.size();
	bool grouped = !m_hist_group_labels.empty();
	int ng = (int)m_hist_group_labels.size();

	// X range from bins, unless an outer caller (faceting) forced a shared
	// range so all panels are visually comparable.
	double xlo = m_bins.front().lo;
	double xhi = m_bins.back().hi;
	if (m_forced_xrange.has_value()) {
		xlo = m_forced_xrange->first;
		xhi = m_forced_xrange->second;
	}
	double xrange = xhi - xlo;
	if (xrange <= 0) xrange = 1;

	// Y range: 0 to max count. In the grouped case, the per-bin maximum is
	// the largest single-group count in any bin (overlay layout, not stacked).
	int max_count = 0;
	if (grouped)
	{
		for (auto &b : m_bins) {
			for (int c : b.group_counts) max_count = std::max(max_count, c);
		}
	}
	else
	{
		for (auto &b : m_bins) max_count = std::max(max_count, b.count);
	}

	double ymax = (double)max_count;

	// If a density curve is present, ensure the Y range covers its peak.
	if (m_show_density) {
		for (double v : m_density_y)
			ymax = std::max(ymax, v);
	}

	double yhi = ymax * 1.08;
	if (yhi < 1) yhi = 1;
	// Faceted shared-y: outer caller has already computed the global max and
	// supplied it via m_forced_yrange (upper bound only; the lower bound
	// stays at 0 for count histograms).
	if (m_forced_yrange.has_value()) {
		yhi = m_forced_yrange->second;
		if (yhi <= 0) yhi = 1;
	}

	auto dataToX = [&](double v) -> double { return left + ((v - xlo) / xrange) * pw; };
	auto dataToY = [&](double v) -> double { return bottom - (v / yhi) * ph; };

	// Frame
	p.setPen(QPen(AXIS_COLOR, 1));
	p.setBrush(Qt::NoBrush);
	p.drawRect(left, top, pw, ph);

	QFont font;
	font.setPixelSize(11);
	p.setFont(font);
	QFontMetrics fm(font);

	// X grid + ticks
	double xtick = nice_tick(xrange);
	double x0 = std::ceil(xlo / xtick) * xtick;

	p.setPen(QPen(GRID_COLOR, 1, Qt::DotLine));
	for (double v = x0; v <= xhi; v += xtick) {
		double x = dataToX(v);
		if (x > left + 1 && x < left + pw - 1)
			p.drawLine(QPointF(x, top), QPointF(x, bottom));
	}

	p.setPen(QPen(AXIS_COLOR, 1));
	for (double v = x0; v <= xhi; v += xtick) {
		double x = dataToX(v);
		if (x < left - 2 || x > left + pw + 2) continue;
		p.drawLine(QPointF(x, bottom), QPointF(x, bottom + 4));
		QString label = QString::number(v, 'g', 4);
		int lw = fm.horizontalAdvance(label);
		p.drawText(int(x) - lw / 2, bottom + 4 + fm.ascent() + 2, label);
	}
	{ int tw = fm.horizontalAdvance(m_x_label); p.drawText(left + (pw - tw) / 2, bottom + MARGIN_BOTTOM - 4, m_x_label); }

	// Y ticks (integer counts)
	double ytick_val = nice_tick(yhi);
	p.setPen(QPen(GRID_COLOR, 1, Qt::DotLine));
	for (double v = ytick_val; v <= yhi; v += ytick_val) {
		double y = dataToY(v);
		if (y > top + 1 && y < bottom - 1)
			p.drawLine(QPointF(left, y), QPointF(left + pw, y));
	}

	p.setPen(QPen(AXIS_COLOR, 1));
	for (double v = 0; v <= yhi; v += ytick_val) {
		double y = dataToY(v);
		if (y < top - 2 || y > bottom + 2) continue;
		p.drawLine(QPointF(left - 4, y), QPointF(left, y));
		QString label = QString::number((int)v);
		int lw = fm.horizontalAdvance(label);
		p.drawText(left - 6 - lw, int(y) + fm.ascent() / 2 - 1, label);
	}
	{ p.save(); p.translate(14, top + ph / 2); p.rotate(-90);
	  int tw = fm.horizontalAdvance(m_y_label); p.drawText(-tw / 2, 0, m_y_label); p.restore(); }

	// Title
	renderTitle(p, left, pw, top);

	// Draw bars
	p.setClipRect(left, top, pw, ph);

	if (!grouped)
	{
		// Single-series histogram (original behavior).
		for (int i = 0; i < nbins; i++)
		{
			double x1 = dataToX(m_bins[i].lo);
			double x2 = dataToX(m_bins[i].hi);
			double y1 = dataToY(m_bins[i].count);
			double y2 = dataToY(0);

			p.setPen(QPen(BAR_BORDER, 1));
			p.setBrush(BAR_FILL);
			p.drawRect(QRectF(x1, y1, x2 - x1, y2 - y1));
		}
	}
	else
	{
		// Grouped (overlaid) histogram: each bin draws ng translucent
		// rectangles, one per group. Bins share the same x edges; per-group
		// counts come from HistBin::group_counts. The fill is translucent so
		// overlapping bars remain legible; outline keeps each level
		// individually traceable.
		for (int i = 0; i < nbins; i++)
		{
			double x1 = dataToX(m_bins[i].lo);
			double x2 = dataToX(m_bins[i].hi);
			double y0 = dataToY(0);

			// Draw groups in reverse so the first group ends up on top
			// (matches first-seen palette ordering in the legend).
			for (int g = ng - 1; g >= 0; g--)
			{
				int cnt = (g < (int)m_bins[i].group_counts.size())
					? m_bins[i].group_counts[g] : 0;
				if (cnt <= 0) continue;
				double y1 = dataToY(cnt);
				QColor c0 = GROUP_PALETTE[g % NUM_PALETTE_COLORS];
				QColor fill = c0; fill.setAlpha(100);
				p.setPen(QPen(c0, 1));
				p.setBrush(fill);
				p.drawRect(QRectF(x1, y1, x2 - x1, y0 - y1));
			}
		}
	}

	// Density curve overlay
	if (m_show_density && m_density_x.size() >= 2)
	{
		QPainterPath path;
		bool started = false;
		for (size_t i = 0; i < m_density_x.size(); i++)
		{
			double px = dataToX(m_density_x[i]);
			double py = dataToY(m_density_y[i]);
			if (!started) { path.moveTo(px, py); started = true; }
			else          { path.lineTo(px, py); }
		}
		p.setPen(QPen(DENSITY_COLOR, 2.0));
		p.setBrush(Qt::NoBrush);
		p.drawPath(path);
	}

	p.setClipping(false);

	// Legend for the grouped overlay.
	if (grouped && !m_facet_render_active)
	{
		int swatch = 10;
		int spacing = 4;
		int line_h = std::max(fm.height(), swatch) + 2;
		int padding = 6;
		int nlevels = ng;

		int max_lbl = 0;
		for (auto &lbl : m_hist_group_labels)
			max_lbl = std::max(max_lbl, fm.horizontalAdvance(lbl));

		int legend_w = padding + swatch + spacing + max_lbl + padding;
		int legend_h = padding + nlevels * line_h + padding;
		int lx = left + pw - legend_w - 8;
		int ly = top + 8;
		(void)legend_h;

		for (int i = 0; i < nlevels; i++)
		{
			QColor c0 = GROUP_PALETTE[i % NUM_PALETTE_COLORS];
			QColor fill = c0; fill.setAlpha(100);
			int ey = ly + padding + i * line_h;
			p.setPen(QPen(c0, 1));
			p.setBrush(fill);
			p.drawRect(QRectF(lx + padding, ey + (line_h - swatch) / 2.0, swatch, swatch));
			p.setPen(AXIS_COLOR);
			p.drawText(lx + padding + swatch + spacing,
			           ey + (line_h + fm.ascent() - fm.descent()) / 2,
			           m_hist_group_labels[i]);
		}
	}
}


void PlotWidget::renderBarChart(QPainter &p, int left, int top, int pw, int ph)
{
	if (m_bar_labels.empty()) return;

	int bottom = top + ph;
	int nbars = (int)m_bar_labels.size();
	bool grouped = !m_bar_group_labels.empty() && !m_bar_grouped_counts.empty();
	int ng = grouped ? (int)m_bar_group_labels.size() : 1;

	// Y range: max bar height. In the grouped path the max is over per-cell
	// counts (dodged, not stacked).
	int max_count = 0;
	if (grouped)
	{
		for (auto &gc : m_bar_grouped_counts)
			for (int c : gc) max_count = std::max(max_count, c);
	}
	else
	{
		for (int c : m_bar_counts) max_count = std::max(max_count, c);
	}

	double yhi = max_count * 1.08;
	if (yhi < 1) yhi = 1;

	auto dataToY = [&](double v) -> double { return bottom - (v / yhi) * ph; };

	// Frame
	p.setPen(QPen(AXIS_COLOR, 1));
	p.setBrush(Qt::NoBrush);
	p.drawRect(left, top, pw, ph);

	QFont font;
	font.setPixelSize(11);
	p.setFont(font);
	QFontMetrics fm(font);

	// Y ticks
	double ytick_val = nice_tick(yhi);

	p.setPen(QPen(GRID_COLOR, 1, Qt::DotLine));
	for (double v = ytick_val; v <= yhi; v += ytick_val) {
		double y = dataToY(v);
		if (y > top + 1 && y < bottom - 1)
			p.drawLine(QPointF(left, y), QPointF(left + pw, y));
	}

	p.setPen(QPen(AXIS_COLOR, 1));
	for (double v = 0; v <= yhi; v += ytick_val) {
		double y = dataToY(v);
		if (y < top - 2 || y > bottom + 2) continue;
		p.drawLine(QPointF(left - 4, y), QPointF(left, y));
		QString label = QString::number((int)v);
		int lw = fm.horizontalAdvance(label);
		p.drawText(left - 6 - lw, int(y) + fm.ascent() / 2 - 1, label);
	}
	{ p.save(); p.translate(14, top + ph / 2); p.rotate(-90);
	  int tw = fm.horizontalAdvance(m_y_label); p.drawText(-tw / 2, 0, m_y_label); p.restore(); }

	// X-axis label
	{ int tw = fm.horizontalAdvance(m_x_label); p.drawText(left + (pw - tw) / 2, bottom + MARGIN_BOTTOM - 4, m_x_label); }

	// Title
	renderTitle(p, left, pw, top);

	// Draw bars
	p.setClipRect(left, top, pw, ph);

	double bar_area = (double)pw / nbars;

	if (!grouped)
	{
		double bar_width = bar_area * 0.7;
		double gap = bar_area * 0.15;

		for (int i = 0; i < nbars; i++)
		{
			double x = left + bar_area * i + gap;
			double y1 = dataToY(m_bar_counts[i]);
			double y2 = dataToY(0);

			p.setPen(QPen(BAR_BORDER, 1));
			p.setBrush(BAR_FILL);
			p.drawRect(QRectF(x, y1, bar_width, y2 - y1));
		}
	}
	else
	{
		// Dodged: each category gets ng sub-bars side by side. The inner
		// region uses 70% of the category width with 15% padding on each side;
		// each sub-bar takes 80% of its slot.
		double inner_w = bar_area * 0.70;
		double slot_w = inner_w / ng;
		double sub_bar_w = slot_w * 0.80;

		for (int i = 0; i < nbars; i++)
		{
			double inner_left = left + bar_area * i + (bar_area - inner_w) * 0.5;
			for (int g = 0; g < ng; g++)
			{
				int cnt = (g < (int)m_bar_grouped_counts.size()
				           && i < (int)m_bar_grouped_counts[g].size())
					? m_bar_grouped_counts[g][i] : 0;
				if (cnt <= 0) continue;
				double x = inner_left + slot_w * g + (slot_w - sub_bar_w) * 0.5;
				double y1 = dataToY(cnt);
				double y2 = dataToY(0);
				QColor c0 = GROUP_PALETTE[g % NUM_PALETTE_COLORS];
				p.setPen(QPen(c0.darker(120), 1));
				p.setBrush(c0);
				p.drawRect(QRectF(x, y1, sub_bar_w, y2 - y1));
			}
		}
	}
	p.setClipping(false);

	// Category labels below x-axis
	p.setPen(QPen(AXIS_COLOR, 1));
	for (int i = 0; i < nbars; i++)
	{
		double cx = left + bar_area * (i + 0.5);
		int lw = fm.horizontalAdvance(m_bar_labels[i]);
		p.drawText(int(cx) - lw / 2, bottom + 4 + fm.ascent() + 2, m_bar_labels[i]);
	}

	// Legend for the grouped layout.
	if (grouped && !m_facet_render_active)
	{
		int swatch = 10;
		int spacing = 4;
		int line_h = std::max(fm.height(), swatch) + 2;
		int padding = 6;

		int max_lbl = 0;
		for (auto &lbl : m_bar_group_labels)
			max_lbl = std::max(max_lbl, fm.horizontalAdvance(lbl));

		int legend_w = padding + swatch + spacing + max_lbl + padding;
		int legend_h = padding + ng * line_h + padding;
		int lx = left + pw - legend_w - 8;
		int ly = top + 8;
		(void)legend_w; (void)legend_h;

		for (int i = 0; i < ng; i++)
		{
			QColor c0 = GROUP_PALETTE[i % NUM_PALETTE_COLORS];
			int ey = ly + padding + i * line_h;
			p.setPen(QPen(c0.darker(120), 1));
			p.setBrush(c0);
			p.drawRect(QRectF(lx + padding, ey + (line_h - swatch) / 2.0, swatch, swatch));
			p.setPen(AXIS_COLOR);
			p.drawText(lx + padding + swatch + spacing,
			           ey + (line_h + fm.ascent() - fm.descent()) / 2,
			           m_bar_group_labels[i]);
		}
	}
}


void PlotWidget::renderLinePlot(QPainter &p, int left, int top, int pw, int ph)
{
	if (m_line_curves.empty()) return;

	int bottom = top + ph;
	int right  = left + pw;

	// Compute global x/y range across all curves.
	double xlo = 1e300, xhi = -1e300;
	double ylo = 0, yhi = -1e300; // y starts at 0 for densities

	for (auto &curve : m_line_curves)
	{
		for (double v : curve.x) { xlo = std::min(xlo, v); xhi = std::max(xhi, v); }
		for (double v : curve.y) { yhi = std::max(yhi, v); }
	}

	if (xhi <= xlo) { xlo -= 1; xhi += 1; }
	if (yhi <= ylo) { yhi = ylo + 1; }

	// Add padding.
	double xrange = xhi - xlo;
	double yrange = yhi - ylo;
	double xpad = xrange * 0.04;
	double ypad = yrange * 0.08;
	xlo -= xpad; xhi += xpad;
	yhi += ypad;

	xrange = xhi - xlo;
	yrange = yhi - ylo;
	if (xrange <= 0) xrange = 1;
	if (yrange <= 0) yrange = 1;

	auto dataToX = [&](double v) -> double { return left + ((v - xlo) / xrange) * pw; };
	auto dataToY = [&](double v) -> double { return bottom - ((v - ylo) / yrange) * ph; };

	// Frame
	p.setPen(QPen(AXIS_COLOR, 1));
	p.setBrush(Qt::NoBrush);
	p.drawRect(left, top, pw, ph);

	// Axes
	QFont font;
	font.setPixelSize(11);
	p.setFont(font);
	QFontMetrics fm(font);

	// X grid + ticks
	double xtick = nice_tick(xrange);
	double x0 = std::ceil(xlo / xtick) * xtick;

	p.setPen(QPen(GRID_COLOR, 1, Qt::DotLine));
	for (double v = x0; v <= xhi; v += xtick) {
		double x = dataToX(v);
		if (x > left + 1 && x < right - 1)
			p.drawLine(QPointF(x, top), QPointF(x, bottom));
	}

	p.setPen(QPen(AXIS_COLOR, 1));
	for (double v = x0; v <= xhi; v += xtick) {
		double x = dataToX(v);
		if (x < left - 2 || x > right + 2) continue;
		p.drawLine(QPointF(x, bottom), QPointF(x, bottom + 4));
		QString label = QString::number(v, 'g', 4);
		int lw = fm.horizontalAdvance(label);
		p.drawText(int(x) - lw / 2, bottom + 4 + fm.ascent() + 2, label);
	}
	{ int tw = fm.horizontalAdvance(m_x_label); p.drawText(left + (pw - tw) / 2, bottom + MARGIN_BOTTOM - 4, m_x_label); }

	// Y grid + ticks
	double ytick = nice_tick(yrange);
	double y0 = std::ceil(ylo / ytick) * ytick;

	p.setPen(QPen(GRID_COLOR, 1, Qt::DotLine));
	for (double v = y0; v <= yhi; v += ytick) {
		double y = dataToY(v);
		if (y > top + 1 && y < bottom - 1)
			p.drawLine(QPointF(left, y), QPointF(right, y));
	}

	p.setPen(QPen(AXIS_COLOR, 1));
	for (double v = y0; v <= yhi; v += ytick) {
		double y = dataToY(v);
		if (y < top - 2 || y > bottom + 2) continue;
		p.drawLine(QPointF(left - 4, y), QPointF(left, y));
		QString label = QString::number(v, 'g', 4);
		int lw = fm.horizontalAdvance(label);
		p.drawText(left - 6 - lw, int(y) + fm.ascent() / 2 - 1, label);
	}
	{ p.save(); p.translate(14, top + ph / 2); p.rotate(-90);
	  int tw = fm.horizontalAdvance(m_y_label); p.drawText(-tw / 2, 0, m_y_label); p.restore(); }

	// Title
	renderTitle(p, left, pw, top);

	// Partition curves into background (highlight = false) and highlight
	// (highlight = true). Backgrounds render first, beneath the highlight
	// layer, so highlight curves remain visible on top of overlays.
	std::vector<int> bg_idx, hl_idx;
	bg_idx.reserve(m_line_curves.size());
	hl_idx.reserve(m_line_curves.size());
	for (int c = 0; c < (int)m_line_curves.size(); c++)
	{
		if (m_line_curves[(size_t)c].highlight)
			hl_idx.push_back(c);
		else
			bg_idx.push_back(c);
	}

	p.setClipRect(left, top, pw, ph);

	// ── Background pass ─────────────────────────────────────────────
	// Posterior-predictive replicate curves: drawn in a single muted blue,
	// thin and translucent, with no fill. Many overlapping replicates form
	// a soft band that the bold observed curve sits on top of.
	if (!bg_idx.empty())
	{
		QColor bg_color = GROUP_PALETTE[0];   // blue
		bg_color.setAlpha(70);
		QPen bg_pen(bg_color, 1.0, Qt::SolidLine);
		p.setPen(bg_pen);
		p.setBrush(Qt::NoBrush);
		for (int c : bg_idx)
		{
			auto &curve = m_line_curves[(size_t)c];
			if (curve.x.size() < 2) continue;
			for (size_t i = 1; i < curve.x.size(); i++)
				p.drawLine(QPointF(dataToX(curve.x[i-1]), dataToY(curve.y[i-1])),
				           QPointF(dataToX(curve.x[i]),   dataToY(curve.y[i])));
		}
	}

	// ── Highlight pass ──────────────────────────────────────────────
	// Each highlight curve uses a distinct palette colour with translucent
	// fill, matching the original posterior-density appearance.
	for (size_t hi = 0; hi < hl_idx.size(); hi++)
	{
		auto &curve = m_line_curves[(size_t)hl_idx[hi]];
		if (curve.x.size() < 2) continue;

		// When all curves are highlights (e.g. posterior densities), keep
		// per-curve palette colours so the user can distinguish coefficients.
		// When highlights coexist with backgrounds (PPC), just use blue so
		// the focal "y" curve reads as the salient one.
		QColor color = bg_idx.empty()
		             ? GROUP_PALETTE[(int)hi % NUM_PALETTE_COLORS]
		             : QColor(20, 50, 110);

		// Translucent fill under the curve.
		QPainterPath path;
		path.moveTo(dataToX(curve.x[0]), dataToY(0));
		for (size_t i = 0; i < curve.x.size(); i++)
			path.lineTo(dataToX(curve.x[i]), dataToY(curve.y[i]));
		path.lineTo(dataToX(curve.x.back()), dataToY(0));
		path.closeSubpath();

		QColor fill_color = color;
		fill_color.setAlpha(bg_idx.empty() ? 30 : 22);
		p.setPen(Qt::NoPen);
		p.setBrush(fill_color);
		p.drawPath(path);

		// Curve outline.
		p.setBrush(Qt::NoBrush);
		p.setPen(QPen(color, 2.0, Qt::SolidLine));
		for (size_t i = 1; i < curve.x.size(); i++)
			p.drawLine(QPointF(dataToX(curve.x[i-1]), dataToY(curve.y[i-1])),
			           QPointF(dataToX(curve.x[i]),   dataToY(curve.y[i])));
	}

	p.setClipping(false);

	// ── Legend ──────────────────────────────────────────────────────
	// When backgrounds exist, the legend collapses to two entries: the
	// observed-curve highlights (named individually) and a single "y_rep"
	// row representing the entire replicate overlay.
	bool show_legend_bg = !bg_idx.empty();
	int legend_entries = (int)hl_idx.size() + (show_legend_bg ? 1 : 0);

	if (legend_entries >= 2)
	{
		font.setPixelSize(10);
		p.setFont(font);
		QFontMetrics lfm(font);

		int line_w = 18;
		int spacing = 4;
		int padding = 6;
		int line_h = std::max(lfm.height(), 4) + 2;

		int max_label_w = 0;
		for (int c : hl_idx)
			max_label_w = std::max(max_label_w,
			                       lfm.horizontalAdvance(m_line_curves[(size_t)c].name));
		const QString rep_label = QStringLiteral("y_rep");
		if (show_legend_bg)
			max_label_w = std::max(max_label_w, lfm.horizontalAdvance(rep_label));

		int legend_w = padding + line_w + spacing + max_label_w + padding;
		int legend_h = padding + legend_entries * line_h + padding;

		int lx = left + pw - legend_w - 8;
		int ly = top + 8;

		QColor bg(255, 255, 255, 210);
		p.setPen(QPen(GRID_COLOR, 1));
		p.setBrush(bg);
		p.drawRoundedRect(lx, ly, legend_w, legend_h, 3, 3);

		int row = 0;
		for (size_t hi = 0; hi < hl_idx.size(); hi++)
		{
			QColor color = bg_idx.empty()
			             ? GROUP_PALETTE[(int)hi % NUM_PALETTE_COLORS]
			             : QColor(20, 50, 110);
			int ey = ly + padding + row * line_h;
			int cy = ey + line_h / 2;

			p.setPen(QPen(color, 2.0));
			p.drawLine(lx + padding, cy, lx + padding + line_w, cy);
			p.setPen(AXIS_COLOR);
			p.drawText(lx + padding + line_w + spacing,
			           ey + (line_h + lfm.ascent() - lfm.descent()) / 2,
			           m_line_curves[(size_t)hl_idx[hi]].name);
			row++;
		}
		if (show_legend_bg)
		{
			QColor bg_color = GROUP_PALETTE[0];
			bg_color.setAlpha(120);
			int ey = ly + padding + row * line_h;
			int cy = ey + line_h / 2;
			p.setPen(QPen(bg_color, 2.0));
			p.drawLine(lx + padding, cy, lx + padding + line_w, cy);
			p.setPen(AXIS_COLOR);
			p.drawText(lx + padding + line_w + spacing,
			           ey + (line_h + lfm.ascent() - lfm.descent()) / 2,
			           rep_label);
		}
	}
}


// ── Posterior-predictive discrete plot ───────────────────────────────
//
// Bars at (x, obs) plus an interval marker at (x, exp_mean) with vertical
// I-bars from exp_lo to exp_hi.  Used for binomial bar plots (two bars at
// {0, 1}) and for Poisson / negative-binomial rootograms (bars at integer
// counts on the √ scale).  When `m_ppc_integer_ticks` is true, x-axis ticks
// are placed at every integer in the data range; otherwise ticks are placed
// only at the supplied x positions.

void PlotWidget::renderPpcDiscrete(QPainter &p, int left, int top, int pw, int ph)
{
	if (m_ppc_points.empty()) return;

	int bottom = top + ph;
	int right  = left + pw;
	int npts = (int)m_ppc_points.size();

	// X range: a half-bar margin on either side of the supplied x positions.
	double xlo = m_ppc_points.front().x - 0.5;
	double xhi = m_ppc_points.back().x  + 0.5;
	for (auto &pt : m_ppc_points) {
		xlo = std::min(xlo, pt.x - 0.5);
		xhi = std::max(xhi, pt.x + 0.5);
	}

	// Y range: 0 to max(obs, exp_hi) with 8% headroom.
	double yhi = 0;
	for (auto &pt : m_ppc_points) {
		yhi = std::max(yhi, pt.obs);
		yhi = std::max(yhi, pt.exp_hi);
	}
	if (yhi <= 0) yhi = 1.0;
	yhi *= 1.08;
	double ylo = 0;

	double xrange = xhi - xlo;
	double yrange = yhi - ylo;
	if (xrange <= 0) xrange = 1;
	if (yrange <= 0) yrange = 1;

	auto dataToX = [&](double v) -> double { return left + ((v - xlo) / xrange) * pw; };
	auto dataToY = [&](double v) -> double { return bottom - ((v - ylo) / yrange) * ph; };

	// Frame
	p.setPen(QPen(AXIS_COLOR, 1));
	p.setBrush(Qt::NoBrush);
	p.drawRect(left, top, pw, ph);

	QFont font;
	font.setPixelSize(11);
	p.setFont(font);
	QFontMetrics fm(font);

	// X ticks: integer ticks for rootograms, supplied positions for bars.
	p.setPen(QPen(AXIS_COLOR, 1));
	if (m_ppc_integer_ticks)
	{
		// Sparse tick spacing for crowded rootograms (every k-th integer).
		int support = (int)std::lround(m_ppc_points.back().x - m_ppc_points.front().x) + 1;
		int step = 1;
		if (support > 25) step = 5;
		else if (support > 12) step = 2;

		int kmin = (int)std::lround(m_ppc_points.front().x);
		int kmax = (int)std::lround(m_ppc_points.back().x);
		for (int k = kmin; k <= kmax; k++) {
			if ((k - kmin) % step != 0) continue;
			double x = dataToX((double)k);
			if (x < left - 2 || x > right + 2) continue;
			p.drawLine(QPointF(x, bottom), QPointF(x, bottom + 4));
			QString label = QString::number(k);
			int lw = fm.horizontalAdvance(label);
			p.drawText(int(x) - lw / 2, bottom + 4 + fm.ascent() + 2, label);
		}
	}
	else
	{
		for (auto &pt : m_ppc_points) {
			double x = dataToX(pt.x);
			if (x < left - 2 || x > right + 2) continue;
			p.drawLine(QPointF(x, bottom), QPointF(x, bottom + 4));
			QString label = QString::number((int)std::lround(pt.x));
			int lw = fm.horizontalAdvance(label);
			p.drawText(int(x) - lw / 2, bottom + 4 + fm.ascent() + 2, label);
		}
	}
	{ int tw = fm.horizontalAdvance(m_x_label); p.drawText(left + (pw - tw) / 2, bottom + MARGIN_BOTTOM - 4, m_x_label); }

	// Y grid + ticks
	double ytick = nice_tick(yrange);
	double y0 = std::ceil(ylo / ytick) * ytick;

	p.setPen(QPen(GRID_COLOR, 1, Qt::DotLine));
	for (double v = y0; v <= yhi; v += ytick) {
		double y = dataToY(v);
		if (y > top + 1 && y < bottom - 1)
			p.drawLine(QPointF(left, y), QPointF(right, y));
	}

	p.setPen(QPen(AXIS_COLOR, 1));
	for (double v = y0; v <= yhi; v += ytick) {
		double y = dataToY(v);
		if (y < top - 2 || y > bottom + 2) continue;
		p.drawLine(QPointF(left - 4, y), QPointF(left, y));
		QString label = QString::number(v, 'g', 4);
		int lw = fm.horizontalAdvance(label);
		p.drawText(left - 6 - lw, int(y) + fm.ascent() / 2 - 1, label);
	}
	{ p.save(); p.translate(14, top + ph / 2); p.rotate(-90);
	  int tw = fm.horizontalAdvance(m_y_label); p.drawText(-tw / 2, 0, m_y_label); p.restore(); }

	// Title
	renderTitle(p, left, pw, top);

	// ── Draw observed bars ──────────────────────────────────────────
	p.setClipRect(left, top, pw, ph);

	// Bar half-width: for rootograms with many bins, slim bars; for
	// binomial (2 bars), wider bars.
	double bar_half_w_data = (npts <= 4) ? 0.32 : 0.42;
	double y_zero = dataToY(0);

	for (auto &pt : m_ppc_points)
	{
		if (pt.obs <= 0) continue;
		double cx = dataToX(pt.x);
		double bw_left  = dataToX(pt.x - bar_half_w_data);
		double bw_right = dataToX(pt.x + bar_half_w_data);
		double y_top = dataToY(pt.obs);
		QRectF bar_rect(bw_left, y_top, bw_right - bw_left, y_zero - y_top);

		p.setPen(QPen(BAR_BORDER, 1));
		p.setBrush(BAR_FILL);
		p.drawRect(bar_rect);
		(void)cx; // kept for symmetry with marker pass below
	}

	// ── Draw posterior-predictive intervals ─────────────────────────
	// I-bar at each x: vertical line from exp_lo to exp_hi with short
	// horizontal caps, plus a filled circle marker at exp_mean.
	QColor exp_color(180, 50, 70);          // darker red, distinct from BAR_BORDER
	QColor exp_marker = exp_color;
	double cap_half_w_px = 4.0;
	double marker_radius = 3.0;

	// Connecting line through expected medians (rootogram style); for binomial
	// (only 2 points) the line still draws and reads as a slope cue.
	if (npts >= 2)
	{
		QPen line_pen(exp_color, 1.0, Qt::DashLine);
		p.setPen(line_pen);
		p.setBrush(Qt::NoBrush);
		for (int i = 1; i < npts; i++)
		{
			QPointF a(dataToX(m_ppc_points[(size_t)i-1].x),
			          dataToY(m_ppc_points[(size_t)i-1].exp_mean));
			QPointF b(dataToX(m_ppc_points[(size_t)i].x),
			          dataToY(m_ppc_points[(size_t)i].exp_mean));
			p.drawLine(a, b);
		}
	}

	for (auto &pt : m_ppc_points)
	{
		double x = dataToX(pt.x);
		double y_lo = dataToY(pt.exp_lo);
		double y_hi = dataToY(pt.exp_hi);
		double y_md = dataToY(pt.exp_mean);

		// Vertical interval line
		p.setPen(QPen(exp_color, 1.5));
		p.setBrush(Qt::NoBrush);
		p.drawLine(QPointF(x, y_lo), QPointF(x, y_hi));
		// Caps
		p.drawLine(QPointF(x - cap_half_w_px, y_lo), QPointF(x + cap_half_w_px, y_lo));
		p.drawLine(QPointF(x - cap_half_w_px, y_hi), QPointF(x + cap_half_w_px, y_hi));

		// Median marker
		p.setPen(QPen(exp_color, 1.0));
		p.setBrush(exp_marker);
		p.drawEllipse(QPointF(x, y_md), marker_radius, marker_radius);
	}

	p.setClipping(false);

	// ── Legend (top-right) ──────────────────────────────────────────
	{
		font.setPixelSize(10);
		p.setFont(font);
		QFontMetrics lfm(font);

		const QString obs_label = QStringLiteral("y (observed)");
		const QString rep_label = QStringLiteral("y_rep (5\u201395%)");

		int line_w = 18;
		int spacing = 4;
		int padding = 6;
		int line_h = std::max(lfm.height(), 4) + 2;
		int max_label_w = std::max(lfm.horizontalAdvance(obs_label),
		                            lfm.horizontalAdvance(rep_label));
		int legend_w = padding + line_w + spacing + max_label_w + padding;
		int legend_h = padding + 2 * line_h + padding;

		int lx = left + pw - legend_w - 8;
		int ly = top + 8;

		QColor bg(255, 255, 255, 210);
		p.setPen(QPen(GRID_COLOR, 1));
		p.setBrush(bg);
		p.drawRoundedRect(lx, ly, legend_w, legend_h, 3, 3);

		// Bar swatch row
		int ey = ly + padding;
		QRectF swatch(lx + padding, ey + line_h / 2 - 4, line_w, 8);
		p.setPen(QPen(BAR_BORDER, 1));
		p.setBrush(BAR_FILL);
		p.drawRect(swatch);
		p.setPen(AXIS_COLOR);
		p.drawText(lx + padding + line_w + spacing,
		           ey + (line_h + lfm.ascent() - lfm.descent()) / 2,
		           obs_label);

		// Interval row (I-bar + marker)
		ey += line_h;
		int cy = ey + line_h / 2;
		p.setPen(QPen(exp_color, 1.5));
		int x0 = lx + padding;
		int x1 = lx + padding + line_w;
		int xm = (x0 + x1) / 2;
		p.drawLine(QPointF(xm, cy - 5), QPointF(xm, cy + 5));
		p.drawLine(QPointF(xm - 3, cy - 5), QPointF(xm + 3, cy - 5));
		p.drawLine(QPointF(xm - 3, cy + 5), QPointF(xm + 3, cy + 5));
		p.setPen(QPen(exp_color, 1.0));
		p.setBrush(exp_marker);
		p.drawEllipse(QPointF(xm, cy), 3.0, 3.0);
		p.setPen(AXIS_COLOR);
		p.drawText(lx + padding + line_w + spacing,
		           ey + (line_h + lfm.ascent() - lfm.descent()) / 2,
		           rep_label);
	}
}


// ── Effects plot rendering ──────────────────────────────────────────
//
// One or more curves (numeric focal: lines + ribbons; categorical focal:
// connected markers + error bars), each colored from GROUP_PALETTE in
// curve order. NaN entries in fit/ci are skipped — they correspond to
// rows of the reference grid that predict() refused (unseen levels,
// missing predictors). Multi-curve plots get a top-right legend keyed
// on EffectsCurve::label.

void PlotWidget::renderEffectsPlot(QPainter &p, int left, int top, int pw, int ph)
{
	if (m_eff_curves.empty()) return;

	const bool categorical = !m_eff_level_labels.empty();
	const int  ncurves = (int) m_eff_curves.size();

	// Only show a legend when at least one curve carries a label. Single
	// curves with empty labels (the original Phase 2 behaviour) get no
	// legend.
	bool any_label = false;
	for (auto &c : m_eff_curves) if (!c.label.isEmpty()) { any_label = true; break; }

	int bottom = top + ph;
	int right  = left + pw;

	// ── Compute axis ranges across all curves ───────────────────────
	double xlo = 1e300, xhi = -1e300;
	double ylo = 1e300, yhi = -1e300;
	for (auto &c : m_eff_curves)
	{
		if (c.fit.size() != c.x.size()
		    || c.ci_lower.size() != c.x.size()
		    || c.ci_upper.size() != c.x.size())
			continue;

		for (size_t i = 0; i < c.x.size(); i++)
		{
			if (std::isnan(c.fit[i])) continue;
			if (categorical) {
				xlo = std::min(xlo, c.x[i] - 0.5);
				xhi = std::max(xhi, c.x[i] + 0.5);
			} else {
				xlo = std::min(xlo, c.x[i]);
				xhi = std::max(xhi, c.x[i]);
			}
			double lo = std::isnan(c.ci_lower[i]) ? c.fit[i] : c.ci_lower[i];
			double hi = std::isnan(c.ci_upper[i]) ? c.fit[i] : c.ci_upper[i];
			ylo = std::min(ylo, std::min(lo, c.fit[i]));
			yhi = std::max(yhi, std::max(hi, c.fit[i]));
		}
	}
	if (xlo == 1e300 || ylo == 1e300) return; // all NaN / empty

	if (xhi <= xlo) { xlo -= 1; xhi += 1; }
	if (yhi <= ylo) { yhi = ylo + 1; }

	double xrange = xhi - xlo;
	double yrange = yhi - ylo;
	double xpad = xrange * 0.04;
	double ypad = yrange * 0.08;
	if (!categorical) { xlo -= xpad; xhi += xpad; }
	ylo -= ypad; yhi += ypad;

	xrange = xhi - xlo;
	yrange = yhi - ylo;
	if (xrange <= 0) xrange = 1;
	if (yrange <= 0) yrange = 1;

	auto dataToX = [&](double v) -> double { return left + ((v - xlo) / xrange) * pw; };
	auto dataToY = [&](double v) -> double { return bottom - ((v - ylo) / yrange) * ph; };

	// ── Frame ───────────────────────────────────────────────────────
	p.setPen(QPen(AXIS_COLOR, 1));
	p.setBrush(Qt::NoBrush);
	p.drawRect(left, top, pw, ph);

	QFont font;
	font.setPixelSize(11);
	p.setFont(font);
	QFontMetrics fm(font);

	// ── X axis ──────────────────────────────────────────────────────
	if (categorical)
	{
		// Tick + label per focal level (one entry per integer position).
		for (size_t i = 0; i < m_eff_level_labels.size(); i++) {
			double x = dataToX((double) i);
			p.setPen(QPen(AXIS_COLOR, 1));
			p.drawLine(QPointF(x, bottom), QPointF(x, bottom + 4));
			const QString &lbl = m_eff_level_labels[i];
			int lw = fm.horizontalAdvance(lbl);
			p.drawText(int(x) - lw / 2, bottom + 4 + fm.ascent() + 2, lbl);
		}
	}
	else
	{
		double xtick = nice_tick(xrange);
		double x0 = std::ceil(xlo / xtick) * xtick;
		p.setPen(QPen(GRID_COLOR, 1, Qt::DotLine));
		for (double v = x0; v <= xhi; v += xtick) {
			double x = dataToX(v);
			if (x > left + 1 && x < right - 1)
				p.drawLine(QPointF(x, top), QPointF(x, bottom));
		}
		p.setPen(QPen(AXIS_COLOR, 1));
		for (double v = x0; v <= xhi; v += xtick) {
			double x = dataToX(v);
			if (x < left - 2 || x > right + 2) continue;
			p.drawLine(QPointF(x, bottom), QPointF(x, bottom + 4));
			QString label = QString::number(v, 'g', 4);
			int lw = fm.horizontalAdvance(label);
			p.drawText(int(x) - lw / 2, bottom + 4 + fm.ascent() + 2, label);
		}
	}
	{
		int tw = fm.horizontalAdvance(m_x_label);
		p.drawText(left + (pw - tw) / 2, bottom + MARGIN_BOTTOM - 4, m_x_label);
	}

	// ── Y axis ──────────────────────────────────────────────────────
	double ytick = nice_tick(yrange);
	double y0 = std::ceil(ylo / ytick) * ytick;
	p.setPen(QPen(GRID_COLOR, 1, Qt::DotLine));
	for (double v = y0; v <= yhi; v += ytick) {
		double y = dataToY(v);
		if (y > top + 1 && y < bottom - 1)
			p.drawLine(QPointF(left, y), QPointF(right, y));
	}
	p.setPen(QPen(AXIS_COLOR, 1));
	for (double v = y0; v <= yhi; v += ytick) {
		double y = dataToY(v);
		if (y < top - 2 || y > bottom + 2) continue;
		p.drawLine(QPointF(left - 4, y), QPointF(left, y));
		QString label = QString::number(v, 'g', 4);
		int lw = fm.horizontalAdvance(label);
		p.drawText(left - 6 - lw, int(y) + fm.ascent() / 2 - 1, label);
	}
	{
		p.save(); p.translate(14, top + ph / 2); p.rotate(-90);
		int tw = fm.horizontalAdvance(m_y_label);
		p.drawText(-tw / 2, 0, m_y_label);
		p.restore();
	}

	// ── Title + caption ─────────────────────────────────────────────
	renderTitle(p, left, pw, top);

	if (!m_eff_caption.isEmpty())
	{
		QFont capFont;
		capFont.setPixelSize(10);
		capFont.setItalic(true);
		p.setFont(capFont);
		p.setPen(QColor(110, 110, 110));
		QFontMetrics cfm(capFont);
		int cw = cfm.horizontalAdvance(m_eff_caption);
		p.drawText(left + (pw - cw) / 2, top + cfm.ascent() + 2, m_eff_caption);
		p.setFont(font);
	}

	// ── Curves ──────────────────────────────────────────────────────
	p.setClipRect(left, top, pw, ph);

	// With overlapping ribbons we lower the alpha so multiple translucent
	// fills don't stack into opacity. Single-curve uses the original alpha.
	int ribbon_alpha = (ncurves > 1) ? 35 : 50;

	if (categorical)
	{
		// For categorical focal, also draw a connecting polyline across
		// focal levels for each curve when there are 2+ curves — this is
		// where the by-factor visual makes interaction patterns visible.
		// Single-curve categorical stays as disconnected points + bars.
		bool draw_connectors = (ncurves > 1);

		const double cap_half = 5.0;
		const double r = 3.5;

		for (int c = 0; c < ncurves; c++)
		{
			auto &cv = m_eff_curves[(size_t) c];
			QColor color = GROUP_PALETTE[c % NUM_PALETTE_COLORS];

			// Connector line: through (x, fit), skipping NaN entries.
			if (draw_connectors)
			{
				QPolygonF line;
				for (size_t i = 0; i < cv.x.size(); i++) {
					if (std::isnan(cv.fit[i])) continue;
					line << QPointF(dataToX(cv.x[i]), dataToY(cv.fit[i]));
				}
				if (line.size() >= 2) {
					p.setPen(QPen(color, 1.5));
					p.setBrush(Qt::NoBrush);
					p.drawPolyline(line);
				}
			}

			// Error bars + markers per point.
			for (size_t i = 0; i < cv.x.size(); i++) {
				if (std::isnan(cv.fit[i])) continue;
				double x  = dataToX(cv.x[i]);
				double yf = dataToY(cv.fit[i]);
				bool have_lo = !std::isnan(cv.ci_lower[i]);
				bool have_hi = !std::isnan(cv.ci_upper[i]);

				if (m_eff_show_ci && have_lo && have_hi) {
					double ylo_px = dataToY(cv.ci_lower[i]);
					double yhi_px = dataToY(cv.ci_upper[i]);
					p.setPen(QPen(color, 1.5));
					p.drawLine(QPointF(x, ylo_px), QPointF(x, yhi_px));
					p.drawLine(QPointF(x - cap_half, ylo_px), QPointF(x + cap_half, ylo_px));
					p.drawLine(QPointF(x - cap_half, yhi_px), QPointF(x + cap_half, yhi_px));
				}
				p.setPen(QPen(color, 1.0));
				p.setBrush(color);
				p.drawEllipse(QPointF(x, yf), r, r);
				p.setBrush(Qt::NoBrush);
			}
		}
	}
	else
	{
		// Numeric focal: ribbons first (back-to-front in palette order),
		// then lines on top so curve colours stay distinguishable even where
		// ribbons overlap. With m_eff_show_ci == false (e.g. busy conditional
		// plots with many random-effect levels) we draw lines only.
		if (m_eff_show_ci)
		{
			for (int c = 0; c < ncurves; c++)
			{
				auto &cv = m_eff_curves[(size_t) c];
				QColor color = GROUP_PALETTE[c % NUM_PALETTE_COLORS];
				QColor ribbon_color = color;
				ribbon_color.setAlpha(ribbon_alpha);

				std::vector<bool> ok(cv.x.size());
				for (size_t i = 0; i < cv.x.size(); i++) {
					ok[i] = !std::isnan(cv.fit[i])
					     && !std::isnan(cv.ci_lower[i])
					     && !std::isnan(cv.ci_upper[i]);
				}

				size_t i = 0;
				while (i < cv.x.size())
				{
					while (i < cv.x.size() && !ok[i]) i++;
					size_t s = i;
					while (i < cv.x.size() && ok[i]) i++;
					size_t e = i;
					if (e - s < 2) continue;

					QPolygonF seg;
					for (size_t k = s; k < e; k++)
						seg << QPointF(dataToX(cv.x[k]), dataToY(cv.ci_upper[k]));
					for (size_t k = e; k-- > s; )
						seg << QPointF(dataToX(cv.x[k]), dataToY(cv.ci_lower[k]));

					p.setPen(Qt::NoPen);
					p.setBrush(ribbon_color);
					p.drawPolygon(seg);
				}
			}
		}

		// Now draw all the lines on top. The "ok" mask uses fit-only when
		// show_ci is off, so a curve with empty CI bounds still draws.
		for (int c = 0; c < ncurves; c++)
		{
			auto &cv = m_eff_curves[(size_t) c];
			QColor color = GROUP_PALETTE[c % NUM_PALETTE_COLORS];

			std::vector<bool> ok(cv.x.size());
			for (size_t i = 0; i < cv.x.size(); i++) {
				if (m_eff_show_ci) {
					ok[i] = !std::isnan(cv.fit[i])
					     && !std::isnan(cv.ci_lower[i])
					     && !std::isnan(cv.ci_upper[i]);
				} else {
					ok[i] = !std::isnan(cv.fit[i]);
				}
			}

			size_t i = 0;
			while (i < cv.x.size())
			{
				while (i < cv.x.size() && !ok[i]) i++;
				size_t s = i;
				while (i < cv.x.size() && ok[i]) i++;
				size_t e = i;
				if (e - s < 2) continue;

				QPolygonF line;
				for (size_t k = s; k < e; k++)
					line << QPointF(dataToX(cv.x[k]), dataToY(cv.fit[k]));
				p.setPen(QPen(color, 2.0));
				p.setBrush(Qt::NoBrush);
				p.drawPolyline(line);
			}
		}
	}

	// ── Legend ──────────────────────────────────────────────────────
	// Suppressed when too many curves to display readably (set by
	// setEffectsPlotData based on curve count). Color-cycling still
	// distinguishes the lines.
	if (any_label && m_eff_show_legend)
	{
		QFont lfont;
		lfont.setPixelSize(10);
		p.setFont(lfont);
		QFontMetrics lfm(lfont);

		int swatch = 14;
		int spacing = 4;
		int line_h = std::max(lfm.height(), swatch) + 2;
		int padding = 6;

		int max_w = 0;
		for (auto &c : m_eff_curves)
			max_w = std::max(max_w, lfm.horizontalAdvance(c.label));

		int legend_w = padding + swatch + spacing + max_w + padding;
		int legend_h = padding + ncurves * line_h + padding;

		// Place at top-right inside the plot area, with a small inset so it
		// doesn't touch the frame.
		int lx = left + pw - legend_w - 8;
		int ly = top + 8;
		// If a caption is present, push the legend down so the two don't
		// collide.
		if (!m_eff_caption.isEmpty()) ly += 14;

		QColor bg(255, 255, 255, 220);
		p.setPen(QPen(GRID_COLOR, 1));
		p.setBrush(bg);
		p.drawRoundedRect(lx, ly, legend_w, legend_h, 3, 3);

		for (int c = 0; c < ncurves; c++)
		{
			QColor color = GROUP_PALETTE[c % NUM_PALETTE_COLORS];
			int ey = ly + padding + c * line_h;
			// Short coloured line as the swatch (matches "line + ribbon"
			// rendering for numeric, "connected markers" for categorical).
			int sx = lx + padding;
			int sy = ey + line_h / 2;
			p.setPen(QPen(color, 2.0));
			p.drawLine(QPointF(sx, sy), QPointF(sx + swatch, sy));

			p.setPen(AXIS_COLOR);
			p.drawText(sx + swatch + spacing,
			           ey + (line_h + lfm.ascent() - lfm.descent()) / 2,
			           m_eff_curves[(size_t) c].label);
		}
		p.setFont(font);
	}
}


// ── Faceted plot grid ────────────────────────────────────────────────
//
// renderFacetGrid lays out a near-square grid of sub-panels, each driven
// by one of the existing inner renderers (Histogram / BoxPlot / Scatter /
// GroupedScatter / BarChart). The approach is swap-and-restore: for each
// cell we move its data into the matching member fields, set
// m_facet_render_active = true (so renderTitle uses the panel label,
// per-panel legends are suppressed) and m_forced_xrange / m_forced_yrange
// (so axes are shared across panels), invoke the inner renderer with the
// sub-rectangle, then move the data back. The grid-level title, the shared
// X/Y axis labels, and the legend are drawn once around the cell loop.

namespace {

// Choose a near-square grid layout for n panels. Prefer more columns than
// rows when n isn't a perfect square (matches ggplot's facet_wrap default).
inline void facet_layout(int n, int &ncols, int &nrows)
{
	if (n <= 0) { ncols = 1; nrows = 1; return; }
	ncols = (int)std::ceil(std::sqrt((double)n));
	nrows = (int)std::ceil((double)n / ncols);
}

} // anonymous namespace

void PlotWidget::renderFacetGrid(QPainter &p, int left, int top, int pw, int ph)
{
	if (m_facet_cells.empty()) return;

	int n = (int)m_facet_cells.size();
	int ncols = 1, nrows = 1;
	facet_layout(n, ncols, nrows);

	// Reserve space for the outer title (top), shared X label (bottom), and
	// shared rotated Y label (left). Panels share these so they aren't drawn
	// per-cell. The legend (if any) lives in the top-right corner of the
	// grid area and is drawn last.
	QFont font;
	font.setPixelSize(11);
	p.setFont(font);
	QFontMetrics fm(font);

	const int OUTER_TITLE_H   = 22;  // global title strip
	const int X_LABEL_H       = 18;  // shared X-axis label strip
	const int Y_LABEL_W       = 18;  // shared Y-axis label strip
	const int FACET_PANEL_GAP = 22;  // vertical gap between facet rows

	// Compute the legend strip width upfront so the panel grid can be shrunk
	// to make room — otherwise the legend overlaps the rightmost panels.
	// The legend is right-anchored in its own column with a small gap.
	auto legend_label_list = [&]() -> const std::vector<QString>* {
		switch (m_facet_inner_mode) {
		case FacetInnerMode::Histogram:      return &m_facet_hist_group_labels;
		case FacetInnerMode::BoxPlot:        return &m_facet_box_secondary_labels;
		case FacetInnerMode::BarChart:       return &m_facet_bar_group_labels;
		case FacetInnerMode::GroupedScatter: return &m_facet_color_labels;
		case FacetInnerMode::Scatter:        return (const std::vector<QString>*)nullptr;
		}
		return nullptr;
	}();
	bool has_legend = legend_label_list && !legend_label_list->empty();
	int LEGEND_STRIP_W = 0;
	int legend_swatch = 10;
	int legend_spacing = 4;
	int legend_padding = 6;
	int legend_outer_margin = 8;  // gap between panels and legend strip
	if (has_legend) {
		int max_lbl = 0;
		for (auto &lbl : *legend_label_list)
			max_lbl = std::max(max_lbl, fm.horizontalAdvance(lbl));
		LEGEND_STRIP_W = legend_outer_margin + legend_padding + legend_swatch
		               + legend_spacing + max_lbl + legend_padding;
	}

	// Outer title.
	if (!m_title.isEmpty())
	{
		QFont tf; tf.setPixelSize(13); tf.setBold(true);
		p.setFont(tf);
		p.setPen(QPen(AXIS_COLOR, 1));
		QFontMetrics tfm(tf);
		int tw = tfm.horizontalAdvance(m_title);
		// Center over the panel grid (which excludes the legend strip), not
		// over the full widget — keeps the title visually attached to its data.
		int title_box_w = pw - LEGEND_STRIP_W;
		p.drawText(left + (title_box_w - tw) / 2, top + tfm.ascent(), m_title);
		p.setFont(font);
	}

	// Shared X-axis label, drawn at the bottom of the grid area.
	p.setPen(QPen(AXIS_COLOR, 1));
	if (!m_x_label.isEmpty())
	{
		int tw = fm.horizontalAdvance(m_x_label);
		int label_box_w = pw - LEGEND_STRIP_W;
		p.drawText(left + (label_box_w - tw) / 2, top + ph - 4, m_x_label);
	}
	if (!m_y_label.isEmpty())
	{
		p.save();
		p.translate(left + 4, top + (ph) / 2);
		p.rotate(-90);
		int tw = fm.horizontalAdvance(m_y_label);
		p.drawText(-tw / 2, fm.ascent(), m_y_label);
		p.restore();
	}

	// Grid area (inside the reserved strips). The right edge is pulled
	// inward by LEGEND_STRIP_W so the legend lives in its own column.
	int grid_left = left + Y_LABEL_W;
	int grid_top  = top + OUTER_TITLE_H;
	int grid_w    = pw - Y_LABEL_W - LEGEND_STRIP_W;
	int grid_h    = ph - OUTER_TITLE_H - X_LABEL_H;
	if (grid_w <= 0 || grid_h <= 0) return;

	// Panel dimensions. Panels are evenly spaced with a small gap between
	// rows; columns share their left/right edges.
	double cell_w = (double)grid_w / ncols;
	double cell_h = (double)(grid_h - (nrows - 1) * (FACET_PANEL_GAP - 6))
	                / nrows;
	if (cell_h < 60) cell_h = 60;

	// Save state we'll mutate during the inner pass.
	QString saved_x_label = m_x_label;
	QString saved_y_label = m_y_label;
	QString saved_title = m_title;
	bool saved_reverse_x = m_reverse_x;
	bool saved_reverse_y = m_reverse_y;

	// In the inner pass, the global X/Y labels and title are suppressed
	// because we already drew them above; the renderer's per-panel title is
	// the facet level.
	m_x_label.clear();
	m_y_label.clear();
	m_title.clear();

	// Range honored by every inner renderer that supports m_forced_*.
	// Histograms use the X range for shared bin edges (set in caller) and
	// the Y range upper bound when m_facet_shared_y_count is true; boxplots
	// use Y range; scatter uses both. NaN bounds are skipped.
	auto is_finite_pair = [](const std::pair<double, double> &r) {
		return std::isfinite(r.first) && std::isfinite(r.second) && r.first < r.second;
	};

	if (is_finite_pair(m_facet_x_range))
		m_forced_xrange = m_facet_x_range;
	else
		m_forced_xrange.reset();

	// For histograms we compute the shared Y upper bound from the pooled
	// max-per-bin-per-group max across all cells, then expose it via
	// m_forced_yrange (lower bound 0).
	if (m_facet_inner_mode == FacetInnerMode::Histogram && m_facet_shared_y_count)
	{
		int gmax = 0;
		for (auto &cell : m_facet_cells)
		{
			for (auto &b : cell.bins)
			{
				if (b.group_counts.empty()) {
					gmax = std::max(gmax, b.count);
				} else {
					for (int c : b.group_counts) gmax = std::max(gmax, c);
				}
			}
		}
		if (gmax < 1) gmax = 1;
		m_forced_yrange = std::make_pair(0.0, gmax * 1.08);
	}
	else if (m_facet_inner_mode == FacetInnerMode::BarChart && m_facet_shared_y_count)
	{
		int gmax = 0;
		for (auto &cell : m_facet_cells)
		{
			if (cell.bar_grouped_counts.empty()) {
				for (int c : cell.bar_counts) gmax = std::max(gmax, c);
			} else {
				for (auto &gc : cell.bar_grouped_counts)
					for (int c : gc) gmax = std::max(gmax, c);
			}
		}
		if (gmax < 1) gmax = 1;
		m_forced_yrange = std::make_pair(0.0, gmax * 1.08);
	}
	else if (is_finite_pair(m_facet_y_range))
	{
		m_forced_yrange = m_facet_y_range;
	}
	else
	{
		m_forced_yrange.reset();
	}

	m_facet_render_active = true;

	// Inner-pass: for each cell, move data into the matching member fields,
	// call the right inner renderer, then move it back. std::swap is move-
	// only on vectors so this is allocation-free.
	for (int i = 0; i < n; i++)
	{
		FacetCell &cell = m_facet_cells[i];
		int row = i / ncols;
		int col = i % ncols;

		int cell_left = grid_left + (int)std::round(col * cell_w);
		int cell_top  = grid_top  + (int)std::round(row * (cell_h + (FACET_PANEL_GAP - 6)));
		int cell_pw   = (int)std::round(cell_w);
		int cell_ph   = (int)std::round(cell_h);
		// Reserve a little margin inside each cell for the panel header
		// (the facet level text drawn by renderTitle).
		const int PANEL_HEADER_H = 18;
		int inner_top = cell_top + PANEL_HEADER_H;
		int inner_ph  = cell_ph - PANEL_HEADER_H;
		if (inner_ph < 40) inner_ph = 40;

		m_facet_panel_title = cell.label;

		switch (m_facet_inner_mode)
		{
		case FacetInnerMode::Histogram:
		{
			std::swap(m_bins, cell.bins);
			std::swap(m_hist_group_labels,
			          /* per-grid labels populated by caller */ m_facet_hist_group_labels);
			renderHistogram(p, cell_left + 2, inner_top, cell_pw - 4, inner_ph);
			std::swap(m_hist_group_labels, m_facet_hist_group_labels);
			std::swap(m_bins, cell.bins);
			break;
		}
		case FacetInnerMode::BoxPlot:
		{
			std::swap(m_boxes, cell.boxes);
			std::swap(m_box_secondary_labels, m_facet_box_secondary_labels);
			renderBoxPlot(p, cell_left + 2, inner_top, cell_pw - 4, inner_ph);
			std::swap(m_box_secondary_labels, m_facet_box_secondary_labels);
			std::swap(m_boxes, cell.boxes);
			break;
		}
		case FacetInnerMode::Scatter:
		{
			std::swap(m_x, cell.x);
			std::swap(m_y, cell.y);
			std::swap(m_point_labels, cell.point_labels);
			std::swap(m_source_rows, cell.source_rows);
			m_reverse_x = saved_reverse_x;
			m_reverse_y = saved_reverse_y;
			m_show_regression = cell.show_regression;
			m_reg_intercept = cell.reg_intercept;
			m_reg_slope = cell.reg_slope;
			m_reg_r2 = cell.reg_r2;
			m_use_labels = !m_point_labels.empty();
			renderScatter(p, cell_left + 2, inner_top, cell_pw - 4, inner_ph, 0, 0, 0, 0);
			std::swap(m_source_rows, cell.source_rows);
			std::swap(m_point_labels, cell.point_labels);
			std::swap(m_y, cell.y);
			std::swap(m_x, cell.x);
			m_show_regression = false;
			m_use_labels = false;
			break;
		}
		case FacetInnerMode::GroupedScatter:
		{
			std::swap(m_group_data, cell.group_data);
			std::swap(m_color_labels, m_facet_color_labels);
			std::swap(m_style_labels, m_facet_style_labels);
			m_reverse_x = saved_reverse_x;
			m_reverse_y = saved_reverse_y;
			renderGroupedScatter(p, cell_left + 2, inner_top, cell_pw - 4, inner_ph);
			std::swap(m_style_labels, m_facet_style_labels);
			std::swap(m_color_labels, m_facet_color_labels);
			std::swap(m_group_data, cell.group_data);
			break;
		}
		case FacetInnerMode::BarChart:
		{
			std::swap(m_bar_labels, cell.bar_labels);
			std::swap(m_bar_counts, cell.bar_counts);
			std::swap(m_bar_grouped_counts, cell.bar_grouped_counts);
			std::swap(m_bar_group_labels, m_facet_bar_group_labels);
			renderBarChart(p, cell_left + 2, inner_top, cell_pw - 4, inner_ph);
			std::swap(m_bar_group_labels, m_facet_bar_group_labels);
			std::swap(m_bar_grouped_counts, cell.bar_grouped_counts);
			std::swap(m_bar_counts, cell.bar_counts);
			std::swap(m_bar_labels, cell.bar_labels);
			break;
		}
		}
	}

	// Restore state.
	m_facet_render_active = false;
	m_facet_panel_title.clear();
	m_forced_xrange.reset();
	m_forced_yrange.reset();
	m_x_label = saved_x_label;
	m_y_label = saved_y_label;
	m_title = saved_title;
	m_reverse_x = saved_reverse_x;
	m_reverse_y = saved_reverse_y;

	// Grid-level legend (drawn once after all panels). Picks the legend
	// labels matching the inner mode; pure single-series modes have no
	// legend.
	auto draw_legend = [&](const std::vector<QString> &labels, bool filled_rect) {
		if (labels.empty()) return;
		int swatch = legend_swatch;
		int spacing = legend_spacing;
		int line_h = std::max(fm.height(), swatch) + 4;
		int padding = legend_padding;
		int nlevels = (int)labels.size();

		// Anchor the legend in the reserved right strip. Top edge aligns
		// with the title strip; vertically centered would also work but
		// top-aligned matches ggplot's default and stays predictable when
		// there are many levels.
		int lx = left + pw - LEGEND_STRIP_W + legend_outer_margin;
		int ly = top + OUTER_TITLE_H + 4;

		for (int i = 0; i < nlevels; i++)
		{
			QColor c0 = GROUP_PALETTE[i % NUM_PALETTE_COLORS];
			int ey = ly + padding + i * line_h;
			if (filled_rect) {
				QColor fill = c0; fill.setAlpha(110);
				p.setPen(QPen(c0, 1));
				p.setBrush(fill);
			} else {
				p.setPen(Qt::NoPen);
				p.setBrush(c0);
			}
			p.drawRect(QRectF(lx + padding, ey + (line_h - swatch) / 2.0, swatch, swatch));
			p.setPen(AXIS_COLOR);
			p.drawText(lx + padding + swatch + spacing,
			           ey + (line_h + fm.ascent() - fm.descent()) / 2,
			           labels[i]);
		}
	};

	switch (m_facet_inner_mode)
	{
	case FacetInnerMode::Histogram:
		draw_legend(m_facet_hist_group_labels, true);
		break;
	case FacetInnerMode::BoxPlot:
		draw_legend(m_facet_box_secondary_labels, false);
		break;
	case FacetInnerMode::BarChart:
		draw_legend(m_facet_bar_group_labels, false);
		break;
	case FacetInnerMode::GroupedScatter:
		draw_legend(m_facet_color_labels, false);
		break;
	case FacetInnerMode::Scatter:
		break;  // no legend for ungrouped scatter
	}
}


void PlotWidget::renderTitle(QPainter &p, int left, int pw, int top)
{
	// In faceted mode the per-panel title replaces the global title; the
	// outer grid driver draws the global title once above the grid.
	const QString &title = m_facet_render_active ? m_facet_panel_title : m_title;
	if (title.isEmpty()) return;

	QFont titleFont;
	titleFont.setPixelSize(m_facet_render_active ? 11 : 13);
	titleFont.setBold(true);
	p.setFont(titleFont);
	p.setPen(QPen(AXIS_COLOR, 1));
	QFontMetrics tfm(titleFont);
	int tw = tfm.horizontalAdvance(title);
	p.drawText(left + (pw - tw) / 2, top - 4, title);

	// Restore base font
	QFont font;
	font.setPixelSize(11);
	p.setFont(font);
}


// ── Cache ───────────────────────────────────────────────────────────

void PlotWidget::rebuildCache()
{
	int w = width();
	int h = height();

	m_cache = QPixmap(QSize(w, h) * devicePixelRatioF());
	m_cache.setDevicePixelRatio(devicePixelRatioF());

	// Hit targets are populated only during on-screen rendering (here),
	// never during exports — savePNG/PDF/SVG render at different scales and
	// would corrupt the screen-space cache.
	m_hit_targets.clear();
	m_collect_hits = true;

	QPainter painter(&m_cache);
	if (m_mode == Mode::Empty) {
		m_cache.fill(BG_COLOR);
	} else {
		renderPlot(painter, w, h);
	}

	m_collect_hits = false;
	m_cache_valid = true;
}

void PlotWidget::paintEvent(QPaintEvent *)
{
	if (!m_cache_valid)
		rebuildCache();

	QPainter p(this);
	if (!m_cache.isNull())
		p.drawPixmap(0, 0, m_cache);
}

void PlotWidget::resizeEvent(QResizeEvent *)
{
	m_cache_valid = false;
}


// ── Click-to-source hit testing ─────────────────────────────────────
//
// The hit cache is rebuilt from scratch on every paintEvent that triggers a
// rebuildCache; mouse handlers gate on m_cache_valid so a hover that arrives
// after a resize and before the next paint doesn't consult stale positions.

static bool find_nearest_hit(const std::vector<PlotWidget::HitTarget> &targets,
                             const QPointF &cursor, double tolerance,
                             intptr_t &out_row)
{
	double best_d2 = tolerance * tolerance;
	bool found = false;
	for (auto &t : targets) {
		double dx = t.pos.x() - cursor.x();
		double dy = t.pos.y() - cursor.y();
		double d2 = dx * dx + dy * dy;
		if (d2 <= best_d2) {
			best_d2 = d2;
			out_row = t.source_row;
			found = true;
		}
	}
	return found;
}

void PlotWidget::mousePressEvent(QMouseEvent *event)
{
	if (event->button() != Qt::LeftButton || !m_cache_valid || m_hit_targets.empty()) {
		QWidget::mousePressEvent(event);
		return;
	}

	intptr_t row = INVALID_ROW;
	if (find_nearest_hit(m_hit_targets, event->position(), HIT_TOLERANCE_PX, row)) {
		event->accept();
		emit pointClicked(row);
		return;
	}
	QWidget::mousePressEvent(event);
}

void PlotWidget::mouseMoveEvent(QMouseEvent *event)
{
	bool over = false;
	if (m_cache_valid && !m_hit_targets.empty()) {
		intptr_t row = INVALID_ROW;
		over = find_nearest_hit(m_hit_targets, event->position(),
		                        HIT_TOLERANCE_PX, row);
	}
	if (over != m_hover_on_target) {
		m_hover_on_target = over;
		if (over) setCursor(Qt::PointingHandCursor);
		else      unsetCursor();
	}
	QWidget::mouseMoveEvent(event);
}

void PlotWidget::leaveEvent(QEvent *event)
{
	if (m_hover_on_target) {
		m_hover_on_target = false;
		unsetCursor();
	}
	QWidget::leaveEvent(event);
}


// ── Export ───────────────────────────────────────────────────────────

void PlotWidget::savePNG(const QString &path)
{
	const int scale = 2;
	int w = width();
	int h = height();

	QPixmap pixmap(QSize(w, h) * scale);
	pixmap.setDevicePixelRatio(scale);
	pixmap.fill(BG_COLOR);
	QPainter painter(&pixmap);
	renderPlot(painter, w, h);
	painter.end();
	pixmap.save(path, "PNG");
}

void PlotWidget::savePDF(const QString &path)
{
	int w = width();
	int h = height();

	QPrinter printer(QPrinter::HighResolution);
	printer.setOutputFormat(QPrinter::PdfFormat);
	printer.setOutputFileName(path);

	QPageSize pageSize(QSizeF(w, h), QPageSize::Point);
	printer.setPageSize(pageSize);
	printer.setPageMargins(QMarginsF(0, 0, 0, 0));

	QPainter painter(&printer);
	double sx = printer.width() / double(w);
	double sy = printer.height() / double(h);
	double s = std::min(sx, sy);
	painter.scale(s, s);
	renderPlot(painter, w, h);
	painter.end();
}

void PlotWidget::saveSVG(const QString &path)
{
	int w = width();
	int h = height();

	QSvgGenerator svg;
	svg.setFileName(path);
	svg.setSize(QSize(w, h));
	svg.setViewBox(QRect(0, 0, w, h));
	svg.setTitle(m_title);

	QPainter painter(&svg);
	renderPlot(painter, w, h);
	painter.end();
}

} // namespace phonometrica
