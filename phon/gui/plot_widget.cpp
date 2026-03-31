/***********************************************************************************************************************
 *                                                                                                                     *
 * Copyright (C) 2019-2026 Julien Eychenne                                                                             *
 *                                                                                                                     *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0. If a copy of the MPL was not   *
 * distributed with this file, You can obtain one at https://mozilla.org/MPL/2.0/.                                     *
 *                                                                                                                     *
 * Created: 30/03/2026                                                                                                 *
 *                                                                                                                     *
 * Purpose: see header.                                                                                                *
 *                                                                                                                     *
 ***********************************************************************************************************************/

#include <cmath>
#include <algorithm>
#include <map>
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

static constexpr int MARGIN_LEFT   = 65;
static constexpr int MARGIN_RIGHT  = 20;
static constexpr int MARGIN_TOP    = 30;
static constexpr int MARGIN_BOTTOM = 45;

static constexpr double POINT_RADIUS = 2.5;


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
	setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
}


// ── Public setters ──────────────────────────────────────────────────

void PlotWidget::setData(std::vector<double> x, std::vector<double> y,
                          const QString &x_label, const QString &y_label,
                          const QString &title, RefLine ref)
{
	m_mode = Mode::Scatter;
	m_x = std::move(x);
	m_y = std::move(y);
	m_x_label = x_label;
	m_y_label = y_label;
	m_title = title;
	m_ref_line = ref;
	m_boxes.clear();
	m_bins.clear();
	m_cache_valid = false;
	update();
}

