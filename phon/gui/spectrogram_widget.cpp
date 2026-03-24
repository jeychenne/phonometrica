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
#include <QMouseEvent>
#include <QWheelEvent>
#include <phon/third_party/pocketfft-cpp/pocketfft_hdronly.h>
#include <phon/gui/spectrogram_widget.hpp>
#include <phon/application/settings.hpp>

namespace phonometrica {

// Overlay colours — same as WaveformWidget.
static const QColor SELECTION_COLOR(209, 116, 23, 50);
static const QColor POINT_SEL_COLOR(199, 179, 0);
static const QColor PLAYBACK_COLOR(255, 0, 0);

SpectrogramWidget::SpectrogramWidget(TimeModel *model, const Handle<Sound> &sound, int channel, QWidget *parent) :
	QWidget(parent), m_model(model), m_sound(sound), m_channel(channel)
{
	setMinimumHeight(60);
	setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);

	// Enable mouse tracking for cursor updates.
	QWidget::setMouseTracking(true);

	try {
		readSettings();
	}
	catch (std::exception &) {
		Settings::reset_spectrogram();
		readSettings();
	}

	connect(m_model, &TimeModel::viewportChanged, this, &SpectrogramWidget::onViewportChanged);
	connect(m_model, &TimeModel::selectionChanged, this, &SpectrogramWidget::onSelectionChanged);
	connect(m_model, &TimeModel::selectionCleared, this, &SpectrogramWidget::onSelectionCleared);
	connect(m_model, &TimeModel::cursorChanged, this, &SpectrogramWidget::onCursorChanged);
	connect(m_model, &TimeModel::cursorCleared, this, &SpectrogramWidget::onCursorCleared);
	connect(m_model, &TimeModel::playbackTimeChanged, this, &SpectrogramWidget::onPlaybackChanged);
	connect(m_model, &TimeModel::playbackCleared, this, &SpectrogramWidget::onPlaybackCleared);
}

void SpectrogramWidget::readSettings()
{
	using namespace speech;

	String category("spectrogram");
	m_max_freq = Settings::get_number(category, "frequency_range");
	double nyquist = double(m_sound->sample_rate()) / 2;
	m_max_freq = (std::min)(m_max_freq, nyquist);
	m_window_length = Settings::get_number(category, "window_size");
	m_dynamic_range = (int) Settings::get_number(category, "dynamic_range");
	m_preemph_threshold = Settings::get_number(category, "preemphasis_threshold");

	String win = Settings::get_string(category, "window_type");
	if (win == "Bartlett")
		m_window_type = WindowType::Bartlett;
	else if (win == "Blackman")
		m_window_type = WindowType::Blackman;
	else if (win == "Gaussian")
		m_window_type = WindowType::Gaussian;
	else if (win == "Hamming")
		m_window_type = WindowType::Hamming;
	else if (win == "Hann")
		m_window_type = WindowType::Hann;
	else if (win == "Rectangular")
		m_window_type = WindowType::Rectangular;
	else
	{
		Settings::set_value(category, "window_type", "Gaussian");
		m_window_type = WindowType::Gaussian;
	}

	m_cache_valid = false;
}

void SpectrogramWidget::setMouseTracking(bool enabled)
{
	m_mouse_tracking_enabled = enabled;
}

void SpectrogramWidget::setTopPlot(bool top)
{
	if (m_is_top != top)
	{
		m_is_top = top;
		update();
	}
}


// ─────────────────────────────────────────────────
//  Coordinate mapping (in logical pixels)
// ─────────────────────────────────────────────────

double SpectrogramWidget::timeToX(double t) const
{
	auto start = m_model->windowStart();
	auto dur = m_model->windowDuration();
	if (dur <= 0) return 0;
	return (t - start) / dur * width();
}

double SpectrogramWidget::xToTime(double x) const
{
	auto start = m_model->windowStart();
	auto dur = m_model->windowDuration();
	return start + x / width() * dur;
}

double SpectrogramWidget::yPosToHertz(int y) const
{
	auto h = height();
	return (m_max_freq * (h - y)) / h;
}


// ─────────────────────────────────────────────────
//  Spectrogram computation (in physical pixels)
// ─────────────────────────────────────────────────

