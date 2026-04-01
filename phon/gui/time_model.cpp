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

#include <algorithm>
#include <cmath>
#include <phon/gui/time_model.hpp>

namespace phonometrica {

TimeModel::TimeModel(double duration, QObject *parent) :
	QObject(parent), m_duration(duration)
{
	// Start with an invalid viewport so the first setViewport() always emits the signal.
	m_win_start = -1;
	m_win_end = -1;
}


// ─────────────────────────────────────────────────
//  Viewport
// ─────────────────────────────────────────────────

void TimeModel::setViewport(double start, double end)
{
	start = std::clamp(start, 0.0, m_duration);
	end = std::clamp(end, 0.0, m_duration);

	if (end - start < MIN_WINDOW)
		return;

	if (start == m_win_start && end == m_win_end)
		return;

	m_win_start = start;
	m_win_end = end;
	emit viewportChanged(m_win_start, m_win_end);
}

void TimeModel::zoomIn(double center)
{
	if (center < 0)
		center = (m_win_start + m_win_end) / 2;

	auto half = windowDuration() / 4; // zoom to 50% of current
	setViewport(center - half, center + half);
}

void TimeModel::zoomOut(double center)
{
	if (center < 0)
		center = (m_win_start + m_win_end) / 2;

	auto half = windowDuration(); // expand to 200% of current
	setViewport(center - half, center + half);
}

void TimeModel::zoomToSelection()
{
	if (!hasSpanSelection())
		return;

	// Add a small margin (2%) around the selection.
	auto margin = (m_sel_end - m_sel_start) * 0.02;
	setViewport(m_sel_start - margin, m_sel_end + margin);
}

void TimeModel::viewAll()
{
	setViewport(0, m_duration);
}

void TimeModel::moveForward(double fraction)
{
	auto delta = windowDuration() * fraction;
	auto start = m_win_start + delta;
	auto end = m_win_end + delta;

	// Clamp to end of file.
	if (end > m_duration)
	{
		end = m_duration;
		start = end - windowDuration();
	}

	setViewport(start, end);
}

void TimeModel::moveBackward(double fraction)
{
	auto delta = windowDuration() * fraction;
	auto start = m_win_start - delta;
	auto end = m_win_end - delta;

	// Clamp to start of file.
	if (start < 0)
	{
		start = 0;
		end = start + windowDuration();
	}

	setViewport(start, end);
}


// ─────────────────────────────────────────────────
//  Selection
// ─────────────────────────────────────────────────

void TimeModel::setSelection(double t1, double t2)
{
	// Ensure t1 <= t2.
	if (t1 > t2)
		std::swap(t1, t2);

	t1 = std::clamp(t1, 0.0, m_duration);
	t2 = std::clamp(t2, 0.0, m_duration);

	m_sel_start = t1;
	m_sel_end = t2;
	emit selectionChanged(m_sel_start, m_sel_end);
}

void TimeModel::clearSelection()
{
	if (!hasSelection())
		return;

	m_sel_start = -1;
	m_sel_end = -1;
	emit selectionCleared();
}


// ─────────────────────────────────────────────────
//  Cursor
// ─────────────────────────────────────────────────

void TimeModel::setCursor(double time)
{
	m_cursor = std::clamp(time, 0.0, m_duration);
	emit cursorChanged(m_cursor);
}

void TimeModel::clearCursor()
{
	if (!hasCursor())
		return;

	m_cursor = -1;
	emit cursorCleared();
}


// ─────────────────────────────────────────────────
//  Playback
// ─────────────────────────────────────────────────

void TimeModel::setPlaybackTime(double time)
{
	m_playback = std::clamp(time, 0.0, m_duration);
	emit playbackTimeChanged(m_playback);
}

void TimeModel::clearPlayback()
{
	if (!isPlaying())
		return;

	m_playback = -1;
	emit playbackCleared();
}

} // namespace phonometrica
