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
 * Created: 22/03/2026                                                                                                 *
 *                                                                                                                     *
 * Purpose: see header.                                                                                                *
 *                                                                                                                     *
 ***********************************************************************************************************************/

#include <QPainter>
#include <QPainterPath>
#include <QMouseEvent>
#include <QWheelEvent>
#include <QTimer>
#include <cmath>
#include <algorithm>
#include <phon/gui/waveform_widget.hpp>
#include <phon/analysis/voice_quality.hpp>

namespace phonometrica {

// Colours matching the wx version.
static const QColor ZERO_LINE_COLOR(0, 0, 255);           // blue dotted
static const QColor WAVEFORM_COLOR(0, 0, 0);              // black
static const QColor SELECTION_COLOR(209, 116, 23, 50);    // semi-transparent orange
static const QColor POINT_SEL_COLOR(199, 179, 0);         // gold cursor
static const QColor PLAYBACK_COLOR(255, 0, 0);            // red tick
static const QColor PULSE_COLOR(0, 0, 200, 140);          // semi-transparent blue, Praat-like

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

	// Debounce for pulse recompute on viewport changes. 120 ms matches
	// PitchWidget / IntensityWidget so the UX feels consistent across
	// overlays. Pulses are recomputed leading-edge (on the first viewport
	// change after stillness) and again trailing-edge (once changes stop).
	m_pulse_debounce_timer = new QTimer(this);
	m_pulse_debounce_timer->setSingleShot(true);
	m_pulse_debounce_timer->setInterval(120);
	connect(m_pulse_debounce_timer, &QTimer::timeout, this, [this]() {
		if (!m_show_pulses) return;
		m_pulses_valid = false;
		update();
	});
}

void WaveformWidget::setGlobalMagnitude(double value)
{
	m_global_magnitude = value;
	// Only update the active magnitude if we're in global mode.
	if (m_scaling == Scaling::Global)
	{
		m_magnitude = value;
		m_cache_valid = false;
		update();
	}
}

void WaveformWidget::setScaling(Scaling mode)
{
	if (m_scaling == mode)
		return;
	m_scaling = mode;
	if (mode == Scaling::Fixed)
		m_magnitude = 1.0;
	else if (mode == Scaling::Global)
		m_magnitude = m_global_magnitude;
	// Local magnitude is recomputed on every draw.
	m_cache_valid = false;
	update();
}

void WaveformWidget::setMouseTracking(bool enabled)
{
	m_mouse_tracking_enabled = enabled;
}

void WaveformWidget::setTopPlot(bool top)
{
	if (m_is_top != top)
	{
		m_is_top = top;
		update();
	}
}

void WaveformWidget::setShowGlottalPulses(bool show)
{
	if (m_show_pulses == show)
		return;
	m_show_pulses = show;
	// Force a fresh compute when turning on, in case the cache is from
	// a different viewport (or never computed). Turning off clears the
	// flag; we don't bother invalidating the cache because it'll be
	// invalidated next time the user toggles on or scrolls.
	if (show)
		m_pulses_valid = false;
	update();
}