Matrix<double> SpectrogramWidget::computeSpectrogram(int w, int h)
{
	using namespace speech;

	auto sample_rate = m_sound->sample_rate();
	auto nyquist_frequency = double(sample_rate) / 2;
	auto analysis_window_duration = m_window_length;

	// Praat uses a Gaussian-like window which is twice as long as a regular window.
	if (m_window_type == WindowType::Gaussian) {
		analysis_window_duration *= 2;
	}

	auto nframe = int(ceil(sample_rate * analysis_window_duration));
	if (nframe % 2 == 1) {
		nframe++;
	}

	int nfft = 256;
	while (nfft < nframe) nfft *= 2;
	auto half_nfft = nfft / 2;

	std::vector<double> amplitude(half_nfft, 0.0);

	if (w <= 0 || h <= 0) {
		return Matrix<double>(0, 0);
	}

	// Get audio data. We get a bit more data before and after the window so that we can calculate
	// frames at the edge.
	auto window_duration = m_model->windowDuration();
	auto window_start = m_model->windowStart();
	auto slice_duration = window_duration / w;
	auto offset = (slice_duration - analysis_window_duration) / 2;
	auto first_sample = m_sound->time_to_frame(window_start + offset);
	auto last_sample = m_sound->time_to_frame(window_start + (w - 1) * slice_duration + offset) + nframe + 1;

	if (first_sample < 1) {
		first_sample = 1;
	}
	auto total_sample_count = m_sound->channel_size();
	if (last_sample > total_sample_count) {
		last_sample = total_sample_count;
	}

	// An (w x h) matrix: rows = horizontal pixels (time), cols = vertical pixels (frequency).
	Matrix<double> raster(w, h);
	raster.setZero(w, h);

	auto win = create_window(nframe, nfft, m_window_type);

	// Weight power.
	double weight = 0;
	for (double x : win) weight += x * x;
	double k1 = 1.0 / (sample_rate * weight); // at DC and Nyquist frequencies.
	double k2 = 2.0 / (sample_rate * weight); // at other frequencies.

	std::vector<double> input(nfft, 0.0);
	std::vector<std::complex<double>> output(half_nfft + 1, std::complex<double>(0, 0));

	// pocketfft setup — constant across all frames.
	pocketfft::shape_t shape{static_cast<size_t>(nfft)};
	pocketfft::stride_t stride_in{sizeof(double)};
	pocketfft::stride_t stride_out{sizeof(std::complex<double>)};

	auto data = m_sound->get_channel(m_channel, first_sample, last_sample);
	speech::pre_emphasis(data, sample_rate, m_preemph_threshold);

	for (intptr_t x = 0; x < w; x++)
	{
		auto t = window_start + x * slice_duration + offset;
		auto from_sample = m_sound->time_to_frame(t) - first_sample;
		auto to_sample = from_sample + nframe;
		auto it = data.begin() + from_sample;

		if (from_sample < 0 || to_sample >= data.size())
		{
			// Can't compute FFT — outside the bounds. Mark as NaN (will render as white).
			for (int j = 0; j < h; j++) {
				raster(x, j) = std::nan("");
			}
			continue;
		}

		// Apply window and fill FFT input buffer.
		for (intptr_t j = 0; j < nframe; j++)
		{
			// win is a 1-based Array; j+1 maps to raw position j.
			input[j] = (*it++) * win[j + 1];
		}
		for (intptr_t j = nframe; j < nfft; j++) {
			input[j] = 0;
		}

		// Real-to-complex FFT via pocketfft.
		pocketfft::r2c(shape, stride_in, stride_out, {0}, true,
		               input.data(), output.data(), 1.0);

		// Convert to power spectral density in dB.
		for (int y = 0; y < half_nfft; y++)
		{
			double re = output[y].real();
			double im = output[y].imag();
			double a = re * re + im * im;
			double k = (y == 0 || y == half_nfft - 1) ? k1 : k2;
			a = k * a / nfft;
			constexpr double Iref = 4.0e-10;
			double dB = 10.0 * log10(a / Iref);
			amplitude[y] = dB;
		}

		// Map amplitude bins to raster pixels with linear interpolation.
		double ceiling_bin = m_max_freq * half_nfft / nyquist_frequency;
		for (intptr_t y = 1; y <= h; y++)
		{
			double freq = double(y * ceiling_bin) / (h - 1.0);
			int bin = int(freq);

			if (bin >= half_nfft)
			{
				raster(x, y - 1) = amplitude[half_nfft - 1];
			}
			else
			{
				// Linear interpolation between adjacent bins.
				double a1 = amplitude[bin];
				double a2 = amplitude[bin + 1];
				double delta = a2 - a1;
				double remainder = freq - bin;
				double amp = a1 + (delta * remainder);
				raster(x, y - 1) = amp;
			}
		}
	}

	return raster;
}


