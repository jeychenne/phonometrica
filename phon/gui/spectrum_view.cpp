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
#include <QMenu>
#include <QToolButton>
#include <QFileDialog>
#include <QMessageBox>
#include <QSvgGenerator>
#include <QPrinter>
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

	auto *layout = new QVBoxLayout(this);
	layout->setContentsMargins(0, 0, 0, 0);
	layout->setSpacing(0);

	// ── Toolbar ────────────────────────────────
	m_toolbar = new QToolBar(this);
	m_toolbar->setIconSize(QSize(20, 20));
	m_toolbar->setMovable(false);

	auto *save_menu = new QMenu(this);
	save_menu->addAction(tr("Save as PNG..."), this, &SpectrumView::onSavePNG);
	save_menu->addAction(tr("Save as PDF..."), this, &SpectrumView::onSavePDF);
	save_menu->addAction(tr("Save as SVG..."), this, &SpectrumView::onSaveSVG);

	auto *save_action = new QAction(QIcon(":/icons/save.svg"), tr("Save as..."), this);
	save_action->setMenu(save_menu);
	m_toolbar->addAction(save_action);
	if (auto *btn = qobject_cast<QToolButton *>(m_toolbar->widgetForAction(save_action)))
		btn->setPopupMode(QToolButton::InstantPopup);

	layout->addWidget(m_toolbar);

	// A spacer pushes the status label to the bottom; the plot area in between
	// is painted directly in paintEvent().
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
//  Margins and plot area (interactive view — includes toolbar offset)
// ─────────────────────────────────────────────────

int SpectrumView::marginLeft()   const { return MARGIN_LEFT; }
int SpectrumView::marginRight()  const { return MARGIN_RIGHT; }
int SpectrumView::marginTop()    const { return MARGIN_TOP + m_toolbar->height(); }
int SpectrumView::marginBottom() const { return MARGIN_BOTTOM; }

int SpectrumView::plotWidth()  const { return width() - marginLeft() - marginRight(); }
int SpectrumView::plotHeight() const { return height() - marginTop() - marginBottom() - m_status->height(); }


// ─────────────────────────────────────────────────
//  Coordinate mapping (interactive — uses widget dimensions)
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
//  Self-contained plot renderer (for cache and export)
//
//  Draws the full plot (background, axes, curve) into the given painter
//  at the specified logical dimensions. The crosshair is NOT included.
// ─────────────────────────────────────────────────

