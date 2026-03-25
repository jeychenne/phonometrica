/***********************************************************************************************************************
 *                                                                                                                     *
 * Copyright (C) 2019-2026 Julien Eychenne                                                                             *
 *                                                                                                                     *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0. If a copy of the MPL was not   *
 * distributed with this file, You can obtain one at https://mozilla.org/MPL/2.0/.                                     *
 *                                                                                                                     *
 * Created: 24/03/2026                                                                                                 *
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
#include <phon/gui/pitch_widget.hpp>
#include <phon/application/settings.hpp>

namespace phonometrica {

// Overlay colours — same as WaveformWidget / SpectrogramWidget / IntensityWidget.
static const QColor SELECTION_COLOR(209, 116, 23, 50);
static const QColor POINT_SEL_COLOR(199, 179, 0);
static const QColor PLAYBACK_COLOR(255, 0, 0);
static const QColor PITCH_COLOR(0, 0, 255); // blue curve (same as wx version)

PitchWidget::PitchWidget(TimeModel *model, const Handle<Sound> &sound, int channel, QWidget *parent) :
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
		Settings::reset_pitch_tracking();
		readSettings();
	}

	connect(m_model, &TimeModel::viewportChanged, this, &PitchWidget::onViewportChanged);
	connect(m_model, &TimeModel::selectionChanged, this, &PitchWidget::onSelectionChanged);
	connect(m_model, &TimeModel::selectionCleared, this, &PitchWidget::onSelectionCleared);
	connect(m_model, &TimeModel::cursorChanged, this, &PitchWidget::onCursorChanged);
	connect(m_model, &TimeModel::cursorCleared, this, &PitchWidget::onCursorCleared);
	connect(m_model, &TimeModel::playbackTimeChanged, this, &PitchWidget::onPlaybackChanged);
	connect(m_model, &TimeModel::playbackCleared, this, &PitchWidget::onPlaybackCleared);
}

void PitchWidget::readSettings()
{
	String category("pitch_tracking");
	auto method = Settings::get_string(category, "method");
	m_min_pitch = Settings::get_number(category, "minimum_pitch");
	m_max_pitch = Settings::get_number(category, "maximum_pitch");
	m_time_step = Settings::get_number(category, "time_step");
	m_voicing_threshold = Settings::get_number(category, "voicing_threshold");

	if (method == "harvest") {
		m_algorithm = speech::PitchTracker::Harvest;
	}
	else if (method == "rapt") {
		m_algorithm = speech::PitchTracker::Rapt;
	}
	else if (method == "reaper") {
		m_algorithm = speech::PitchTracker::Reaper;
	}
	else if (method == "swipe") {
		m_algorithm = speech::PitchTracker::Swipe;
	}
	else if (method == "praat") {
		m_algorithm = speech::PitchTracker::Praat;
	}
	else {
		// Fallback to reaper if unknown method.
		m_algorithm = speech::PitchTracker::Reaper;
	}

	// Praat-specific parameters (read always, used only by Praat).
	try {
		m_octave_jump_cost = Settings::get_number(category, "octave_jump_cost");
	} catch (...) {
		m_octave_jump_cost = 0.35;
	}
	try {
		m_voicing_cost = Settings::get_number(category, "voicing_cost");
	} catch (...) {
		m_voicing_cost = 0.45;
	}
	try {
		m_silence_threshold = Settings::get_number(category, "silence_threshold");
	} catch (...) {
		m_silence_threshold = 0.03;
	}
	try {
		m_octave_cost = Settings::get_number(category, "octave_cost");
	} catch (...) {
		m_octave_cost = 0.01;
	}

	m_cache_valid = false;
}

void PitchWidget::setMouseTracking(bool enabled)
{
	m_mouse_tracking_enabled = enabled;
}

void PitchWidget::setTopPlot(bool top)
{
	if (m_is_top != top)
	{
		m_is_top = top;
		update();
	}
}


// ─────────────────────────────────────────────────
//  Coordinate mapping
// ─────────────────────────────────────────────────

double PitchWidget::timeToX(double t) const
{
	auto start = m_model->windowStart();
	auto dur = m_model->windowDuration();
	if (dur <= 0) return 0;
	return (t - start) / dur * width();
}

double PitchWidget::xToTime(double x) const
{
	auto start = m_model->windowStart();
	auto dur = m_model->windowDuration();
	return start + x / width() * dur;
}

double PitchWidget::pitchToY(double hz) const
{
	auto h = double(height());
	return h - ((hz - m_min_pitch) * h / (m_max_pitch - m_min_pitch));
}

double PitchWidget::yToPitch(int y) const
{
	auto h = double(height());
	return (m_max_pitch - m_min_pitch) * (h - y) / h + m_min_pitch;
}


// ─────────────────────────────────────────────────
//  Pitch computation
// ─────────────────────────────────────────────────

std::vector<double> PitchWidget::computePitch()
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

	auto first_sample = m_sound->time_to_frame(m_model->windowStart());
	auto last_sample = m_sound->time_to_frame(m_model->windowEnd());
	auto input = m_sound->get_channel(m_channel, first_sample, last_sample);
	auto sample_rate = m_sound->sample_rate();

	return speech::get_pitch(m_algorithm, input, sample_rate, m_min_pitch, m_max_pitch,
	                         m_time_step, m_voicing_threshold, m_octave_jump_cost, m_voicing_cost,
	                         m_silence_threshold, m_octave_cost);
}


// ─────────────────────────────────────────────────
//  Cache building
// ─────────────────────────────────────────────────

void PitchWidget::rebuildCache()
{
	int w = width();
	int h = height();

	m_cache = QPixmap(size() * devicePixelRatioF());
	m_cache.setDevicePixelRatio(devicePixelRatioF());
	m_cache.fill(Qt::white);

	if (w <= 0 || h <= 0) {
		m_cache_valid = true;
		return;
	}

	QPainter painter(&m_cache);
	painter.setRenderHint(QPainter::Antialiasing);

	try
	{
		auto pitch = computePitch();

		if (pitch.empty())
		{
			// Show a message instead.
			auto window_duration = m_model->windowDuration();
			QString msg;
			if (window_duration <= 2 * m_time_step)
				msg = tr("Zoom out to see pitch");
			else
				msg = tr("Zoom in to see pitch");

			painter.setPen(Qt::black);
			QFontMetrics fm = painter.fontMetrics();
			int tx = (w - fm.horizontalAdvance(msg)) / 2;
			int ty = (h + fm.ascent()) / 2;
			painter.drawText(tx, ty, msg);
		}
		else
		{
			painter.setPen(QPen(PITCH_COLOR, 2));
			QPainterPath path;

			double t = m_model->windowStart();
			bool previous = false;

			for (auto f : pitch)
			{
				if (std::isfinite(f) && f > 0)
				{
					double x = timeToX(t);
					double y = pitchToY(f);

					if (previous) {
						path.lineTo(x, y);
					}
					else {
						path.moveTo(x, y);
					}
					previous = true;
				}
				else
				{
					previous = false;
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

void PitchWidget::paintEvent(QPaintEvent *)
{
	if (!m_cache_valid)
		rebuildCache();

	QPainter p(this);

	// Draw the cached pitch curve.
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

			// Draw the cursor time label on the top-most visible plot.
			if (m_is_top)
			{
				QString time = QString::number(m_model->cursorTime(), 'f', 4);
				QFont font = p.font();
				font.setPointSizeF(font.pointSizeF() * 0.9);
				p.setFont(font);
				QFontMetrics fm(font);
				int tw = fm.horizontalAdvance(time);
				int th = fm.height();
				int pad = 2;

				int lx = int(x) + 4;
				if (lx + tw + pad > w)
					lx = int(x) - tw - 4;
				int ly = pad;

				p.fillRect(lx - pad, ly, tw + 2 * pad, th + pad, QColor(255, 255, 255, 180));
				p.setPen(Qt::darkGray);
				p.drawText(lx, ly + fm.ascent(), time);
			}
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

void PitchWidget::resizeEvent(QResizeEvent *)
{
	m_cache_valid = false;
}


// ─────────────────────────────────────────────────
//  Mouse interaction
// ─────────────────────────────────────────────────

void PitchWidget::mousePressEvent(QMouseEvent *event)
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

void PitchWidget::mouseMoveEvent(QMouseEvent *event)
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

void PitchWidget::mouseReleaseEvent(QMouseEvent *event)
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

void PitchWidget::wheelEvent(QWheelEvent *event)
{
	double t = xToTime(event->position().x());
	int delta = event->angleDelta().y();

	if (delta > 0)
		m_model->zoomIn(t);
	else if (delta < 0)
		m_model->zoomOut(t);
}

void PitchWidget::leaveEvent(QEvent *)
{
	if (m_mouse_tracking_enabled)
		m_model->clearCursor();
}


// ─────────────────────────────────────────────────
//  Model signal handlers
// ─────────────────────────────────────────────────

void PitchWidget::onViewportChanged(double, double)
{
	m_cache_valid = false;
	update();
}

void PitchWidget::onSelectionChanged(double, double)
{
	update(); // Repaint overlays only.
}

void PitchWidget::onSelectionCleared()
{
	update();
}

void PitchWidget::onCursorChanged(double)
{
	update();
}

void PitchWidget::onCursorCleared()
{
	update();
}

void PitchWidget::onPlaybackChanged(double)
{
	update();
}

void PitchWidget::onPlaybackCleared()
{
	update();
}

} // namespace phonometrica
