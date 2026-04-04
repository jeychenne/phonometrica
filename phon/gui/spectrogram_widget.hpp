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
 * Purpose: Widget to display a spectrogram for a single channel (or channel average) of a sound file.                 *
 *          Observes TimeModel for viewport, selection, cursor and playback state.                                     *
 *                                                                                                                     *
 ***********************************************************************************************************************/

#ifndef PHONOMETRICA_SPECTROGRAM_WIDGET_HPP
#define PHONOMETRICA_SPECTROGRAM_WIDGET_HPP

#include <vector>
#include <complex>
#include <QWidget>
#include <QImage>
#include <QTimer>
#include <phon/application/sound.hpp>
#include <phon/gui/time_model.hpp>
#include <phon/analysis/signal_processing.hpp>
#include <phon/utils/matrix.hpp>

namespace phonometrica {

class SpectrogramWidget : public QWidget
{
	Q_OBJECT

public:

	// channel: 1..nchannel for a specific channel, 0 for average of all channels.
	SpectrogramWidget(TimeModel *model, const Handle<Sound> &sound, int channel, QWidget *parent = nullptr);

	void setMouseTracking(bool enabled);

	// When true, this widget draws the cursor time label.
	void setTopPlot(bool top);

	double maxFrequency() const { return m_max_freq; }

	void readSettings();

	// Formant overlay.
	void setShowFormants(bool show);
	bool showFormants() const { return m_show_formants; }
	void readFormantSettings();

protected:

	void paintEvent(QPaintEvent *event) override;
	void resizeEvent(QResizeEvent *event) override;
	void showEvent(QShowEvent *event) override;
	void mousePressEvent(QMouseEvent *event) override;
	void mouseMoveEvent(QMouseEvent *event) override;
	void mouseReleaseEvent(QMouseEvent *event) override;
	void wheelEvent(QWheelEvent *event) override;
	void leaveEvent(QEvent *event) override;

signals:

	void yValueDescription(const QString &text);

	void anchorRequested(double time);

private slots:

	void onViewportChanged(double start, double end);
	void onSelectionChanged(double t1, double t2);
	void onSelectionCleared();
	void onCursorChanged(double time);
	void onCursorCleared();
	void onPlaybackChanged(double time);
	void onPlaybackCleared();

	// Called when the debounce timer fires after a viewport change.
	void onDebounceFired();

private:

	// Maximum number of FFT columns to compute, regardless of widget width.
	// Spectrograms are inherently blurry — extra columns beyond this are wasted.
	static constexpr int MAX_SPECTROGRAM_COLUMNS = 1200;

	void rebuildCache();

	// Compute the spectrogram raster: a (w x h) matrix of dB values.
	// w and h are in logical pixels (w is capped by MAX_SPECTROGRAM_COLUMNS).
	// The time range is given explicitly so the method can be called for a partial
	// strip during incremental pan. out_min_dB/out_max_dB are set to the raster's
	// dB extremes, avoiding a separate scan pass.
	Matrix<double> computeSpectrogram(int w, int h, double start_time, double duration,
	                                  double &out_min_dB, double &out_max_dB);

	// Attempt to reuse most of the cached image when the viewport has panned
	// without changing zoom. Returns true if the cache was updated incrementally.
	bool tryIncrementalPan();

	// Build a QImage from a raster, scale it to (target_w × target_h), and return it.
	static QImage rasterToImage(const Matrix<double> &raster,
	                            double min_dB, double max_dB, int dynamic_range,
	                            int target_w, int target_h);

	// Formant estimation for the current viewport.
	void rebuildFormantCache();
	void drawFormants(QPainter &p);
	double hertzToY(double hz) const;

	// Map between time/frequency and pixel coordinates.
	double timeToX(double t) const;
	double xToTime(double x) const;
	double yPosToHertz(int y) const;

	TimeModel *m_model;
	Handle<Sound> m_sound;
	int m_channel; // 0 = average, 1..N = specific channel

	// Cached image of the spectrogram (no overlays).
	QImage m_cache;
	bool m_cache_valid = false;

	// Viewport and dimensions that the current cache represents.
	// Used to detect pan operations for incremental updates.
	double m_cache_start = -1;
	double m_cache_end = -1;
	int m_cache_lw = 0;
	int m_cache_lh = 0;
	double m_cache_min_dB = 0;
	double m_cache_max_dB = 0;

	// Debounce timer: after an incremental pan or a leading-edge recompute,
	// the timer fires to do a clean full recompute (fixing normalization seams)
	// and to rebuild the formant overlay.
	QTimer *m_debounce_timer = nullptr;

	// Spectrogram settings.
	double m_window_length;           // Duration of the analysis window (seconds).
	double m_max_freq;                // Highest frequency to display (Hz).
	double m_preemph_threshold;       // Pre-emphasis threshold (Hz).
	int m_dynamic_range;              // Dynamic range in dB.
	speech::WindowType m_window_type; // Window function.

	// Cached analysis window — regenerated only when settings change.
	Array<double> m_cached_window;
	int m_cached_nframe = 0;
	int m_cached_nfft = 0;
	double m_cached_weight = 0; // sum of squared window values

	// Reusable audio buffer — avoids a heap allocation per recompute.
	std::vector<double> m_audio_buffer;

	// Formant overlay.
	bool m_show_formants = false;
	int m_nformant = 4;
	double m_formant_max_freq = 5500;
	double m_formant_window_length = 0.025;
	double m_formant_time_step = 0.01;
	int m_lpc_order = 10;

	// Cached formant data: m_formant_freqs[i][j] = frequency of formant j at time i.
	std::vector<double> m_formant_times;
	std::vector<std::vector<double>> m_formant_freqs;
	bool m_formants_valid = false;

	// Dragging state for selection.
	bool m_dragging = false;
	double m_drag_start_time = -1;

	bool m_mouse_tracking_enabled = false;
	bool m_is_top = false;
};

} // namespace phonometrica

#endif // PHONOMETRICA_SPECTROGRAM_WIDGET_HPP
