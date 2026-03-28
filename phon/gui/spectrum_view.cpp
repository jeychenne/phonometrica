/***********************************************************************************************************************
 *                                                                                                                     *
 * Copyright (C) 2019-2026 Julien Eychenne                                                                             *
 *                                                                                                                     *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0. If a copy of the MPL was not   *
 * distributed with this file, You can obtain one at https://mozilla.org/MPL/2.0/.                                     *
 *                                                                                                                     *
 * Created: 28/03/2026                                                                                                 *
 *                                                                                                                     *
 * Purpose: see header.                                                                                                *
 *                                                                                                                     *
 ***********************************************************************************************************************/

#include <cmath>
#include <algorithm>
#include <QPainter>
#include <QPainterPath>
#include <QMouseEvent>
#include <QVBoxLayout>
#include <QFontMetrics>
#include <phon/gui/spectrum_view.hpp>

namespace phonometrica {

// ── Colours ────────────────────────────────────────
static const QColor CURVE_COLOR(0, 80, 180);          // dark blue
static const QColor GRID_COLOR(220, 220, 220);        // light grey
static const QColor CROSSHAIR_COLOR(180, 0, 0, 160);  // semi-transparent red
static const QColor AXIS_COLOR(60, 60, 60);
static const QColor BG_COLOR(255, 255, 255);

// ── Constants ──────────────────────────────────────
static constexpr int MARGIN_LEFT   = 65;
static constexpr int MARGIN_RIGHT  = 20;
static constexpr int MARGIN_TOP    = 20;
static constexpr int MARGIN_BOTTOM = 40;


SpectrumView::SpectrumView(const Handle<Spectrum> &spectrum, QWidget *parent) :
	QDialog(parent), m_spectrum(spectrum)
{
	setWindowTitle(tr("Spectral slice (%1 – %2 s)")
		.arg(m_spectrum->start_time(), 0, 'f', 4)
		.arg(m_spectrum->end_time(), 0, 'f', 4));
	setMinimumSize(400, 250);
	resize(sizeHint());

	// Enable mouse tracking for crosshair readout.
	setMouseTracking(true);

	// Status bar at the bottom.
	auto *layout = new QVBoxLayout(this);
	layout->setContentsMargins(0, 0, 0, 0);
	layout->setSpacing(0);

	// A spacer widget pushes the status label to the bottom.
	layout->addStretch(1);

	m_status = new QLabel(this);
	m_status->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
	m_status->setIndent(8);
	m_status->setFixedHeight(22);
	m_status->setStyleSheet(QStringLiteral("QLabel { background: #f0f0f0; color: #333; font-size: 11px; }"));
	layout->addWidget(m_status);

	// Show bandwidth information initially.
	m_status->setText(tr("Bandwidth \u2248 %1 Hz  |  FFT size = %2  |  %3 bins")
		.arg(m_spectrum->bandwidth(), 0, 'f', 1)
		.arg(m_spectrum->fft_size())
		.arg(m_spectrum->bin_count()));
}


// ─────────────────────────────────────────────────
//  Margins and plot area
// ─────────────────────────────────────────────────

int SpectrumView::marginLeft()   const { return MARGIN_LEFT; }
int SpectrumView::marginRight()  const { return MARGIN_RIGHT; }
int SpectrumView::marginTop()    const { return MARGIN_TOP; }
int SpectrumView::marginBottom() const { return MARGIN_BOTTOM; }

int SpectrumView::plotWidth()  const { return width() - marginLeft() - marginRight(); }
int SpectrumView::plotHeight() const { return height() - marginTop() - marginBottom() - m_status->height(); }


// ─────────────────────────────────────────────────
//  Coordinate mapping
// ─────────────────────────────────────────────────

double SpectrumView::frequencyToX(double hz) const
{
	double maxF = m_spectrum->max_frequency();
	if (maxF <= 0) return marginLeft();
	return marginLeft() + (hz / maxF) * plotWidth();
}

double SpectrumView::xToFrequency(double x) const
{
	double maxF = m_spectrum->max_frequency();
	return ((x - marginLeft()) / plotWidth()) * maxF;
}

double SpectrumView::dBToY(double dB) const
{
	double peak = m_spectrum->peak_dB();
	double floor = m_spectrum->floor_dB();
	double range = peak - floor;
	if (range <= 0) return marginTop() + plotHeight() / 2.0;
	return marginTop() + (1.0 - (dB - floor) / range) * plotHeight();
}

double SpectrumView::yToDB(double y) const
{
	double peak = m_spectrum->peak_dB();
	double floor = m_spectrum->floor_dB();
	double range = peak - floor;
	return peak - ((y - marginTop()) / plotHeight()) * range;
}


// ─────────────────────────────────────────────────
//  Axis drawing
// ─────────────────────────────────────────────────

void SpectrumView::drawAxes(QPainter &p)
{
	int left   = marginLeft();
	int right  = left + plotWidth();
	int top    = marginTop();
	int bottom = top + plotHeight();

	p.setPen(QPen(AXIS_COLOR, 1));

	// Frame around the plot area.
	p.drawRect(left, top, plotWidth(), plotHeight());

	QFont font = p.font();
	font.setPointSizeF(font.pointSizeF() * 0.85);
	p.setFont(font);
	QFontMetrics fm(font);

	// ── X-axis (frequency) ─────────────────────
	double maxF = m_spectrum->max_frequency();
	double tickF = 1000;
	if (maxF <= 1000)       tickF = 100;
	else if (maxF <= 5000)  tickF = 500;
	else if (maxF <= 12000) tickF = 1000;
	else                    tickF = 2000;

	p.setPen(QPen(GRID_COLOR, 1, Qt::DotLine));
	for (double f = tickF; f < maxF; f += tickF)
	{
		double x = frequencyToX(f);
		p.drawLine(QPointF(x, top), QPointF(x, bottom));
	}

	p.setPen(QPen(AXIS_COLOR, 1));
	for (double f = 0; f <= maxF; f += tickF)
	{
		double x = frequencyToX(f);
		p.drawLine(QPointF(x, bottom), QPointF(x, bottom + 4));

		QString label;
		if (f >= 1000 && std::fmod(f, 1000) == 0)
			label = QString("%1k").arg(f / 1000, 0, 'f', 0);
		else
			label = QString::number(int(f));

		int lw = fm.horizontalAdvance(label);
		p.drawText(int(x) - lw / 2, bottom + 4 + fm.ascent() + 2, label);
	}

	// X-axis title.
	{
		QString title = tr("Frequency (Hz)");
		int tw = fm.horizontalAdvance(title);
		p.drawText(left + (plotWidth() - tw) / 2, bottom + MARGIN_BOTTOM - 4, title);
	}

	// ── Y-axis (power dB) ──────────────────────
	double peak  = m_spectrum->peak_dB();
	double floor = m_spectrum->floor_dB();
	double range = peak - floor;

	double tickDB = 10;
	if (range <= 20)      tickDB = 5;
	else if (range <= 50) tickDB = 10;
	else                  tickDB = 20;

	double dB_start = std::floor(floor / tickDB) * tickDB;
	double dB_end   = std::ceil(peak / tickDB) * tickDB;

	p.setPen(QPen(GRID_COLOR, 1, Qt::DotLine));
	for (double dB = dB_start; dB <= dB_end; dB += tickDB)
	{
		if (dB <= floor || dB >= peak) continue;
		double y = dBToY(dB);
		p.drawLine(QPointF(left, y), QPointF(right, y));
	}

	p.setPen(QPen(AXIS_COLOR, 1));
	for (double dB = dB_start; dB <= dB_end; dB += tickDB)
	{
		double y = dBToY(dB);
		if (y < top - 2 || y > bottom + 2) continue;
		p.drawLine(QPointF(left - 4, y), QPointF(left, y));

		QString label = QString::number(int(dB));
		int lw = fm.horizontalAdvance(label);
		p.drawText(left - 6 - lw, int(y) + fm.ascent() / 2 - 1, label);
	}

	// Y-axis title (rotated).
	{
		QString title = tr("Power (dB)");
		p.save();
		p.translate(14, top + plotHeight() / 2);
		p.rotate(-90);
		int tw = fm.horizontalAdvance(title);
		p.drawText(-tw / 2, 0, title);
		p.restore();
	}
}


// ─────────────────────────────────────────────────
//  Cache building
// ─────────────────────────────────────────────────

void SpectrumView::rebuildCache()
{
	int w = width();
	int h = height() - m_status->height();

	m_cache = QPixmap(QSize(w, h) * devicePixelRatioF());
	m_cache.setDevicePixelRatio(devicePixelRatioF());
	m_cache.fill(BG_COLOR);

	if (plotWidth() <= 0 || plotHeight() <= 0) {
		m_cache_valid = true;
		return;
	}

	QPainter painter(&m_cache);
	painter.setRenderHint(QPainter::Antialiasing);

	// Axes first (behind the curve).
	drawAxes(painter);

	// ── Draw the spectrum curve ────────────────
	const auto &power = m_spectrum->power_dB();
	if (power.empty()) {
		m_cache_valid = true;
		return;
	}

	painter.setPen(QPen(CURVE_COLOR, 1.5));
	painter.setClipRect(marginLeft(), marginTop(), plotWidth(), plotHeight());

	QPainterPath path;
	bool first = true;

	for (intptr_t k = 0; k < m_spectrum->bin_count(); k++)
	{
		double freq = m_spectrum->bin_frequency(k);
		if (freq > m_spectrum->max_frequency()) break;

		double x = frequencyToX(freq);
		double y = dBToY(power[k]);

		if (first) {
			path.moveTo(x, y);
			first = false;
		}
		else {
			path.lineTo(x, y);
		}
	}

	painter.drawPath(path);
	painter.setClipping(false);

	m_cache_valid = true;
}


// ─────────────────────────────────────────────────
//  Painting
// ─────────────────────────────────────────────────

void SpectrumView::paintEvent(QPaintEvent *)
{
	if (!m_cache_valid)
		rebuildCache();

	QPainter p(this);

	// Draw the cached plot.
	if (!m_cache.isNull())
		p.drawPixmap(0, 0, m_cache);

	// ── Crosshair at mouse position ────────────
	if (m_mouse_x >= marginLeft() && m_mouse_x <= marginLeft() + plotWidth() &&
	    m_mouse_y >= marginTop()  && m_mouse_y <= marginTop() + plotHeight())
	{
		p.setPen(QPen(CROSSHAIR_COLOR, 1, Qt::DashLine));
		int left   = marginLeft();
		int right  = left + plotWidth();
		int top    = marginTop();
		int bottom = top + plotHeight();

		// Vertical line.
		p.drawLine(QPointF(m_mouse_x, top), QPointF(m_mouse_x, bottom));
		// Horizontal line.
		p.drawLine(QPointF(left, m_mouse_y), QPointF(right, m_mouse_y));
	}
}

void SpectrumView::resizeEvent(QResizeEvent *)
{
	m_cache_valid = false;
}


// ─────────────────────────────────────────────────
//  Mouse interaction
// ─────────────────────────────────────────────────

void SpectrumView::mouseMoveEvent(QMouseEvent *event)
{
	m_mouse_x = event->position().x();
	m_mouse_y = event->position().y();

	// Update the status bar readout.
	if (m_mouse_x >= marginLeft() && m_mouse_x <= marginLeft() + plotWidth() &&
	    m_mouse_y >= marginTop()  && m_mouse_y <= marginTop() + plotHeight())
	{
		double freq = xToFrequency(m_mouse_x);
		double dB   = yToDB(m_mouse_y);
		m_status->setText(tr("Frequency \u2248 %1 Hz  |  Power \u2248 %2 dB")
			.arg(freq, 0, 'f', 1)
			.arg(dB, 0, 'f', 1));
	}
	else
	{
		m_status->setText(tr("Bandwidth \u2248 %1 Hz  |  FFT size = %2  |  %3 bins")
			.arg(m_spectrum->bandwidth(), 0, 'f', 1)
			.arg(m_spectrum->fft_size())
			.arg(m_spectrum->bin_count()));
	}

	update();
}

void SpectrumView::leaveEvent(QEvent *)
{
	m_mouse_x = -1;
	m_mouse_y = -1;

	m_status->setText(tr("Bandwidth \u2248 %1 Hz  |  FFT size = %2  |  %3 bins")
		.arg(m_spectrum->bandwidth(), 0, 'f', 1)
		.arg(m_spectrum->fft_size())
		.arg(m_spectrum->bin_count()));

	update();
}

} // namespace phonometrica
