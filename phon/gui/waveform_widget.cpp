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
#include <phon/gui/waveform_widget.hpp>

namespace phonometrica {

// Colours matching the wx version.
static const QColor ZERO_LINE_COLOR(0, 0, 255);           // blue dotted
static const QColor WAVEFORM_COLOR(0, 0, 0);              // black
static const QColor SELECTION_COLOR(209, 116, 23, 50);    // semi-transparent orange
static const QColor POINT_SEL_COLOR(199, 179, 0);         // gold cursor
static const QColor PLAYBACK_COLOR(255, 0, 0);            // red tick

WaveformWidget::WaveformWidget(TimeModel *model, const Handle<Sound> &sound, int channel, QWidget *parent) :
	QWidget(parent), m_model(model), m_sound(sound), m_channel(channel)
{
	setMinimumHeight(60);
	setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);

	// Enable mouse tracking for cursor updates (hover without clicking).
	QWidget::setMouseTracking(true);

	connect(m_model, &TimeModel::viewportChanged, this, &WaveformWidget::onViewportChanged);
	connect(m_model, &TimeModel::selectionChanged, this, &WaveformWidget::onSelectionChanged);
	connect(m_model, &TimeModel::selectionCleared, this, &WaveformWidget::onSelectionCleared);
	connect(m_model, &TimeModel::cursorChanged, this, &WaveformWidget::onCursorChanged);
	connect(m_model, &TimeModel::cursorCleared, this, &WaveformWidget::onCursorCleared);
	connect(m_model, &TimeModel::playbackTimeChanged, this, &WaveformWidget::onPlaybackChanged);
	connect(m_model, &TimeModel::playbackCleared, this, &WaveformWidget::onPlaybackCleared);
}

void WaveformWidget::setGlobalMagnitude(double value)
{
	m_global_magnitude = value;
	m_magnitude = value;
	m_cache_valid = false;
	update();
}

void WaveformWidget::setMouseTracking(bool enabled)
{
	m_mouse_tracking_enabled = enabled;
}


// ─────────────────────────────────────────────────
//  Coordinate mapping
// ─────────────────────────────────────────────────

double WaveformWidget::timeToX(double t) const
{
	auto start = m_model->windowStart();
	auto dur = m_model->windowDuration();
	if (dur <= 0) return 0;
	return (t - start) / dur * width();
}

double WaveformWidget::xToTime(double x) const
{
	auto start = m_model->windowStart();
	auto dur = m_model->windowDuration();
	return start + x / width() * dur;
}

double WaveformWidget::sampleToY(double sample) const
{
	double H = height() / 2.0;
	return H - sample * H / m_magnitude;
}


// ─────────────────────────────────────────────────
//  Cache management
// ─────────────────────────────────────────────────

void WaveformWidget::rebuildCache()
{
	drawWaveformToPixmap();
	m_cache_valid = true;
}

void WaveformWidget::drawWaveformToPixmap()
{
	m_cache = QPixmap(size() * devicePixelRatioF());
	m_cache.setDevicePixelRatio(devicePixelRatioF());
	m_cache.fill(Qt::white);

	QPainter p(&m_cache);
	p.setRenderHint(QPainter::Antialiasing, true);
	int w = width();
	int h = height();

	// Zero-crossing line.
	QPen zeroPen(ZERO_LINE_COLOR, 1, Qt::DotLine);
	p.setPen(zeroPen);
	p.drawLine(0, h / 2, w, h / 2);

	auto wave = computeWaveform();
	if (wave.empty())
		return;

	QPen wavePen(WAVEFORM_COLOR, 1);
	p.setPen(wavePen);

	QPainterPath path;

	if (wave.size() == (size_t)w * 2)
	{
		// Min/max pairs: draw vertical lines per pixel.
		path.moveTo(0.0, wave[0]);
		path.lineTo(0.0, wave[1]);

		for (size_t i = 2; i < wave.size(); i += 2)
		{
			double x = double(i) / 2.0;
			path.lineTo(x, wave[i]);
			path.lineTo(x, wave[i + 1]);
		}
	}
	else
	{
		// Fewer samples than pixels: draw each sample.
		double offset = double(w) / wave.size();
		path.moveTo(0.0, wave[0]);

		for (size_t i = 1; i < wave.size(); ++i)
		{
			double x = double(i) * offset;
			path.lineTo(x, wave[i]);
		}

		// Draw dots for individual samples when very zoomed in.
		if (wave.size() < (size_t)w / 4)
		{
			p.setBrush(WAVEFORM_COLOR);
			for (size_t i = 0; i < wave.size(); ++i)
			{
				double x = double(i) * offset;
				p.drawEllipse(QPointF(x, wave[i]), 1.5, 1.5);
			}
		}
	}

	p.drawPath(path);
}

