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
#include <QPainter>
#include <QPainterPath>
#include <QPrinter>
#include <QSvgGenerator>
#include <phon/gui/plot_widget.hpp>

namespace phonometrica {

// ── Colors and layout (consistent with SpectrumView) ─────────────────

static const QColor POINT_COLOR(0, 80, 180, 160);      // semi-transparent blue
static const QColor REFLINE_COLOR(200, 60, 60);         // muted red
static const QColor GRID_COLOR(220, 220, 220);          // light grey
static const QColor AXIS_COLOR(60, 60, 60);
static const QColor BG_COLOR(255, 255, 255);

static constexpr int MARGIN_LEFT   = 65;
static constexpr int MARGIN_RIGHT  = 20;
static constexpr int MARGIN_TOP    = 30;
static constexpr int MARGIN_BOTTOM = 45;

static constexpr double POINT_RADIUS = 2.5;

// ── Axis tick helpers ────────────────────────────────────────────────

// Choose a "nice" tick spacing for the given data range.
static double nice_tick(double range)
{
	if (range <= 0) return 1;
	double rough = range / 5.0; // aim for ~5 ticks
	double mag = std::pow(10.0, std::floor(std::log10(rough)));
	double frac = rough / mag;

	if (frac <= 1.5) return mag;
	if (frac <= 3.5) return 2 * mag;
	if (frac <= 7.5) return 5 * mag;
	return 10 * mag;
}

// Compute axis bounds with a small margin.
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


// ── Constructor ──────────────────────────────────────────────────────

PlotWidget::PlotWidget(QWidget *parent) : QWidget(parent)
{
	setMinimumSize(200, 150);
	setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
}

void PlotWidget::setData(std::vector<double> x, std::vector<double> y,
                          const QString &x_label, const QString &y_label,
                          const QString &title, RefLine ref)
{
	m_x = std::move(x);
	m_y = std::move(y);
	m_x_label = x_label;
	m_y_label = y_label;
	m_title = title;
	m_ref_line = ref;
	m_cache_valid = false;
	update();
}

void PlotWidget::clear()
{
	m_x.clear();
	m_y.clear();
	m_title.clear();
	m_cache_valid = false;
	update();
}


// ── Rendering ────────────────────────────────────────────────────────

void PlotWidget::renderPlot(QPainter &p, int w, int h)
{
	int pw = w - MARGIN_LEFT - MARGIN_RIGHT;
	int ph = h - MARGIN_TOP - MARGIN_BOTTOM;
	if (pw <= 0 || ph <= 0) return;

	int left   = MARGIN_LEFT;
	int right  = MARGIN_LEFT + pw;
	int top    = MARGIN_TOP;
	int bottom = MARGIN_TOP + ph;

	// ── Data range ──────────────────────────────
	double xlo, xhi, ylo, yhi;
	axis_range(m_x, xlo, xhi);
	axis_range(m_y, ylo, yhi);

	// For Q-Q diagonal reference, ensure symmetric range around the data.
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

	auto dataToX = [&](double v) -> double {
		return left + ((v - xlo) / xrange) * pw;
	};
	auto dataToY = [&](double v) -> double {
		return bottom - ((v - ylo) / yrange) * ph;
	};

	p.setRenderHint(QPainter::Antialiasing);

	// ── Background ──────────────────────────────
	p.fillRect(0, 0, w, h, BG_COLOR);

	// ── Frame ───────────────────────────────────
	p.setPen(QPen(AXIS_COLOR, 1));
	p.drawRect(left, top, pw, ph);

	// ── Axes ────────────────────────────────────
	QFont font;
	font.setPixelSize(11);
	p.setFont(font);
	QFontMetrics fm(font);

	// X-axis
	double xtick = nice_tick(xrange);
	double x0 = std::ceil(xlo / xtick) * xtick;

	// X grid
	p.setPen(QPen(GRID_COLOR, 1, Qt::DotLine));
	for (double v = x0; v <= xhi; v += xtick)
	{
		double x = dataToX(v);
		if (x > left + 1 && x < right - 1)
			p.drawLine(QPointF(x, top), QPointF(x, bottom));
	}

	// X ticks and labels
	p.setPen(QPen(AXIS_COLOR, 1));
	for (double v = x0; v <= xhi; v += xtick)
	{
		double x = dataToX(v);
		if (x < left - 2 || x > right + 2) continue;
		p.drawLine(QPointF(x, bottom), QPointF(x, bottom + 4));

		QString label = QString::number(v, 'g', 4);
		int lw = fm.horizontalAdvance(label);
		p.drawText(int(x) - lw / 2, bottom + 4 + fm.ascent() + 2, label);
	}

	// X-axis title
	{
		int tw = fm.horizontalAdvance(m_x_label);
		p.drawText(left + (pw - tw) / 2, bottom + MARGIN_BOTTOM - 4, m_x_label);
	}

	// Y-axis
	double ytick = nice_tick(yrange);
	double y0 = std::ceil(ylo / ytick) * ytick;

	// Y grid
	p.setPen(QPen(GRID_COLOR, 1, Qt::DotLine));
	for (double v = y0; v <= yhi; v += ytick)
	{
		double y = dataToY(v);
		if (y > top + 1 && y < bottom - 1)
			p.drawLine(QPointF(left, y), QPointF(right, y));
	}

	// Y ticks and labels
	p.setPen(QPen(AXIS_COLOR, 1));
	for (double v = y0; v <= yhi; v += ytick)
	{
		double y = dataToY(v);
		if (y < top - 2 || y > bottom + 2) continue;
		p.drawLine(QPointF(left - 4, y), QPointF(left, y));

		QString label = QString::number(v, 'g', 4);
		int lw = fm.horizontalAdvance(label);
		p.drawText(left - 6 - lw, int(y) + fm.ascent() / 2 - 1, label);
	}

	// Y-axis title (rotated)
	{
		p.save();
		p.translate(14, top + ph / 2);
		p.rotate(-90);
		int tw = fm.horizontalAdvance(m_y_label);
		p.drawText(-tw / 2, 0, m_y_label);
		p.restore();
	}

	// ── Title ───────────────────────────────────
	if (!m_title.isEmpty())
	{
		QFont titleFont = font;
		titleFont.setPixelSize(13);
		titleFont.setBold(true);
		p.setFont(titleFont);
		QFontMetrics tfm(titleFont);
		int tw = tfm.horizontalAdvance(m_title);
		p.drawText(left + (pw - tw) / 2, top - 8, m_title);
		p.setFont(font);
	}

	// ── Reference line ──────────────────────────
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

	// ── Data points ─────────────────────────────
	p.setPen(Qt::NoPen);
	p.setBrush(POINT_COLOR);

	size_t n = std::min(m_x.size(), m_y.size());
	for (size_t i = 0; i < n; i++)
	{
		double x = dataToX(m_x[i]);
		double y = dataToY(m_y[i]);
		p.drawEllipse(QPointF(x, y), POINT_RADIUS, POINT_RADIUS);
	}

	p.setClipping(false);
}


// ── Cache ────────────────────────────────────────────────────────────

void PlotWidget::rebuildCache()
{
	int w = width();
	int h = height();

	m_cache = QPixmap(QSize(w, h) * devicePixelRatioF());
	m_cache.setDevicePixelRatio(devicePixelRatioF());

	QPainter painter(&m_cache);
	if (m_x.empty()) {
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


// ── Export ────────────────────────────────────────────────────────────

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
