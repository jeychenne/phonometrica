/***********************************************************************************************************************
 *                                                                                                                     *
 * Copyright (C) 2019-2026 Julien Eychenne                                                                             *
 *                                                                                                                     *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0. If a copy of the MPL was not   *
 * distributed with this file, You can obtain one at https://mozilla.org/MPL/2.0/.                                     *
 *                                                                                                                     *
 * Created: 22/03/2026                                                                                                 *
 *                                                                                                                     *
 * Purpose: Widget to display the waveform for a single channel (or channel average) of a sound file.                 *
 *          Observes TimeModel for viewport, selection, cursor and playback state.                                     *
 *                                                                                                                     *
 ***********************************************************************************************************************/

#ifndef PHONOMETRICA_WAVEFORM_WIDGET_HPP
#define PHONOMETRICA_WAVEFORM_WIDGET_HPP

#include <vector>
#include <QWidget>
#include <QPixmap>
#include <phon/application/sound.hpp>
#include <phon/gui/time_model.hpp>

namespace phonometrica {

// Waveform amplitude scaling mode.
enum class Scaling
{
	Local,  // Fit to visible window peak (Praat default).
	Global, // Fit to file peak.
	Fixed   // Always -1 to +1.
};

class WaveformWidget : public QWidget
{
	Q_OBJECT

public:

	// channel: 1..nchannel for a specific channel, 0 for average of all channels.
	WaveformWidget(TimeModel *model, const Handle<Sound> &sound, int channel, QWidget *parent = nullptr);

	void setGlobalMagnitude(double value);
	double magnitude() const { return m_magnitude; }

	void setScaling(Scaling mode);

	void setMouseTracking(bool enabled);

	// When true, this widget draws the cursor time label.
	void setTopPlot(bool top);

protected:

	void paintEvent(QPaintEvent *event) override;
	void resizeEvent(QResizeEvent *event) override;
	void mousePressEvent(QMouseEvent *event) override;
	void mouseMoveEvent(QMouseEvent *event) override;
	void mouseReleaseEvent(QMouseEvent *event) override;
	void wheelEvent(QWheelEvent *event) override;
	void leaveEvent(QEvent *event) override;

signals:

	// Emitted when the mouse moves over the widget; text describes the Y value
	// (e.g. "Amplitude ≈ 0.42"). Empty string when the mouse leaves.
	void yValueDescription(const QString &text);

	// Emitted on Ctrl+Click to request anchor creation at the given time.
	void anchorRequested(double time);

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
	std::vector<double> computeWaveform();
	double sampleToY(double sample) const;
	double yToAmplitude(int y) const;
	double timeToX(double t) const;
	double xToTime(double x) const;
	void drawWaveformToPixmap();

	TimeModel *m_model;
	Handle<Sound> m_sound;
	int m_channel; // 0 = average, 1..N = specific channel

	// Cached pixmap of just the waveform (no selection/cursor overlays).
	QPixmap m_cache;
	bool m_cache_valid = false;

	double m_magnitude = 1.0;
	double m_global_magnitude = 1.0;
	Scaling m_scaling = Scaling::Local;

	// Dragging state for selection.
	bool m_dragging = false;
	double m_drag_start_time = -1;

	bool m_mouse_tracking_enabled = false;
	bool m_is_top = false;
};

} // namespace phonometrica

#endif // PHONOMETRICA_WAVEFORM_WIDGET_HPP