void WaveformWidget::setGlottalPulseParams(double f0_min, double f0_max, bool do_highpass)
{
	if (m_pulse_f0_min == f0_min && m_pulse_f0_max == f0_max && m_pulse_highpass == do_highpass)
		return;
	m_pulse_f0_min   = f0_min;
	m_pulse_f0_max   = f0_max;
	m_pulse_highpass = do_highpass;
	m_pulses_valid = false;
	if (m_show_pulses)
		update();
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

double WaveformWidget::yToAmplitude(int y) const
{
	double H = height() / 2.0;
	return (H - y) * m_magnitude / H;
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

		// Update magnitude for local scaling mode.
		if (m_scaling == Scaling::Local)
		{
			double peak = 0;
			for (intptr_t x = 0; x < sample_count; x++)
			{
				double s = 0;
				for (int c = 0; c < nchannel; c++)
					s += channels[c][x];
				s /= nchannel;
				double a = std::abs(s);
				if (a > peak) peak = a;
			}
			m_magnitude = (peak > 0) ? peak : 1.0;
		}

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

		// Update magnitude for local scaling mode.
		if (m_scaling == Scaling::Local)
		{
			double peak = 0;
			for (intptr_t x = 0; x < sample_count; x++)
			{
				double a = std::abs(double(data[x]));
				if (a > peak) peak = a;
			}
			m_magnitude = (peak > 0) ? peak : 1.0;
		}

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
//  Glottal-pulse overlay
// ─────────────────────────────────────────────────

// Above this viewport width, individual pulses blur into a solid stripe
// at typical screen sizes (~150 pulses/sec of voicing → sub-pixel spacing).
// Skipping computation entirely also avoids the multi-second REAPER latency
// at very wide views. Tighten if pulses end up needed on longer spans.
static constexpr double PULSE_MAX_WINDOW_S = 30.0;

void WaveformWidget::computeGlottalPulses()
{
	m_pulses.clear();
	m_pulses_valid = true; // mark valid even on bail-out so we don't retry per-paint

	double t0 = m_model->windowStart();
	double t1 = m_model->windowEnd();
	double dur = t1 - t0;
	if (dur <= 0.0 || dur > PULSE_MAX_WINDOW_S) return;

	auto first_sample = m_sound->time_to_frame(t0);
	auto last_sample  = m_sound->time_to_frame(t1);
	if (last_sample - first_sample < 2) return;
	int nchannel = m_sound->nchannel();
	double sr = static_cast<double>(m_sound->sample_rate());
	if (sr <= 0.0) return;

	// Materialise window samples as doubles. REAPER's int16 conversion
	// happens inside the kernel; we just need contiguous doubles.
	intptr_t n = last_sample - first_sample;
	std::vector<double> mono(static_cast<size_t>(n), 0.0);

	if (m_channel == 0)
	{
		std::vector<std::span<const float>> channels;
		channels.reserve(static_cast<size_t>(nchannel));
		for (int c = 1; c <= nchannel; ++c)
			channels.push_back(m_sound->channel_view(c, first_sample, last_sample));

		const double inv_nc = (nchannel > 0) ? 1.0 / nchannel : 1.0;
		for (intptr_t x = 0; x < n; ++x)
		{
			double s = 0.0;
			for (int c = 0; c < nchannel; ++c)
				s += channels[c][x];
			mono[x] = s * inv_nc;
		}
	}
	else
	{
		auto data = m_sound->channel_view(m_channel, first_sample, last_sample);
		for (intptr_t x = 0; x < n; ++x)
			mono[x] = data[x];
	}

	std::vector<double> local_pulses;
	try {
		local_pulses = speech::compute_glottal_pulses(
		    std::span<const double>(mono.data(), mono.size()),
		    sr, m_pulse_f0_min, m_pulse_f0_max, m_pulse_highpass);
	}
	catch (const std::exception &) {
		// REAPER failed on this window (rare). Leave pulses empty.
		return;
	}

	// REAPER returns pulse times relative to the start of its input. Shift
	// to absolute file time so the draw routine can compare against the
	// time model directly.
	m_pulses.reserve(local_pulses.size());
	for (double t : local_pulses)
		m_pulses.push_back(t0 + t);
}

void WaveformWidget::drawGlottalPulses(QPainter &p)
{
	if (m_pulses.empty()) return;

	double t0 = m_model->windowStart();
	double t1 = m_model->windowEnd();
	int h = height();
	int w = width();

	p.setPen(QPen(PULSE_COLOR, 1));
	p.setRenderHint(QPainter::Antialiasing, false); // crisp 1-px verticals

	// Pulses are sorted by construction (REAPER yields chronological times).
	// The viewport check costs nothing relative to compute time and keeps
	// the routine correct if a stale cache straddles a viewport edge.
	for (double t : m_pulses)
	{
		if (t < t0) continue;
		if (t > t1) break;
		double x = timeToX(t);
		if (x >= 0 && x <= w)
			p.drawLine(QPointF(x, 0), QPointF(x, h));
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

	// Glottal-pulse overlay (between the wave and the cursor/selection overlays
	// so pulses are visible but the selection / cursor / playback still draw
	// on top of them).
	if (m_show_pulses)
	{
		if (!m_pulses_valid)
			computeGlottalPulses();
		drawGlottalPulses(p);
	}

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

				// Place label to the right of the cursor; flip to the left near the edge.
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
		if (event->modifiers() & Qt::ControlModifier)
		{
			double t = xToTime(event->position().x());
			t = std::clamp(t, 0.0, m_model->duration());
			m_model->setSelection(t, t);
			emit anchorRequested(t);
			return;
		}
		m_dragging = true;
		m_drag_start_time = xToTime(event->position().x());
		m_model->setSelection(m_drag_start_time, m_drag_start_time);
	}
	else if (event->button() == Qt::MiddleButton)
	{
		m_model->zoomToSelection();
	}
	else if (event->button() == Qt::RightButton)
	{
		m_model->clearSelection();
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
	else if (event->modifiers() & Qt::ControlModifier)
	{
		// Ctrl held: show a point selection so layers display a candidate anchor.
		m_model->setSelection(t, t);
	}
	else if (m_mouse_tracking_enabled)
	{
		m_model->setCursor(t);
	}

	double amp = yToAmplitude(int(event->position().y()));
	emit yValueDescription(tr("Amplitude \u2248 %1").arg(amp, 0, 'f', 3));
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
	int delta = event->angleDelta().y();
	if (delta == 0)
	{
		event->ignore();
		return;
	}

	if (event->modifiers() & Qt::ControlModifier)
	{
		// Ctrl+wheel: zoom toward the cursor's time position.
		double t = xToTime(event->position().x());
		if (delta > 0)
			m_model->zoomIn(t);
		else
			m_model->zoomOut(t);
	}
	else
	{
		// Plain wheel: pan horizontally (left/right in time).
		if (delta > 0)
			m_model->moveBackward();
		else
			m_model->moveForward();
	}
	event->accept();
}

void WaveformWidget::leaveEvent(QEvent *)
{
	if (m_mouse_tracking_enabled)
		m_model->clearCursor();
	emit yValueDescription({});
}


// ─────────────────────────────────────────────────
//  Model signal handlers
// ─────────────────────────────────────────────────

void WaveformWidget::onViewportChanged(double, double)
{
	m_cache_valid = false;

	// Pulse cache also follows the viewport. Use the same leading-edge
	// throttle pattern as PitchWidget: invalidate immediately on the
	// first change after stillness, coalesce rapid subsequent changes
	// (scrolling, zooming), and recompute once more 120 ms after the
	// last change so the final position is correct.
	if (m_show_pulses)
	{
		if (!m_pulse_debounce_timer->isActive())
			m_pulses_valid = false;
		m_pulse_debounce_timer->start();
	}
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