std::vector<double> WaveformWidget::computeWaveform()
{
	auto first_sample = m_sound->time_to_frame(m_model->windowStart());
	auto last_sample = m_sound->time_to_frame(m_model->windowEnd());

	if (last_sample - first_sample < 1)
		return {};

	int w = width();
	int nchannel = m_sound->nchannel();

	if (m_channel == 0)
	{
		// Average across channels. Get a span for each channel and average per frame.
		std::vector<std::span<const float>> channels;
		for (int c = 1; c <= nchannel; c++)
			channels.push_back(m_sound->channel_view(c, first_sample, last_sample));

		auto sample_count = (intptr_t)channels[0].size();

		if (sample_count >= w * 2)
		{
			std::vector<double> wave(w * 2);
			double offset = double(sample_count) / w;

			for (int i = 0; i < w; i++)
			{
				auto x1 = intptr_t(floor(i * offset));
				auto x2 = intptr_t(ceil((i + 1) * offset));
				if (x2 > sample_count) x2 = sample_count;

				// First frame averaged.
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

				wave[i * 2] = sampleToY(maximum);
				wave[i * 2 + 1] = sampleToY(minimum);
			}
			return wave;
		}
		else
		{
			std::vector<double> wave(sample_count);
			for (intptr_t x = 0; x < sample_count; x++)
			{
				double s = 0;
				for (int c = 0; c < nchannel; c++)
					s += channels[c][x];
				s /= nchannel;
				wave[x] = sampleToY(s);
			}
			return wave;
		}
	}
	else
	{
		// Specific channel — use channel_view for zero-copy access.
		auto data = m_sound->channel_view(m_channel, first_sample, last_sample);
		auto sample_count = (intptr_t)data.size();

		if (sample_count >= w * 2)
		{
			std::vector<double> wave(w * 2);
			double offset = double(sample_count) / w;

			for (int i = 0; i < w; i++)
			{
				auto x1 = intptr_t(floor(i * offset));
				auto x2 = intptr_t(ceil((i + 1) * offset));
				if (x2 > sample_count) x2 = sample_count;

				double maximum = data[x1];
				double minimum = maximum;

				for (auto x = x1 + 1; x < x2; x++)
				{
					double s = data[x];
					if (s < minimum) minimum = s;
					else if (s > maximum) maximum = s;
				}

				wave[i * 2] = sampleToY(maximum);
				wave[i * 2 + 1] = sampleToY(minimum);
			}
			return wave;
		}
		else
		{
			std::vector<double> wave(sample_count);
			for (intptr_t i = 0; i < sample_count; i++)
				wave[i] = sampleToY(data[i]);
			return wave;
		}
	}
}


// ─────────────────────────────────────────────────
//  Painting
// ─────────────────────────────────────────────────

void WaveformWidget::paintEvent(QPaintEvent *)
{
	if (!m_cache_valid)
		rebuildCache();

	QPainter p(this);

	// Draw the cached waveform.
	p.drawPixmap(0, 0, m_cache);

	int w = width();
	int h = height();

	// Draw span selection overlay.
	if (m_model->hasSpanSelection())
	{
		double x1 = timeToX(m_model->selectionStart());
		double x2 = timeToX(m_model->selectionEnd());

		// Only draw if overlapping with viewport.
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

void WaveformWidget::resizeEvent(QResizeEvent *)
{
	m_cache_valid = false;
}


// ─────────────────────────────────────────────────
//  Mouse interaction
// ─────────────────────────────────────────────────

void WaveformWidget::mousePressEvent(QMouseEvent *event)
{
	if (event->button() == Qt::LeftButton)
	{
		m_dragging = true;
		m_drag_start_time = xToTime(event->position().x());
		m_model->setSelection(m_drag_start_time, m_drag_start_time);
	}
}

void WaveformWidget::mouseMoveEvent(QMouseEvent *event)
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

void WaveformWidget::mouseReleaseEvent(QMouseEvent *event)
{
	if (event->button() == Qt::LeftButton && m_dragging)
	{
		m_dragging = false;
		// If the drag distance is tiny, treat it as a point selection (click).
		double t = xToTime(event->position().x());
		t = std::clamp(t, 0.0, m_model->duration());
		if (std::abs(t - m_drag_start_time) < 0.001)
			m_model->setSelection(m_drag_start_time, m_drag_start_time);
		else
			m_model->setSelection(m_drag_start_time, t);
	}
}

void WaveformWidget::wheelEvent(QWheelEvent *event)
{
	double t = xToTime(event->position().x());
	int delta = event->angleDelta().y();

	if (delta > 0)
		m_model->zoomIn(t);
	else if (delta < 0)
		m_model->zoomOut(t);
}

void WaveformWidget::leaveEvent(QEvent *)
{
	if (m_mouse_tracking_enabled)
		m_model->clearCursor();
}


// ─────────────────────────────────────────────────
//  Model signal handlers
// ─────────────────────────────────────────────────

void WaveformWidget::onViewportChanged(double, double)
{
	m_cache_valid = false;
	update();
}

void WaveformWidget::onSelectionChanged(double, double)
{
	update(); // Repaint overlays only (cache still valid).
}

void WaveformWidget::onSelectionCleared()
{
	update();
}

void WaveformWidget::onCursorChanged(double)
{
	update();
}

void WaveformWidget::onCursorCleared()
{
	update();
}

void WaveformWidget::onPlaybackChanged(double)
{
	update();
}

void WaveformWidget::onPlaybackCleared()
{
	update();
}

} // namespace phonometrica
