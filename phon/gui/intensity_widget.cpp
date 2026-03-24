/***********************************************************************************************************************
 *                                                                                                                     *
 * Copyright (C) 2019-2026 Julien Eychenne                                                                             *
 *                                                                                                                     *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0. If a copy of the MPL was not   *
 * distributed with this file, You can obtain one at https://mozilla.org/MPL/2.0/.                                     *
 *                                                                                                                     *
 * Created: 23/03/2026                                                                                                 *
 *                                                                                                                     *
 * Purpose: see header.                                                                                                *
 *                                                                                                                     *
 ***********************************************************************************************************************/

#include <cmath>
#include <algorithm>
#include <QPainter>
#include <QPainterPath>
#include <QMouseEvent>
#include <QWheelEvent>
#include <phon/gui/intensity_widget.hpp>
#include <phon/application/settings.hpp>

namespace phonometrica {

// Overlay colours — same as WaveformWidget / SpectrogramWidget.
static const QColor SELECTION_COLOR(209, 116, 23, 50);
static const QColor POINT_SEL_COLOR(199, 179, 0);
static const QColor PLAYBACK_COLOR(255, 0, 0);
static const QColor INTENSITY_COLOR(0, 180, 0); // green curve

IntensityWidget::IntensityWidget(TimeModel *model, const Handle<Sound> &sound, int channel, QWidget *parent) :
	QWidget(parent), m_model(model), m_sound(sound), m_channel(channel)
{
	setMinimumHeight(40);
	setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);

	// Enable mouse tracking for cursor updates.
	QWidget::setMouseTracking(true);

	try {
		readSettings();
	}
	catch (std::exception &) {
		Settings::reset_intensity();
		readSettings();
	}

	connect(m_model, &TimeModel::viewportChanged, this, &IntensityWidget::onViewportChanged);
	connect(m_model, &TimeModel::selectionChanged, this, &IntensityWidget::onSelectionChanged);
	connect(m_model, &TimeModel::selectionCleared, this, &IntensityWidget::onSelectionCleared);
	connect(m_model, &TimeModel::cursorChanged, this, &IntensityWidget::onCursorChanged);
	connect(m_model, &TimeModel::cursorCleared, this, &IntensityWidget::onCursorCleared);
	connect(m_model, &TimeModel::playbackTimeChanged, this, &IntensityWidget::onPlaybackChanged);
	connect(m_model, &TimeModel::playbackCleared, this, &IntensityWidget::onPlaybackCleared);
}

void IntensityWidget::readSettings()
{
	String category("intensity");
	m_min_dB = Settings::get_number(category, "minimum_intensity");
	m_max_dB = Settings::get_number(category, "maximum_intensity");
	m_time_step = Settings::get_number(category, "time_step");

	m_cache_valid = false;
}

void IntensityWidget::setMouseTracking(bool enabled)
{
	m_mouse_tracking_enabled = enabled;
}


// ─────────────────────────────────────────────────
//  Coordinate mapping
// ─────────────────────────────────────────────────

double IntensityWidget::timeToX(double t) const
{
	auto start = m_model->windowStart();
	auto dur = m_model->windowDuration();
	if (dur <= 0) return 0;
	return (t - start) / dur * width();
}

double IntensityWidget::xToTime(double x) const
{
	auto start = m_model->windowStart();
	auto dur = m_model->windowDuration();
	return start + x / width() * dur;
}

double IntensityWidget::intensityToY(double dB) const
{
	auto h = double(height());
	return h - ((dB - m_min_dB) * h / (m_max_dB - m_min_dB));
}

double IntensityWidget::yToIntensity(int y) const
{
	auto h = double(height());
	return (m_max_dB - m_min_dB) * (h - y) / h + m_min_dB;
}


// ─────────────────────────────────────────────────
//  Intensity computation
// ─────────────────────────────────────────────────

std::vector<double> IntensityWidget::computeIntensity()
{
	auto window_duration = m_model->windowDuration();

	// At least 2 measurements per window.
	if (window_duration <= 2 * m_time_step)
	{
		return {}; // too zoomed in
	}

	// At most 2 measurements per pixel.
	if (window_duration / m_time_step > width() * 2)
	{
		return {}; // too zoomed out
	}

	bool start_at_zero = true;
	auto result = m_sound->get_intensity(m_channel, m_model->windowStart(),
		m_model->windowEnd(), m_time_step, start_at_zero);

	// Convert from phonometrica::Array to std::vector.
	std::vector<double> vec(result.size());
	for (intptr_t i = 0; i < result.size(); i++) {
		vec[i] = result[i + 1]; // Array is 1-based
	}

	return vec;
}


// ─────────────────────────────────────────────────
//  Cache building
// ─────────────────────────────────────────────────

