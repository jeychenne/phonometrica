/***********************************************************************************************************************
 *                                                                                                                     *
 * Copyright (C) 2019-2026 Julien Eychenne                                                                             *
 *                                                                                                                     *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0. If a copy of the MPL was not   *
 * distributed with this file, You can obtain one at https://mozilla.org/MPL/2.0/.                                     *
 *                                                                                                                     *
 * Created: 23/03/2026                                                                                                 *
 *                                                                                                                     *
 * Purpose: Widget to display an intensity track for a single channel (or channel average) of a sound file.            *
 *          Observes TimeModel for viewport, selection, cursor and playback state.                                     *
 *                                                                                                                     *
 ***********************************************************************************************************************/

#ifndef PHONOMETRICA_INTENSITY_WIDGET_HPP
#define PHONOMETRICA_INTENSITY_WIDGET_HPP

#include <vector>
#include <QWidget>
#include <QPixmap>
#include <phon/application/sound.hpp>
#include <phon/gui/time_model.hpp>

namespace phonometrica {

class IntensityWidget : public QWidget
{
	Q_OBJECT

public:

	// channel: 1..nchannel for a specific channel, 0 for average of all channels.
	IntensityWidget(TimeModel *model, const Handle<Sound> &sound, int channel, QWidget *parent = nullptr);

	void setMouseTracking(bool enabled);

	double minIntensity() const { return m_min_dB; }
	double maxIntensity() const { return m_max_dB; }

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

	// Compute intensity values for the current viewport.
	std::vector<double> computeIntensity();

	// Map between time/intensity and pixel coordinates.
	double timeToX(double t) const;
	double xToTime(double x) const;
	double intensityToY(double dB) const;
	double yToIntensity(int y) const;

	TimeModel *m_model;
	Handle<Sound> m_sound;
	int m_channel; // 0 = average, 1..N = specific channel

	// Cached pixmap of the intensity curve (no overlays).
	QPixmap m_cache;
	bool m_cache_valid = false;

	// Intensity settings.
	double m_min_dB = 50;
	double m_max_dB = 100;
	double m_time_step = 0.01;

	// Dragging state for selection.
	bool m_dragging = false;
	double m_drag_start_time = -1;

	bool m_mouse_tracking_enabled = false;
};

} // namespace phonometrica

#endif // PHONOMETRICA_INTENSITY_WIDGET_HPP
