/***********************************************************************************************************************
 *                                                                                                                     *
 * Copyright (C) 2019-2026 Julien Eychenne                                                                             *
 *                                                                                                                     *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0. If a copy of the MPL was not   *
 * distributed with this file, You can obtain one at https://mozilla.org/MPL/2.0/.                                     *
 *                                                                                                                     *
 * Created: 22/03/2026                                                                                                 *
 *                                                                                                                     *
 * Purpose: Shared model for time-aligned state in sound and annotation views. Holds viewport, selection, cursor and   *
 *          playback position. All time-aligned widgets observe this single source of truth rather than signaling       *
 *          each other directly.                                                                                        *
 *                                                                                                                     *
 ***********************************************************************************************************************/

#ifndef PHONOMETRICA_TIME_MODEL_HPP
#define PHONOMETRICA_TIME_MODEL_HPP

#include <QObject>

namespace phonometrica {

class TimeModel : public QObject
{
	Q_OBJECT

public:

	explicit TimeModel(double duration, QObject *parent = nullptr);

	// ── Read-only properties ─────────────────────

	double duration() const { return m_duration; }

	// ── Viewport ─────────────────────────────────

	double windowStart() const { return m_win_start; }
	double windowEnd() const { return m_win_end; }
	double windowDuration() const { return m_win_end - m_win_start; }

	void setViewport(double start, double end);

	void zoomIn(double center = -1);
	void zoomOut(double center = -1);
	void zoomToSelection();
	void viewAll();

	void moveForward(double fraction = 0.25);
	void moveBackward(double fraction = 0.25);

	// ── Selection ────────────────────────────────

	double selectionStart() const { return m_sel_start; }
	double selectionEnd() const { return m_sel_end; }
	bool hasSelection() const { return m_sel_start >= 0; }
	bool hasSpanSelection() const { return hasSelection() && m_sel_start != m_sel_end; }
	bool hasPointSelection() const { return hasSelection() && m_sel_start == m_sel_end; }

	void setSelection(double t1, double t2);
	void clearSelection();

	// ── Mouse tracking cursor ────────────────────

	double cursorTime() const { return m_cursor; }
	bool hasCursor() const { return m_cursor >= 0; }

	void setCursor(double time);
	void clearCursor();

	// ── Playback ─────────────────────────────────

	double playbackTime() const { return m_playback; }
	bool isPlaying() const { return m_playback >= 0; }

	void setPlaybackTime(double time);
	void clearPlayback();

signals:

	void viewportChanged(double start, double end);
	void selectionChanged(double t1, double t2);
	void selectionCleared();
	void cursorChanged(double time);
	void cursorCleared();
	void playbackTimeChanged(double time);
	void playbackCleared();

private:

	double m_duration;

	double m_win_start = 0;
	double m_win_end = 0;

	double m_sel_start = -1;
	double m_sel_end = -1;

	double m_cursor = -1;

	double m_playback = -1;

	// Minimum window duration to prevent zooming past the sample level.
	static constexpr double MIN_WINDOW = 0.001; // 1 ms
};

} // namespace phonometrica

#endif // PHONOMETRICA_TIME_MODEL_HPP