void IntensityWidget::rebuildCache()
{
	int w = width();
	int h = height();

	m_cache = QPixmap(w, h);
	m_cache.fill(Qt::white);

	if (w <= 0 || h <= 0) {
		m_cache_valid = true;
		return;
	}

	QPainter painter(&m_cache);
	painter.setRenderHint(QPainter::Antialiasing);

	try
	{
		auto intensity = computeIntensity();

		if (intensity.empty())
		{
			// Show a message instead.
			auto window_duration = m_model->windowDuration();
			QString msg;
			if (window_duration <= 2 * m_time_step)
				msg = tr("Zoom out to see intensity");
			else
				msg = tr("Zoom in to see intensity");

			painter.setPen(Qt::black);
			QFontMetrics fm = painter.fontMetrics();
			int tx = (w - fm.horizontalAdvance(msg)) / 2;
			int ty = (h + fm.ascent()) / 2;
			painter.drawText(tx, ty, msg);
		}
		else
		{
			painter.setPen(QPen(INTENSITY_COLOR, 2));
			QPainterPath path;

			bool start_at_zero = true;
			double t = start_at_zero ? m_model->windowStart()
			                         : (m_model->windowStart() + m_time_step / 2);

			bool first = true;
			for (auto dB : intensity)
			{
				double x = timeToX(t);
				double y = intensityToY(dB);

				if (first) {
					path.moveTo(x, y);
					first = false;
				}
				else {
					path.lineTo(x, y);
				}
				t += m_time_step;
			}

			painter.drawPath(path);
		}
	}
	catch (std::exception &e)
	{
		QString msg = QString::fromUtf8(e.what());
		painter.setPen(Qt::black);
		QFontMetrics fm = painter.fontMetrics();
		int tx = (w - fm.horizontalAdvance(msg)) / 2;
		int ty = (h + fm.ascent()) / 2;
		painter.drawText(tx, ty, msg);
	}

	m_cache_valid = true;
}


// ─────────────────────────────────────────────────
//  Painting
// ─────────────────────────────────────────────────

void IntensityWidget::paintEvent(QPaintEvent *)
{
	if (!m_cache_valid)
		rebuildCache();

	QPainter p(this);

	// Draw the cached intensity curve.
	if (!m_cache.isNull())
		p.drawPixmap(0, 0, m_cache);

	int w = width();
	int h = height();

	// Draw span selection overlay.
	if (m_model->hasSpanSelection())
	{
		double x1 = timeToX(m_model->selectionStart());
		double x2 = timeToX(m_model->selectionEnd());

		if (x2 > 0 && x1 < w)
		{
			x1 = std::max(x1, 0.0);
			x2 = std::min(x2, (double)w);
			p.fillRect(QRectF(x1, 0, x2 - x1, h), SELECTION_COLOR);
		}
	}

	// Draw point selection (cursor line).
	if (m_model->hasPointSelection())
	{
		double x = timeToX(m_model->selectionStart());
		if (x >= 0 && x <= w)
		{
			p.setPen(QPen(POINT_SEL_COLOR, 1));
			p.drawLine(QPointF(x, 0), QPointF(x, h));
		}
	}

	// Draw mouse tracking cursor.
	if (m_model->hasCursor())
	{
		double x = timeToX(m_model->cursorTime());
		if (x >= 0 && x <= w)
		{
			p.setPen(QPen(Qt::gray, 1, Qt::DashLine));
			p.drawLine(QPointF(x, 0), QPointF(x, h));
		}
	}

	// Draw playback tick.
	if (m_model->isPlaying())
	{
		double x = timeToX(m_model->playbackTime());
		if (x >= 0 && x <= w)
		{
			p.setPen(QPen(PLAYBACK_COLOR, 1));
			p.drawLine(QPointF(x, 0), QPointF(x, h));
		}
	}
}

void IntensityWidget::resizeEvent(QResizeEvent *)
{
	m_cache_valid = false;
}


// ─────────────────────────────────────────────────
//  Mouse interaction
// ─────────────────────────────────────────────────

void IntensityWidget::mousePressEvent(QMouseEvent *event)
{
	if (event->button() == Qt::LeftButton)
	{
		m_dragging = true;
		m_drag_start_time = xToTime(event->position().x());
		m_model->setSelection(m_drag_start_time, m_drag_start_time);
	}
	else if (event->button() == Qt::MiddleButton)
	{
		m_model->zoomToSelection();
	}
}

void IntensityWidget::mouseMoveEvent(QMouseEvent *event)
{
	double t = xToTime(event->position().x());
	t = std::clamp(t, 0.0, m_model->duration());

	if (m_dragging)
	{
		m_model->setSelection(m_drag_start_time, t);
	}
	else if (m_mouse_tracking_enabled)
	{
		m_model->setCursor(t);
	}
}

void IntensityWidget::mouseReleaseEvent(QMouseEvent *event)
{
	if (event->button() == Qt::LeftButton && m_dragging)
	{
		m_dragging = false;
		double t = xToTime(event->position().x());
		t = std::clamp(t, 0.0, m_model->duration());
		if (std::abs(t - m_drag_start_time) < 0.001)
			m_model->setSelection(m_drag_start_time, m_drag_start_time);
		else
			m_model->setSelection(m_drag_start_time, t);
	}
}

void IntensityWidget::wheelEvent(QWheelEvent *event)
{
	double t = xToTime(event->position().x());
	int delta = event->angleDelta().y();

	if (delta > 0)
		m_model->zoomIn(t);
	else if (delta < 0)
		m_model->zoomOut(t);
}

void IntensityWidget::leaveEvent(QEvent *)
{
	if (m_mouse_tracking_enabled)
		m_model->clearCursor();
}


// ─────────────────────────────────────────────────
//  Model signal handlers
// ─────────────────────────────────────────────────

void IntensityWidget::onViewportChanged(double, double)
{
	m_cache_valid = false;
	update();
}

void IntensityWidget::onSelectionChanged(double, double)
{
	update(); // Repaint overlays only.
}

void IntensityWidget::onSelectionCleared()
{
	update();
}

void IntensityWidget::onCursorChanged(double)
{
	update();
}

void IntensityWidget::onCursorCleared()
{
	update();
}

void IntensityWidget::onPlaybackChanged(double)
{
	update();
}

void IntensityWidget::onPlaybackCleared()
{
	update();
}

} // namespace phonometrica
