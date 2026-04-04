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
 * Created: 23/03/2026                                                                                                 *
 *                                                                                                                     *
 * Purpose: see header.                                                                                                *
 *                                                                                                                     *
 ***********************************************************************************************************************/

#include <cmath>
#include <cstring>
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
		Settings::reset_formants();
		readSettings();
	}

	connect(m_model, &TimeModel::viewportChanged, this, &SpectrogramWidget::onViewportChanged);
	connect(m_model, &TimeModel::selectionChanged, this, &SpectrogramWidget::onSelectionChanged);
	connect(m_model, &TimeModel::selectionCleared, this, &SpectrogramWidget::onSelectionCleared);
	connect(m_model, &TimeModel::cursorChanged, this, &SpectrogramWidget::onCursorChanged);
	connect(m_model, &TimeModel::cursorCleared, this, &SpectrogramWidget::onCursorCleared);
	connect(m_model, &TimeModel::playbackTimeChanged, this, &SpectrogramWidget::onPlaybackChanged);
	connect(m_model, &TimeModel::playbackCleared, this, &SpectrogramWidget::onPlaybackCleared);

	// Debounce timer: coalesces rapid viewport changes (zoom, scroll, arrow keys)
	// so the expensive spectrogram FFT computation runs only once after a quiet period.
	m_debounce_timer = new QTimer(this);
	m_debounce_timer->setSingleShot(true);
	m_debounce_timer->setInterval(120); // ms
	connect(m_debounce_timer, &QTimer::timeout, this, &SpectrogramWidget::onDebounceFired);
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
	m_cache_start = -1; // settings changed — no incremental pan possible
	m_cached_nframe = 0; // force window rebuild on next computeSpectrogram()
	m_cached_nfft = 0;
	readFormantSettings();
}

void SpectrogramWidget::readFormantSettings()
{
	String category("formants");
	m_nformant = (int) Settings::get_number(category, "number_of_formants");
	m_formant_window_length = Settings::get_number(category, "window_size");
	m_lpc_order = (int) Settings::get_number(category, "lpc_order");
	m_formant_max_freq = Settings::get_number(category, "max_frequency");
	m_formant_time_step = Settings::get_number(category, "time_step");

	m_formants_valid = false;
}