// ─────────────────────────────────────────────────
//  Cache management
// ─────────────────────────────────────────────────

void SpectrogramWidget::rebuildCache()
{
	const double dpr = devicePixelRatioF();
	const int pw = int(width() * dpr);   // physical pixel width
	const int ph = int(height() * dpr);  // physical pixel height

	if (pw <= 0 || ph <= 0) {
		m_cache = QImage();
		m_cache_valid = true;
		return;
	}

	auto raster = computeSpectrogram(pw, ph);

	m_cache = QImage(pw, ph, QImage::Format_RGB32);
	m_cache.setDevicePixelRatio(dpr);
	m_cache.fill(Qt::white);

	if (raster.rows() == 0 || raster.cols() == 0) {
		m_cache_valid = true;
		return;
	}

	// Find min and max dB in the raster, ignoring NaN.
	double max_dB = -1e9;
	double min_dB = 1e9;

	for (int i = 0; i < raster.rows(); i++) {
		for (int j = 0; j < raster.cols(); j++) {
			double val = raster(i, j);
			if (std::isfinite(val)) {
				if (val > max_dB) max_dB = val;
				if (val < min_dB) min_dB = val;
			}
		}
	}

	// Min and max can only be equal if we have zeros, in which case we don't fill the image.
	if (min_dB != max_dB)
	{
		// Adjust minimum to fit the dynamic range.
		min_dB = (std::max)(min_dB, max_dB - m_dynamic_range);

		// The image must be filled with frequency reversed: high frequencies at the top.
		// raster columns go 0 (low freq) to ph-1 (high freq).
		// Image rows go 0 (top = high freq) to ph-1 (bottom = low freq).
		for (int img_y = 0; img_y < ph; img_y++)
		{
			int freq_j = ph - 1 - img_y; // reverse: top = high freq
			auto *scanline = reinterpret_cast<QRgb *>(m_cache.scanLine(img_y));

			for (int img_x = 0; img_x < pw; img_x++)
			{
				double value = (std::max)(raster(img_x, freq_j), min_dB);
				if (std::isnan(value)) value = min_dB;

				int g = 255 - int(round((value - min_dB) * 255.0 / (max_dB - min_dB)));
				if (g < 0) g = 0;
				if (g > 255) g = 255;

				scanline[img_x] = qRgb(g, g, g);
			}
		}
	}

	m_cache_valid = true;
}


// ─────────────────────────────────────────────────
//  Painting
// ─────────────────────────────────────────────────

void SpectrogramWidget::paintEvent(QPaintEvent *)
{
	if (!m_cache_valid)
		rebuildCache();

	QPainter p(this);

	// Draw the cached spectrogram image.
	// Because m_cache has its devicePixelRatio set, Qt handles the
	// physical-to-logical mapping automatically.
	if (!m_cache.isNull())
		p.drawImage(0, 0, m_cache);

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

void SpectrogramWidget::resizeEvent(QResizeEvent *)
{
	m_cache_valid = false;
}


// ─────────────────────────────────────────────────
//  Mouse interaction
// ─────────────────────────────────────────────────

void SpectrogramWidget::mousePressEvent(QMouseEvent *event)
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

void SpectrogramWidget::mouseMoveEvent(QMouseEvent *event)
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

void SpectrogramWidget::mouseReleaseEvent(QMouseEvent *event)
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

void SpectrogramWidget::wheelEvent(QWheelEvent *event)
{
	double t = xToTime(event->position().x());
	int delta = event->angleDelta().y();

	if (delta > 0)
		m_model->zoomIn(t);
	else if (delta < 0)
		m_model->zoomOut(t);
}

void SpectrogramWidget::leaveEvent(QEvent *)
{
	if (m_mouse_tracking_enabled)
		m_model->clearCursor();
}


// ─────────────────────────────────────────────────
//  Model signal handlers
// ─────────────────────────────────────────────────

void SpectrogramWidget::onViewportChanged(double, double)
{
	m_cache_valid = false;
	update();
}

void SpectrogramWidget::onSelectionChanged(double, double)
{
	update(); // Repaint overlays only.
}

void SpectrogramWidget::onSelectionCleared()
{
	update();
}

void SpectrogramWidget::onCursorChanged(double)
{
	update();
}

void SpectrogramWidget::onCursorCleared()
{
	update();
}

void SpectrogramWidget::onPlaybackChanged(double)
{
	update();
}

void SpectrogramWidget::onPlaybackCleared()
{
	update();
}

} // namespace phonometrica
