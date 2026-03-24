/***********************************************************************************************************************
 *                                                                                                                     *
 * Copyright (C) 2019-2026 Julien Eychenne                                                                             *
 *                                                                                                                     *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0. If a copy of the MPL was not   *
 * distributed with this file, You can obtain one at https://mozilla.org/MPL/2.0/.                                     *
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

protected:

	void paintEvent(QPaintEvent *event) override;
	void resizeEvent(QResizeEvent *event) override;
	void mousePressEvent(QMouseEvent *event) override;
	void mouseMoveEvent(QMouseEvent *event) override;
	void mouseReleaseEvent(QMouseEvent *event) override;
	void wheelEvent(QWheelEvent *event) override;
	void leaveEvent(QEvent *event) override;

private slots:

	void onViewportChanged(double start, double end);
	void onSelectionChanged(double t1, double t2);
	void onSelectionCleared();
	void onCursorChanged(double time);
	void onCursorCleared();
	void onPlaybackChanged(double time);
	void onPlaybackCleared();

private:

	void rebuildCache();

	// Compute the spectrogram raster: a (w x h) matrix of dB values.
	// w and h are in physical pixels (accounting for device pixel ratio).
	Matrix<double> computeSpectrogram(int w, int h);

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

	// Spectrogram settings.
	double m_window_length;           // Duration of the analysis window (seconds).
	double m_max_freq;                // Highest frequency to display (Hz).
	double m_preemph_threshold;       // Pre-emphasis threshold (Hz).
	int m_dynamic_range;              // Dynamic range in dB.
	speech::WindowType m_window_type; // Window function.

	// Dragging state for selection.
	bool m_dragging = false;
	double m_drag_start_time = -1;

	bool m_mouse_tracking_enabled = false;
	bool m_is_top = false;
};

} // namespace phonometrica

#endif // PHONOMETRICA_SPECTROGRAM_WIDGET_HPP
