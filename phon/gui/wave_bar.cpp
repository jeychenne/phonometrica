/***********************************************************************************************************************
 *                                                                                                                     *
 * Copyright (C) 2019-2026 Julien Eychenne                                                                             *
 *                                                                                                                     *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0. If a copy of the MPL was not   *
 * distributed with this file, You can obtain one at https://mozilla.org/MPL/2.0/.                                     *
 *                                                                                                                     *
 * Created: 22/03/2026                                                                                                 *
 *                                                                                                                     *
 * Purpose: see header.                                                                                                *
 *                                                                                                                     *
 ***********************************************************************************************************************/

#include <QPainter>
#include <QPainterPath>
#include <QMouseEvent>
#include <QWheelEvent>
#include <cmath>
#include <algorithm>
#include <phon/gui/wave_bar.hpp>

namespace phonometrica {

static const QColor WAVEBAR_SEL_COLOR(0, 0, 204, 60);
static const QColor WAVEBAR_WAVE_COLOR(0, 0, 0);
static const QColor WAVEBAR_ZERO_COLOR(0, 0, 255);

WaveBar::WaveBar(TimeModel *model, const Handle<Sound> &sound, QWidget *parent) :
	QWidget(parent), m_model(model), m_sound(sound)
{
	setFixedHeight(50);
	setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
	setMouseTracking(true);

	// Compute global peak magnitude across all channels (once).
	// Data is stored as contiguous channels, but for peak magnitude we just
	// scan everything — order doesn't matter.
	auto *raw = m_sound->data().data();
	auto total = m_sound->size();

	for (intptr_t i = 0; i < total; i++)
	{
		double m = std::abs(double(raw[i]));
		if (m > m_magnitude)
			m_magnitude = m;
	}

	if (m_magnitude == 0.0)
		m_magnitude = 1.0; // Avoid division by zero for silent files.

	connect(m_model, &TimeModel::viewportChanged, this, &WaveBar::onViewportChanged);
}


// ─────────────────────────────────────────────────
//  Coordinate mapping (full file duration)
// ─────────────────────────────────────────────────

double WaveBar::timeToX(double t) const
{
	return t * width() / m_sound->duration();
}

double WaveBar::xToTime(double x) const
{
	return x * m_sound->duration() / width();
}

double WaveBar::sampleToY(double s) const
{
	double H = height() / 2.0;
	return H - s * H / m_magnitude;
}


// ─────────────────────────────────────────────────
//  Cache
// ─────────────────────────────────────────────────

void WaveBar::rebuildCache()
{
	int w = width();
	if (w <= 0) return;

	auto nframes = m_sound->channel_size();
	int nchannel = m_sound->nchannel();

	// Get a span for each channel to average them.
	std::vector<std::span<const float>> channels;
	for (int c = 1; c <= nchannel; c++)
		channels.push_back(m_sound->channel_view(c));

	if (nframes >= w * 2)
	{
		// Min/max decimation: two values per pixel.
		m_cache.resize(w * 2);
		double offset = double(nframes) / w;

		for (int i = 0; i < w; i++)
		{
			auto x1 = intptr_t(floor(i * offset));
			auto x2 = intptr_t(ceil((i + 1) * offset));
			if (x2 > nframes) x2 = nframes;

			// First frame, averaged across channels.
			double s = 0;
			for (int c = 0; c < nchannel; c++)
				s += channels[c][x1];
			s /= nchannel;

			double maximum = s, minimum = s;

			for (auto x = x1 + 1; x < x2; x++)
			{
				double sample = 0;
				for (int c = 0; c < nchannel; c++)
					sample += channels[c][x];
				sample /= nchannel;

				if (sample < minimum) minimum = sample;
				else if (sample > maximum) maximum = sample;
			}

			m_cache[i * 2] = sampleToY(maximum);
			m_cache[i * 2 + 1] = sampleToY(minimum);
		}
	}
	else
	{
		// Fewer frames than pixels: plot every sample (averaged).
		m_cache.resize(nframes);
		for (intptr_t x = 0; x < nframes; x++)
		{
			double s = 0;
			for (int c = 0; c < nchannel; c++)
				s += channels[c][x];
			s /= nchannel;
			m_cache[x] = sampleToY(s);
		}
	}

	m_cached_width = w;
}


// ─────────────────────────────────────────────────
//  Painting
// ─────────────────────────────────────────────────

void WaveBar::paintEvent(QPaintEvent *)
{
	if (m_cached_width != width())
		rebuildCache();

	QPainter p(this);
	p.setRenderHint(QPainter::Antialiasing, true);
	p.fillRect(rect(), Qt::white);

	int w = width();
	int h = height();

	// Zero-crossing line.
	p.setPen(QPen(WAVEBAR_ZERO_COLOR, 1, Qt::DotLine));
	p.drawLine(0, h / 2, w, h / 2);

	// Draw waveform.
	p.setPen(QPen(WAVEBAR_WAVE_COLOR, 1));
	QPainterPath path;

	if (m_cache.size() == (size_t)w * 2)
	{
		path.moveTo(0.0, m_cache[0]);
		path.lineTo(0.0, m_cache[1]);

		for (size_t i = 2; i < m_cache.size(); i += 2)
		{
			double x = double(i) / 2.0;
			path.lineTo(x, m_cache[i]);
			path.lineTo(x, m_cache[i + 1]);
		}
	}
	else if (!m_cache.empty())
	{
		double offset = double(w) / m_cache.size();
		path.moveTo(0.0, m_cache[0]);

		for (size_t i = 1; i < m_cache.size(); ++i)
		{
			double x = double(i) * offset;
			path.lineTo(x, m_cache[i]);
		}
	}

	p.drawPath(path);

	// Draw viewport rectangle: use pending selection during new-selection drag,
	// model viewport otherwise (slide/resize update the model in real time).
	double x1, x2;
	if (m_drag_mode == DragMode::NewSelection && m_pending_x1 >= 0)
	{
		x1 = m_pending_x1;
		x2 = m_pending_x2;
	}
	else
	{
		x1 = timeToX(m_model->windowStart());
		x2 = timeToX(m_model->windowEnd());
	}
	p.fillRect(QRectF(x1, 0, x2 - x1, h), WAVEBAR_SEL_COLOR);
}

void WaveBar::resizeEvent(QResizeEvent *)
{
	m_cached_width = -1; // Force recompute.
	// Re-emit pixel coordinates so the SoundZoom widget updates its trapezoid.
	emit viewportPixelsChanged(timeToX(m_model->windowStart()), timeToX(m_model->windowEnd()));
}


// ─────────────────────────────────────────────────
//  Mouse interaction
// ─────────────────────────────────────────────────

WaveBar::HitZone WaveBar::hitTest(double x) const
{
	double vx1 = timeToX(m_model->windowStart());
	double vx2 = timeToX(m_model->windowEnd());

	if (x >= vx1 - EDGE_GRAB_PX && x <= vx1 + EDGE_GRAB_PX)
		return HitZone::LeftEdge;
	if (x >= vx2 - EDGE_GRAB_PX && x <= vx2 + EDGE_GRAB_PX)
		return HitZone::RightEdge;
	if (x > vx1 + EDGE_GRAB_PX && x < vx2 - EDGE_GRAB_PX)
		return HitZone::Inside;
	return HitZone::Outside;
}

void WaveBar::mousePressEvent(QMouseEvent *event)
{
	if (event->button() != Qt::LeftButton)
		return;

	double x = event->position().x();
	bool force_new = event->modifiers() & Qt::AltModifier;
	auto zone = hitTest(x);

	if (force_new || zone == HitZone::Outside)
	{
		// Start a brand-new viewport selection.
		m_drag_mode = DragMode::NewSelection;
		m_drag_start_x = x;
		m_pending_x1 = x;
		m_pending_x2 = x;
	}
	else if (zone == HitZone::LeftEdge)
	{
		m_drag_mode = DragMode::ResizeLeft;
		m_drag_start_x = x;
	}
	else if (zone == HitZone::RightEdge)
	{
		m_drag_mode = DragMode::ResizeRight;
		m_drag_start_x = x;
	}
	else // Inside
	{
		m_drag_mode = DragMode::SlideViewport;
		m_drag_start_x = x;
		m_slide_start_t1 = m_model->windowStart();
		m_slide_start_t2 = m_model->windowEnd();
		setCursor(Qt::ClosedHandCursor);
	}
}

void WaveBar::mouseMoveEvent(QMouseEvent *event)
{
	double x = std::clamp(event->position().x(), 0.0, (double)width());

	if (m_drag_mode == DragMode::None)
	{
		// Update cursor shape based on hover position.
		auto zone = hitTest(event->position().x());
		if (zone == HitZone::LeftEdge || zone == HitZone::RightEdge)
			setCursor(Qt::SizeHorCursor);
		else if (zone == HitZone::Inside)
			setCursor(Qt::OpenHandCursor);
		else
			setCursor(Qt::ArrowCursor);
		return;
	}

	if (m_drag_mode == DragMode::NewSelection)
	{
		m_pending_x1 = std::min(m_drag_start_x, x);
		m_pending_x2 = std::max(m_drag_start_x, x);
		update();
		emit viewportPixelsChanged(m_pending_x1, m_pending_x2);
	}
	else if (m_drag_mode == DragMode::SlideViewport)
	{
		double dt = xToTime(x) - xToTime(m_drag_start_x);
		double t1 = m_slide_start_t1 + dt;
		double t2 = m_slide_start_t2 + dt;
		double dur = t2 - t1;

		// Clamp to file boundaries without changing the window duration.
		if (t1 < 0) { t1 = 0; t2 = dur; }
		if (t2 > m_sound->duration()) { t2 = m_sound->duration(); t1 = t2 - dur; }

		m_model->setViewport(t1, t2);
	}
	else if (m_drag_mode == DragMode::ResizeLeft)
	{
		double t = std::clamp(xToTime(x), 0.0, m_model->windowEnd() - 0.001);
		m_model->setViewport(t, m_model->windowEnd());
	}
	else if (m_drag_mode == DragMode::ResizeRight)
	{
		double t = std::clamp(xToTime(x), m_model->windowStart() + 0.001, m_sound->duration());
		m_model->setViewport(m_model->windowStart(), t);
	}
}

void WaveBar::mouseReleaseEvent(QMouseEvent *event)
{
	if (event->button() != Qt::LeftButton || m_drag_mode == DragMode::None)
		return;

	double x = std::clamp(event->position().x(), 0.0, (double)width());

	if (m_drag_mode == DragMode::NewSelection)
	{
		double x1 = std::min(m_drag_start_x, x);
		double x2 = std::max(m_drag_start_x, x);

		// If it was just a click (not a drag), center the viewport on that point.
		if (std::abs(x2 - x1) < 3)
		{
			double t = xToTime(event->position().x());
			double halfWin = m_model->windowDuration() / 2;
			m_model->setViewport(t - halfWin, t + halfWin);
		}
		else
		{
			m_model->setViewport(xToTime(x1), xToTime(x2));
		}

		m_pending_x1 = -1;
		m_pending_x2 = -1;
	}
	// SlideViewport and Resize modes already updated the model during drag.

	m_drag_mode = DragMode::None;
	setCursor(Qt::ArrowCursor);
}

void WaveBar::wheelEvent(QWheelEvent *event)
{
	int delta = event->angleDelta().y();
	if (delta > 0)
		m_model->moveBackward();
	else if (delta < 0)
		m_model->moveForward();
}

void WaveBar::leaveEvent(QEvent *)
{
	if (m_drag_mode == DragMode::None)
		setCursor(Qt::ArrowCursor);
}


// ─────────────────────────────────────────────────
//  Model signal handlers
// ─────────────────────────────────────────────────

void WaveBar::onViewportChanged(double start, double end)
{
	update();
	emit viewportPixelsChanged(timeToX(start), timeToX(end));
}

double WaveBar::viewportLeftX() const
{
	return timeToX(m_model->windowStart());
}

double WaveBar::viewportRightX() const
{
	return timeToX(m_model->windowEnd());
}

} // namespace phonometrica