void PlotWidget::setBoxPlotData(std::vector<QString> groups, std::vector<double> values,
                                 const QString &x_label, const QString &y_label,
                                 const QString &title)
{
	m_mode = Mode::BoxPlot;
	m_boxes = computeBoxStats(groups, values);
	m_x_label = x_label;
	m_y_label = y_label;
	m_title = title;
	m_x.clear();
	m_y.clear();
	m_bins.clear();
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
	m_boxes.clear();
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
	m_x_label = x_label;
	m_y_label = y_label;
	m_title = title;
	m_x.clear();
	m_y.clear();
	m_boxes.clear();
	m_bins.clear();
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

void PlotWidget::clear()
{
	m_mode = Mode::Empty;
	m_x.clear();
	m_y.clear();
	m_boxes.clear();
	m_bins.clear();
	m_bar_labels.clear();
	m_bar_counts.clear();
	m_title.clear();
	m_show_regression = false;
	m_show_density = false;
	m_density_x.clear();
	m_density_y.clear();
	m_cache_valid = false;
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
	const std::vector<QString> &groups, const std::vector<double> &values)
{
	// Collect values per group, preserving first-seen order.
	std::vector<QString> order;
	std::map<QString, std::vector<double>> grouped;

	for (size_t i = 0; i < groups.size() && i < values.size(); i++)
	{
		auto &g = groups[i];
		if (grouped.find(g) == grouped.end()) {
			order.push_back(g);
		}
		grouped[g].push_back(values[i]);
	}

	std::vector<BoxStats> result;
	for (auto &label : order)
	{
		auto &vals = grouped[label];
		if (vals.empty()) continue;

		std::sort(vals.begin(), vals.end());

		BoxStats bs;
		bs.label = label;
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

		// Outliers
		for (double v : vals)
		{
			if (v < fence_lo || v > fence_hi) {
				bs.outliers.push_back(v);
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
	case Mode::Scatter:   renderScatter(p, left, top, pw, ph, 0, 0, 0, 0); break;
	case Mode::BoxPlot:   renderBoxPlot(p, left, top, pw, ph); break;
	case Mode::Histogram: renderHistogram(p, left, top, pw, ph); break;
	case Mode::BarChart:  renderBarChart(p, left, top, pw, ph); break;
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

	auto dataToX = [&](double v) -> double { return left + ((v - xlo) / xrange) * pw; };
	auto dataToY = [&](double v) -> double { return bottom - ((v - ylo) / yrange) * ph; };

	// Frame
	p.setPen(QPen(AXIS_COLOR, 1));
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

	// Reference line
	p.setClipRect(left, top, pw, ph);
	if (m_ref_line == RefLine::HorizontalAtZero)
	{
		double y = dataToY(0);
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
	p.setPen(Qt::NoPen);
	p.setBrush(POINT_COLOR);
	size_t n = std::min(m_x.size(), m_y.size());
	for (size_t i = 0; i < n; i++) {
		double x = dataToX(m_x[i]);
		double y = dataToY(m_y[i]);
		p.drawEllipse(QPointF(x, y), POINT_RADIUS, POINT_RADIUS);
	}
	p.setClipping(false);
}


void PlotWidget::renderBoxPlot(QPainter &p, int left, int top, int pw, int ph)
{
	if (m_boxes.empty()) return;

	int bottom = top + ph;
	int ngroups = (int)m_boxes.size();

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
	double yrange = yhi - ylo;

	auto dataToY = [&](double v) -> double { return bottom - ((v - ylo) / yrange) * ph; };

	// Frame
	p.setPen(QPen(AXIS_COLOR, 1));
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

	// Draw boxes
	p.setClipRect(left, top, pw, ph);

	double group_width = (double)pw / ngroups;
	double box_width = group_width * 0.55;

	for (int g = 0; g < ngroups; g++)
	{
		auto &b = m_boxes[g];
		double cx = left + group_width * (g + 0.5);

		double ymed = dataToY(b.median);
		double yq1  = dataToY(b.q1);
		double yq3  = dataToY(b.q3);
		double ywlo = dataToY(b.whisker_lo);
		double ywhi = dataToY(b.whisker_hi);

		// Box (Q1 to Q3)
		p.setPen(QPen(BOX_BORDER, 1.5));
		p.setBrush(BOX_FILL);
		p.drawRect(QRectF(cx - box_width / 2, yq3, box_width, yq1 - yq3));

		// Median
		p.setPen(QPen(BOX_BORDER, 2));
		p.drawLine(QPointF(cx - box_width / 2, ymed), QPointF(cx + box_width / 2, ymed));

		// Whiskers
		p.setPen(QPen(AXIS_COLOR, 1));
		p.drawLine(QPointF(cx, yq3), QPointF(cx, ywhi));    // top whisker
		p.drawLine(QPointF(cx, yq1), QPointF(cx, ywlo));    // bottom whisker
		double cap = box_width * 0.3;
		p.drawLine(QPointF(cx - cap, ywhi), QPointF(cx + cap, ywhi));
		p.drawLine(QPointF(cx - cap, ywlo), QPointF(cx + cap, ywlo));

		// Outliers
		p.setPen(Qt::NoPen);
		p.setBrush(POINT_COLOR);
		for (double o : b.outliers) {
			p.drawEllipse(QPointF(cx, dataToY(o)), POINT_RADIUS, POINT_RADIUS);
		}

		// Group label (below x-axis)
		p.setClipping(false);
		p.setPen(QPen(AXIS_COLOR, 1));
		int lw = fm.horizontalAdvance(b.label);
		p.drawText(int(cx) - lw / 2, bottom + 4 + fm.ascent() + 2, b.label);
		p.setClipRect(left, top, pw, ph);
	}

	p.setClipping(false);
}


void PlotWidget::renderHistogram(QPainter &p, int left, int top, int pw, int ph)
{
	if (m_bins.empty()) return;

	int bottom = top + ph;
	int nbins = (int)m_bins.size();

	// X range from bins
	double xlo = m_bins.front().lo;
	double xhi = m_bins.back().hi;
	double xrange = xhi - xlo;
	if (xrange <= 0) xrange = 1;

	// Y range: 0 to max count
	int max_count = 0;
	for (auto &b : m_bins)
		max_count = std::max(max_count, b.count);

	double ymax = (double)max_count;

	// If a density curve is present, ensure the Y range covers its peak.
	if (m_show_density) {
		for (double v : m_density_y)
			ymax = std::max(ymax, v);
	}

	double yhi = ymax * 1.08;
	if (yhi < 1) yhi = 1;

	auto dataToX = [&](double v) -> double { return left + ((v - xlo) / xrange) * pw; };
	auto dataToY = [&](double v) -> double { return bottom - (v / yhi) * ph; };

	// Frame
	p.setPen(QPen(AXIS_COLOR, 1));
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
}


void PlotWidget::renderBarChart(QPainter &p, int left, int top, int pw, int ph)
{
	if (m_bar_labels.empty()) return;

	int bottom = top + ph;
	int nbars = (int)m_bar_labels.size();

	// Y range: 0 to max count
	int max_count = 0;
	for (int c : m_bar_counts)
		max_count = std::max(max_count, c);

	double yhi = max_count * 1.08;
	if (yhi < 1) yhi = 1;

	auto dataToY = [&](double v) -> double { return bottom - (v / yhi) * ph; };

	// Frame
	p.setPen(QPen(AXIS_COLOR, 1));
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
	p.setClipping(false);

	// Category labels below x-axis
	p.setPen(QPen(AXIS_COLOR, 1));
	for (int i = 0; i < nbars; i++)
	{
		double cx = left + bar_area * (i + 0.5);
		int lw = fm.horizontalAdvance(m_bar_labels[i]);
		p.drawText(int(cx) - lw / 2, bottom + 4 + fm.ascent() + 2, m_bar_labels[i]);
	}
}


void PlotWidget::renderTitle(QPainter &p, int left, int pw, int top)
{
	if (m_title.isEmpty()) return;

	QFont titleFont;
	titleFont.setPixelSize(13);
	titleFont.setBold(true);
	p.setFont(titleFont);
	p.setPen(QPen(AXIS_COLOR, 1));
	QFontMetrics tfm(titleFont);
	int tw = tfm.horizontalAdvance(m_title);
	p.drawText(left + (pw - tw) / 2, top - 8, m_title);

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

	QPainter painter(&m_cache);
	if (m_mode == Mode::Empty) {
		m_cache.fill(BG_COLOR);
	} else {
		renderPlot(painter, w, h);
	}

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