void SpectrogramWidget::setShowFormants(bool show)
{
	m_show_formants = show;
	m_formants_valid = false;
	update();
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

double SpectrogramWidget::hertzToY(double hz) const
{
	auto h = double(height());
	return h - (hz * h / m_max_freq);
}


// ─────────────────────────────────────────────────
//  Spectrogram computation (in logical pixels)
// ─────────────────────────────────────────────────

// Local helper: in-place pre-emphasis on a raw double buffer (mirrors speech::pre_emphasis
// but avoids requiring an Array<double>).
static void apply_pre_emphasis(double *data, intptr_t n, double sample_rate, double threshold)
{
	if (n < 2 || threshold <= 0.0 || sample_rate <= 0.0) return;
	const double alpha = std::exp(-2.0 * M_PI * threshold / sample_rate);
	for (auto i = n - 1; i >= 1; i--) {
		data[i] -= alpha * data[i - 1];
	}
}

Matrix<double> SpectrogramWidget::computeSpectrogram(int w, int h, double window_start, double window_duration,
                                                    double &out_min_dB, double &out_max_dB)
{
	using namespace speech;

	out_min_dB = 1e9;
	out_max_dB = -1e9;

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

	// Rebuild the cached window function only when FFT parameters change
	// (i.e. after a settings change). This avoids recalculating all the
	// sin/exp values on every viewport scroll.
	if (nframe != m_cached_nframe || nfft != m_cached_nfft)
	{
		m_cached_window = create_window(nframe, nfft, m_window_type);
		m_cached_nframe = nframe;
		m_cached_nfft = nfft;

		m_cached_weight = 0;
		for (double x : m_cached_window) m_cached_weight += x * x;
	}

	double k1 = 1.0 / (sample_rate * m_cached_weight); // at DC and Nyquist frequencies.
	double k2 = 2.0 / (sample_rate * m_cached_weight); // at other frequencies.

	// Get audio data. We get a bit more data before and after the window so that we can calculate
	// frames at the edge.
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

	// Fill the reusable audio buffer from the sound's raw float data, avoiding
	// the allocation that get_channel() would perform on every call.
	auto sample_count = last_sample - first_sample + 1;
	m_audio_buffer.resize(sample_count);

	if (m_channel == 0)
	{
		// Average across channels.
		int nchan = m_sound->nchannel();
		std::fill(m_audio_buffer.begin(), m_audio_buffer.end(), 0.0);
		for (int c = 1; c <= nchan; c++)
		{
			auto view = m_sound->channel_view(c, first_sample, last_sample);
			for (intptr_t i = 0; i < sample_count; i++)
				m_audio_buffer[i] += view[i];
		}
		double inv_nchan = 1.0 / nchan;
		for (intptr_t i = 0; i < sample_count; i++)
			m_audio_buffer[i] *= inv_nchan;
	}
	else
	{
		// Single channel — copy float→double.
		auto view = m_sound->channel_view(m_channel, first_sample, last_sample);
		for (intptr_t i = 0; i < sample_count; i++)
			m_audio_buffer[i] = view[i];
	}

	apply_pre_emphasis(m_audio_buffer.data(), sample_count, sample_rate, m_preemph_threshold);

	std::vector<double> input(nfft, 0.0);
	std::vector<std::complex<double>> output(half_nfft + 1, std::complex<double>(0, 0));

	// pocketfft setup — constant across all frames.
	pocketfft::shape_t shape{static_cast<size_t>(nfft)};
	pocketfft::stride_t stride_in{sizeof(double)};
	pocketfft::stride_t stride_out{sizeof(std::complex<double>)};

	for (intptr_t x = 0; x < w; x++)
	{
		auto t = window_start + x * slice_duration + offset;
		auto from_sample = m_sound->time_to_frame(t) - first_sample;
		auto to_sample = from_sample + nframe;

		if (from_sample < 0 || to_sample >= sample_count)
		{
			// Can't compute FFT — outside the bounds. Mark as NaN (will render as white).
			for (int j = 0; j < h; j++) {
				raster(x, j) = std::nan("");
			}
			continue;
		}

		auto *src = m_audio_buffer.data() + from_sample;

		// Apply window and fill FFT input buffer.
		for (intptr_t j = 0; j < nframe; j++)
		{
			// m_cached_window is a 1-based Array; j+1 maps to raw position j.
			input[j] = src[j] * m_cached_window[j + 1];
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

		// Map amplitude bins to raster pixels with linear interpolation,
		// tracking min/max dB inline to avoid a separate scan pass.
		double ceiling_bin = m_max_freq * half_nfft / nyquist_frequency;
		for (intptr_t y = 1; y <= h; y++)
		{
			double freq = double(y * ceiling_bin) / (h - 1.0);
			int bin = int(freq);
			double amp;

			if (bin >= half_nfft)
			{
				amp = amplitude[half_nfft - 1];
			}
			else
			{
				// Linear interpolation between adjacent bins.
				double a1 = amplitude[bin];
				double a2 = amplitude[bin + 1];
				amp = a1 + (a2 - a1) * (freq - bin);
			}

			raster(x, y - 1) = amp;

			if (amp > out_max_dB) out_max_dB = amp;
			if (amp < out_min_dB) out_min_dB = amp;
		}
	}

	return raster;
}


// ─────────────────────────────────────────────────
//  Cache management
// ─────────────────────────────────────────────────

QImage SpectrogramWidget::rasterToImage(const Matrix<double> &raster,
                                        double min_dB, double max_dB, int dynamic_range,
                                        int target_w, int target_h)
{
	int rw = raster.rows();  // raster columns (time)
	int rh = raster.cols();  // raster rows (frequency)

	if (rw <= 0 || rh <= 0 || min_dB >= max_dB)
	{
		QImage img(target_w, target_h, QImage::Format_RGB32);
		img.fill(Qt::white);
		return img;
	}

	min_dB = (std::max)(min_dB, max_dB - dynamic_range);
	double inv_range = 255.0 / (max_dB - min_dB);

	// Build a small image at raster resolution (1:1 mapping, no upscale loop).
	QImage small(rw, rh, QImage::Format_RGB32);
	small.fill(Qt::white);

	for (int img_y = 0; img_y < rh; img_y++)
	{
		int freq_j = rh - 1 - img_y; // reverse: top = high freq
		auto *scanline = reinterpret_cast<QRgb *>(small.scanLine(img_y));

		for (int img_x = 0; img_x < rw; img_x++)
		{
			double value = (std::max)(raster(img_x, freq_j), min_dB);
			if (std::isnan(value)) value = min_dB;

			int g = 255 - int((value - min_dB) * inv_range + 0.5);
			if (g < 0) g = 0;
			if (g > 255) g = 255;

			scanline[img_x] = qRgb(g, g, g);
		}
	}

	// Let Qt handle the upscale to the target (physical) resolution.
	// FastTransformation = nearest-neighbour, which is appropriate for spectrograms.
	if (rw == target_w && rh == target_h)
		return small;

	return small.scaled(target_w, target_h, Qt::IgnoreAspectRatio, Qt::FastTransformation);
}

bool SpectrogramWidget::tryIncrementalPan()
{
	if (m_cache.isNull() || m_cache_start < 0)
		return false;

	double new_start = m_model->windowStart();
	double new_end = m_model->windowEnd();
	double new_dur = new_end - new_start;
	double old_dur = m_cache_end - m_cache_start;
	int lw = width();
	int lh = height();

	// Must be same zoom level and same widget size.
	if (std::abs(new_dur - old_dur) > 1e-9 || lw != m_cache_lw || lh != m_cache_lh)
		return false;

	double shift_time = new_start - m_cache_start;
	if (std::abs(shift_time) < 1e-9)
		return true; // no actual shift

	int shift_px = int(round(shift_time / new_dur * lw));
	if (shift_px == 0)
		return true; // sub-pixel shift — existing cache is close enough
	if (std::abs(shift_px) >= lw)
		return false; // full shift — fall back to full recompute

	const double dpr = devicePixelRatioF();
	int pw = m_cache.width();
	int ph = m_cache.height();
	int phys_shift = int(round(shift_px * dpr));

	if (std::abs(phys_shift) >= pw)
		return false;

	// Shift the existing image: copy the reusable portion.
	QImage shifted(pw, ph, QImage::Format_RGB32);
	shifted.setDevicePixelRatio(dpr);
	shifted.fill(Qt::white);

	if (phys_shift > 0)
	{
		// Window moved right → keep the right part of the old image on the left.
		int copy_width = pw - phys_shift;
		for (int y = 0; y < ph; y++)
		{
			auto *src = reinterpret_cast<const QRgb *>(m_cache.constScanLine(y));
			auto *dst = reinterpret_cast<QRgb *>(shifted.scanLine(y));
			memcpy(dst, src + phys_shift, copy_width * sizeof(QRgb));
		}
	}
	else
	{
		int abs_shift = -phys_shift;
		int copy_width = pw - abs_shift;
		for (int y = 0; y < ph; y++)
		{
			auto *src = reinterpret_cast<const QRgb *>(m_cache.constScanLine(y));
			auto *dst = reinterpret_cast<QRgb *>(shifted.scanLine(y));
			memcpy(dst + abs_shift, src, copy_width * sizeof(QRgb));
		}
	}

	m_cache = std::move(shifted);

	// Compute just the exposed strip.
	int strip_lw = std::abs(shift_px);
	int strip_phys_x;
	int strip_phys_w;
	double strip_start;

	if (shift_px > 0)
	{
		// New content on the right.
		strip_phys_x = pw - phys_shift;
		strip_phys_w = phys_shift;
		strip_start = new_start + double(lw - strip_lw) / lw * new_dur;
	}
	else
	{
		// New content on the left.
		strip_phys_x = 0;
		strip_phys_w = -phys_shift;
		strip_start = new_start;
	}

	// Cap strip columns to avoid excessive FFTs on very wide displays.
	int strip_cols = std::min(strip_lw, MAX_SPECTROGRAM_COLUMNS);
	double strip_dur = double(strip_lw) / lw * new_dur;
	double strip_min, strip_max;
	auto strip_raster = computeSpectrogram(strip_cols, lh, strip_start, strip_dur,
	                                       strip_min, strip_max);

	if (strip_raster.rows() == 0 || strip_raster.cols() == 0)
		return false;

	// Build a scaled strip image at physical pixel resolution.
	auto strip_img = rasterToImage(strip_raster, m_cache_min_dB, m_cache_max_dB,
	                               m_dynamic_range, strip_phys_w, ph);

	// Blit strip_img directly into m_cache using raw pixel copies.
	// We cannot use QPainter here because m_cache has its devicePixelRatio set,
	// which causes QPainter to interpret positions as logical coordinates
	// (multiplied by DPR), misplacing the strip on HiDPI displays.
	for (int y = 0; y < ph; y++)
	{
		auto *src = reinterpret_cast<const QRgb *>(strip_img.constScanLine(y));
		auto *dst = reinterpret_cast<QRgb *>(m_cache.scanLine(y));
		memcpy(dst + strip_phys_x, src, strip_phys_w * sizeof(QRgb));
	}

	// Update cached viewport.
	m_cache_start = new_start;
	m_cache_end = new_end;
	m_cache_valid = true;

	return true;
}

void SpectrogramWidget::rebuildCache()
{
	const double dpr = devicePixelRatioF();
	const int lw = width();              // logical pixel width
	const int lh = height();             // logical pixel height
	const int pw = int(lw * dpr);        // physical pixel width
	const int ph = int(lh * dpr);        // physical pixel height

	if (pw <= 0 || ph <= 0) {
		m_cache = QImage();
		m_cache_valid = true;
		return;
	}

	double win_start = m_model->windowStart();
	double win_dur = m_model->windowDuration();

	// Cap the number of FFT columns — spectrograms don't benefit from
	// more than ~1200 columns regardless of display width.
	int raster_w = std::min(lw, MAX_SPECTROGRAM_COLUMNS);
	double min_dB, max_dB;

	auto raster = computeSpectrogram(raster_w, lh, win_start, win_dur, min_dB, max_dB);

	if (raster.rows() == 0 || raster.cols() == 0) {
		m_cache = QImage(pw, ph, QImage::Format_RGB32);
		m_cache.setDevicePixelRatio(dpr);
		m_cache.fill(Qt::white);
		m_cache_valid = true;
		return;
	}

	// Build the image at raster resolution and let Qt scale to physical size.
	m_cache = rasterToImage(raster, min_dB, max_dB, m_dynamic_range, pw, ph);
	m_cache.setDevicePixelRatio(dpr);

	// Store the viewport and normalization for incremental pan.
	min_dB = (std::max)(min_dB, max_dB - m_dynamic_range);
	m_cache_start = win_start;
	m_cache_end = win_start + win_dur;
	m_cache_lw = lw;
	m_cache_lh = lh;
	m_cache_min_dB = min_dB;
	m_cache_max_dB = max_dB;
	m_cache_valid = true;
}


// ─────────────────────────────────────────────────
//  Formant estimation
// ─────────────────────────────────────────────────

void SpectrogramWidget::rebuildFormantCache()
{
	m_formant_times.clear();
	m_formant_freqs.clear();

	auto window_duration = m_model->windowDuration();
	int w = width();
	if (w <= 0)
		return;

	// Use the configured time step, but if the window is large relative to the
	// widget width, increase the step so we compute at most 1 estimate per pixel.
	// Formant computation is expensive; this keeps wide views responsive while
	// preserving full resolution when zoomed in.
	double step = m_formant_time_step;
	double max_step = window_duration / w; // 1 estimate per pixel
	if (step < max_step)
		step = max_step;

	// At least 2 measurements per window.
	if (window_duration <= 2 * step)
		return;

	try
	{
		auto npoint = int(ceil((window_duration - step) / step));
		if (npoint <= 0) return;

		// Mark valid only once we're committed to computing. If we returned
		// early above (e.g. widget too narrow), the cache stays invalid so
		// that a future resize will trigger a fresh attempt.
		m_formants_valid = true;

		double t = m_model->windowStart();

		for (int i = 0; i < npoint; i++)
		{
			t += step;
			m_formant_times.push_back(t);

			std::vector<double> point_freqs(m_nformant, std::nan(""));

			try
			{
				auto result = m_sound->get_formants(m_channel, t, m_nformant,
					m_formant_max_freq, m_formant_window_length, m_lpc_order);

				for (int j = 0; j < m_nformant; j++)
				{
					double f = result(j + 1, 1); // 1-based Array: row = formant, col 1 = frequency
					if (std::isfinite(f) && f > 50 && f < m_formant_max_freq - 50)
						point_freqs[j] = f;
				}
			}
			catch (...)
			{
				// Edge effect — leave NaN.
			}

			m_formant_freqs.push_back(std::move(point_freqs));
		}
	}
	catch (...)
	{
		m_formant_times.clear();
		m_formant_freqs.clear();
	}
}

void SpectrogramWidget::drawFormants(QPainter &p)
{
	if (m_formant_times.empty())
		return;

	p.setRenderHint(QPainter::Antialiasing);
	p.setPen(Qt::NoPen);
	p.setBrush(QColor(255, 0, 0)); // red dots

	const double radius = 1.5;

	for (size_t i = 0; i < m_formant_times.size(); i++)
	{
		double x = timeToX(m_formant_times[i]);

		for (int j = 0; j < m_nformant; j++)
		{
			double f = m_formant_freqs[i][j];
			if (std::isnan(f)) continue;
			if (f > m_max_freq) continue; // outside visible range

			double y = hertzToY(f);
			p.drawEllipse(QPointF(x, y), radius, radius);
		}
	}
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

	// Draw formant overlay.
	if (m_show_formants)
	{
		if (!m_formants_valid)
			rebuildFormantCache();
		drawFormants(p);
	}

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
	m_cache_start = -1; // dimensions changed — no incremental pan possible
	m_formants_valid = false;
}

void SpectrogramWidget::showEvent(QShowEvent *)
{
	// The viewport may have changed while we were hidden (onViewportChanged
	// is skipped for hidden widgets). Force a full rebuild on the next paint.
	m_cache_valid = false;
	m_cache_start = -1;
	m_formants_valid = false;
}


// ─────────────────────────────────────────────────
//  Mouse interaction
// ─────────────────────────────────────────────────

void SpectrogramWidget::mousePressEvent(QMouseEvent *event)
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

void SpectrogramWidget::mouseMoveEvent(QMouseEvent *event)
{
	double t = xToTime(event->position().x());
	t = std::clamp(t, 0.0, m_model->duration());

	if (m_dragging)
	{
		m_model->setSelection(m_drag_start_time, t);
	}
	else if (event->modifiers() & Qt::ControlModifier)
	{
		m_model->setSelection(t, t);
	}
	else if (m_mouse_tracking_enabled)
	{
		m_model->setCursor(t);
	}

	double hz = yPosToHertz(int(event->position().y()));
	emit yValueDescription(tr("Frequency \u2248 %1 Hz").arg(int(hz)));
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
	emit yValueDescription({});
}


// ─────────────────────────────────────────────────
//  Model signal handlers
// ─────────────────────────────────────────────────

void SpectrogramWidget::onViewportChanged(double, double)
{
	// Skip all work when the widget is hidden — no point computing FFTs
	// that will never be painted. showEvent() invalidates the cache when
	// the widget becomes visible again.
	if (isHidden())
		return;

	m_formants_valid = false;

	// Try the cheap incremental pan first (shift image + compute only the new strip).
	if (tryIncrementalPan())
	{
		// Success — cache is updated. Start the debounce timer for a clean
		// full recompute (to fix normalization seams) and formant rebuild.
		m_debounce_timer->start();
		update();
		return;
	}

	// Incremental pan failed (zoom change, large shift, first paint, etc.).
	// Always invalidate so paintEvent does a full rebuild — showing a stale
	// spectrogram from a different viewport is worse than a brief lag.
	m_cache_valid = false;
	m_debounce_timer->start();
	update();
}

void SpectrogramWidget::onDebounceFired()
{
	// Force a clean full recompute regardless of whether incremental pan was used.
	// This ensures pixel-perfect normalization and rebuilds formants.
	m_cache_valid = false;
	m_formants_valid = false;
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