void SpectrumView::renderPlot(QPainter &p, int w, int h)
{
	// ── Local geometry ───────────────────────────
	int pw = w - MARGIN_LEFT - MARGIN_RIGHT;
	int ph = h - MARGIN_TOP - MARGIN_BOTTOM;
	if (pw <= 0 || ph <= 0) return;

	double maxF = m_spectrum->max_frequency();
	double peak = m_spectrum->peak_dB();
	double floorDB = m_spectrum->floor_dB();
	double dB_range = peak - floorDB;

	int left   = MARGIN_LEFT;
	int right  = MARGIN_LEFT + pw;
	int top    = MARGIN_TOP;
	int bottom = MARGIN_TOP + ph;

	// ── Local coordinate lambdas ────────────────
	auto freqToX = [&](double hz) -> double {
		if (maxF <= 0) return left;
		return left + (hz / maxF) * pw;
	};

	auto dBtoY = [&](double dB) -> double {
		if (dB_range <= 0) return top + ph / 2.0;
		return top + (1.0 - (dB - floorDB) / dB_range) * ph;
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

	// X-axis (frequency): tick spacing.
	double tickF = 1000;
	if (maxF <= 1000)       tickF = 100;
	else if (maxF <= 5000)  tickF = 500;
	else if (maxF <= 12000) tickF = 1000;
	else                    tickF = 2000;

	// X-axis grid lines.
	p.setPen(QPen(GRID_COLOR, 1, Qt::DotLine));
	for (double f = tickF; f < maxF; f += tickF)
	{
		double x = freqToX(f);
		p.drawLine(QPointF(x, top), QPointF(x, bottom));
	}

	// X-axis tick marks and labels.
	p.setPen(QPen(AXIS_COLOR, 1));
	for (double f = 0; f <= maxF; f += tickF)
	{
		double x = freqToX(f);
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
		p.drawText(left + (pw - tw) / 2, bottom + MARGIN_BOTTOM - 4, title);
	}

	// Y-axis (power dB): tick spacing.
	double tickDB = 10;
	if (dB_range <= 20)      tickDB = 5;
	else if (dB_range <= 50) tickDB = 10;
	else                     tickDB = 20;

	double dB_start = std::floor(floorDB / tickDB) * tickDB;
	double dB_end   = std::ceil(peak / tickDB) * tickDB;

	// Y-axis grid lines.
	p.setPen(QPen(GRID_COLOR, 1, Qt::DotLine));
	for (double dB = dB_start; dB <= dB_end; dB += tickDB)
	{
		if (dB <= floorDB || dB >= peak) continue;
		double y = dBtoY(dB);
		p.drawLine(QPointF(left, y), QPointF(right, y));
	}

	// Y-axis tick marks and labels.
	p.setPen(QPen(AXIS_COLOR, 1));
	for (double dB = dB_start; dB <= dB_end; dB += tickDB)
	{
		double y = dBtoY(dB);
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
		p.translate(14, top + ph / 2);
		p.rotate(-90);
		int tw = fm.horizontalAdvance(title);
		p.drawText(-tw / 2, 0, title);
		p.restore();
	}

	// ── Spectrum curve ──────────────────────────
	const auto &power = m_spectrum->power_dB();
	if (power.empty()) return;

	p.setPen(QPen(CURVE_COLOR, 1.5));
	p.setClipRect(left, top, pw, ph);

	QPainterPath path;
	bool first = true;

	for (intptr_t k = 0; k < m_spectrum->bin_count(); k++)
	{
		double freq = m_spectrum->bin_frequency(k);
		if (freq > maxF) break;

		double x = freqToX(freq);
		double y = dBtoY(power[k]);

		if (first) {
			path.moveTo(x, y);
			first = false;
		}
		else {
			path.lineTo(x, y);
		}
	}

	p.drawPath(path);
	p.setClipping(false);
}


// ─────────────────────────────────────────────────
//  Cache building
// ─────────────────────────────────────────────────

void SpectrumView::rebuildCache()
{
	int w = width();
	int h = height() - m_toolbar->height() - m_status->height();

	m_cache = QPixmap(QSize(w, h) * devicePixelRatioF());
	m_cache.setDevicePixelRatio(devicePixelRatioF());

	QPainter painter(&m_cache);
	renderPlot(painter, w, h);

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

	// Draw the cached plot below the toolbar.
	if (!m_cache.isNull())
		p.drawPixmap(0, m_toolbar->height(), m_cache);

	// ── Crosshair at mouse position ────────────
	if (m_mouse_x >= marginLeft() && m_mouse_x <= marginLeft() + plotWidth() &&
	    m_mouse_y >= marginTop()  && m_mouse_y <= marginTop() + plotHeight())
	{
		p.setPen(QPen(CROSSHAIR_COLOR, 1, Qt::DashLine));
		int cl = marginLeft();
		int cr = cl + plotWidth();
		int ct = marginTop();
		int cb = ct + plotHeight();

		// Vertical line.
		p.drawLine(QPointF(m_mouse_x, ct), QPointF(m_mouse_x, cb));
		// Horizontal line.
		p.drawLine(QPointF(cl, m_mouse_y), QPointF(cr, m_mouse_y));
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


// ─────────────────────────────────────────────────
//  Export
// ─────────────────────────────────────────────────

void SpectrumView::onSavePNG()
{
	QString path = QFileDialog::getSaveFileName(this,
		tr("Save spectrum as PNG"), QString(), tr("PNG image (*.png)"));
	if (path.isEmpty()) return;

	// Export at 2× resolution using the device-pixel-ratio pattern:
	// the pixmap has twice as many physical pixels, but the painter
	// operates in logical coordinates so text and margins scale correctly.
	const int scale = 2;
	int w = width();
	int h = height() - m_toolbar->height() - m_status->height();

	QPixmap pixmap(QSize(w, h) * scale);
	pixmap.setDevicePixelRatio(scale);
	pixmap.fill(BG_COLOR);
	QPainter painter(&pixmap);
	renderPlot(painter, w, h);
	painter.end();

	if (!pixmap.save(path, "PNG"))
		QMessageBox::critical(this, tr("Export error"), tr("Could not save PNG file."));
}

void SpectrumView::onSavePDF()
{
	QString path = QFileDialog::getSaveFileName(this,
		tr("Save spectrum as PDF"), QString(), tr("PDF document (*.pdf)"));
	if (path.isEmpty()) return;

	int w = width();
	int h = height() - m_toolbar->height() - m_status->height();

	QPrinter printer(QPrinter::HighResolution);
	printer.setOutputFormat(QPrinter::PdfFormat);
	printer.setOutputFileName(path);

	// Set page size to match the plot aspect ratio.
	QPageSize pageSize(QSizeF(w, h), QPageSize::Point);
	printer.setPageSize(pageSize);
	printer.setPageMargins(QMarginsF(0, 0, 0, 0));

	QPainter painter(&printer);

	// Scale from printer resolution to our logical coordinates.
	double sx = printer.width() / double(w);
	double sy = printer.height() / double(h);
	double scale = std::min(sx, sy);
	painter.scale(scale, scale);

	renderPlot(painter, w, h);
	painter.end();
}

void SpectrumView::onSaveSVG()
{
	QString path = QFileDialog::getSaveFileName(this,
		tr("Save spectrum as SVG"), QString(), tr("SVG image (*.svg)"));
	if (path.isEmpty()) return;

	int w = width();
	int h = height() - m_toolbar->height() - m_status->height();

	QSvgGenerator svg;
	svg.setFileName(path);
	svg.setSize(QSize(w, h));
	svg.setViewBox(QRect(0, 0, w, h));
	svg.setTitle(tr("Spectral slice"));
	svg.setDescription(tr("FFT spectrum (%1 – %2 s)")
		.arg(m_spectrum->start_time(), 0, 'f', 4)
		.arg(m_spectrum->end_time(), 0, 'f', 4));

	QPainter painter(&svg);
	renderPlot(painter, w, h);
	painter.end();
}

} // namespace phonometrica
