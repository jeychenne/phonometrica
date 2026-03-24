/***********************************************************************************************************************
 *                                                                                                                     *
 * Copyright (C) 2019-2026 Julien Eychenne                                                                             *
 *                                                                                                                     *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0. If a copy of the MPL was not   *
 * distributed with this file, You can obtain one at https://mozilla.org/MPL/2.0/.                                     *
 *                                                                                                                     *
 * Created: 22/03/2026                                                                                                 *
 *                                                                                                                     *
 * Purpose: Scrollbar-like widget that displays the entire waveform of a sound file (averaged across channels) and     *
 *          lets the user select a portion to zoom into. The highlighted rectangle represents the current viewport.     *
 *                                                                                                                     *
 ***********************************************************************************************************************/

#ifndef PHONOMETRICA_WAVE_BAR_HPP
#define PHONOMETRICA_WAVE_BAR_HPP

#include <vector>
#include <QWidget>
#include <phon/application/sound.hpp>
#include <phon/gui/time_model.hpp>

namespace phonometrica {

class WaveBar : public QWidget
{
	Q_OBJECT

public:

	WaveBar(TimeModel *model, const Handle<Sound> &sound, QWidget *parent = nullptr);

	// The global peak magnitude (computed once at construction). Useful for setting
	// the waveform widgets' global magnitude.
	double globalMagnitude() const { return m_magnitude; }

	// Pixel x coordinates of the current viewport highlight (for SoundZoom).
	double viewportLeftX() const;
	double viewportRightX() const;

signals:

	// Emitted whenever the viewport highlight changes visually (including during drag).
	void viewportPixelsChanged(double x1, double x2);

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

private:

	void rebuildCache();
	double sampleToY(double s) const;
	double timeToX(double t) const;
	double xToTime(double x) const;

	TimeModel *m_model;
	Handle<Sound> m_sound;

	// Waveform cache: min/max pairs per pixel, recomputed on resize.
	std::vector<double> m_cache;
	int m_cached_width = -1;

	// Peak absolute amplitude across all channels (computed once).
	double m_magnitude = 0.0;

	// Drag state.
	enum class DragMode { None, NewSelection, SlideViewport, ResizeLeft, ResizeRight };

	DragMode m_drag_mode = DragMode::None;
	double m_drag_start_x = -1;

	// For sliding: the viewport boundaries at the moment the drag started.
	double m_slide_start_t1 = 0;
	double m_slide_start_t2 = 0;

	// Pending selection during drag (pixel coordinates). Only committed on release.
	double m_pending_x1 = -1;
	double m_pending_x2 = -1;

	// Hit-test helpers.
	static constexpr double EDGE_GRAB_PX = 5;
	enum class HitZone { Outside, LeftEdge, RightEdge, Inside };
	HitZone hitTest(double x) const;
};

} // namespace phonometrica

#endif // PHONOMETRICA_WAVE_BAR_HPP
