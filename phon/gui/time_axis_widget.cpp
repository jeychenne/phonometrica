/***********************************************************************************************************************
 *                                                                                                                     *
 * Copyright (C) 2019-2026 Julien Eychenne                                                                             *
 *                                                                                                                     *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0. If a copy of the MPL was not   *
 * distributed with this file, You can obtain one at https://mozilla.org/MPL/2.0/.                                     *
 *                                                                                                                     *
 * Created: 22/03/2026                                                                                                 *
 *                                                                                                                     *
 * Purpose: see header.                                                                                                *
 *                                                                                                                     *
 ***********************************************************************************************************************/

#include <QPainter>
#include <QMouseEvent>
#include <phon/gui/time_axis_widget.hpp>

namespace phonometrica {

static const QColor ANCHOR_COLOUR(200, 0, 0);

TimeAxisWidget::TimeAxisWidget(TimeModel *model, QWidget *parent) :
	QWidget(parent), m_model(model)
{
	setFixedHeight(20);

	connect(m_model, &TimeModel::viewportChanged, this, &TimeAxisWidget::onViewportChanged);
	connect(m_model, &TimeModel::selectionChanged, this, &TimeAxisWidget::onSelectionChanged);
	connect(m_model, &TimeModel::selectionCleared, this, &TimeAxisWidget::onSelectionCleared);
	connect(m_model, &TimeModel::cursorChanged, this, &TimeAxisWidget::onCursorChanged);
	connect(m_model, &TimeModel::cursorCleared, this, &TimeAxisWidget::onCursorCleared);
}

void TimeAxisWidget::onViewportChanged(double, double)
{
	update();
}

void TimeAxisWidget::onSelectionChanged(double, double)
{
	update();
}

void TimeAxisWidget::onSelectionCleared()
{
	update();
}

void TimeAxisWidget::onCursorChanged(double)
{
	update();
}

void TimeAxisWidget::onCursorCleared()
{
	update();
}

double TimeAxisWidget::timeToX(double t) const
{
	double dur = m_model->windowEnd() - m_model->windowStart();
	if (dur <= 0) return 0;
	return (t - m_model->windowStart()) * width() / dur;
}

void TimeAxisWidget::paintEvent(QPaintEvent *)
{
	if (m_model->windowDuration() <= 0)
		return;

	QPainter painter(this);
	painter.setRenderHint(QPainter::TextAntialiasing);

	QFontMetrics fm = painter.fontMetrics();
	int h = height();

	// Track rectangles occupied by selection labels so we can avoid overlaps
	// when drawing the window boundary labels.
	QRect used_area1, used_area2;

	if (m_model->hasSelection())
	{
		double sel_start = m_model->selectionStart();
		double sel_end = m_model->selectionEnd();

		// Only draw selection labels if the selection is within the current viewport.
		bool sel_visible = sel_start >= m_model->windowStart() && sel_end <= m_model->windowEnd();

		if (sel_visible)
		{
			painter.save();
			painter.setPen(ANCHOR_COLOUR);

			if (m_model->hasPointSelection())
			{
				// Point selection: single label just to the right of the cursor.
				QString time = QString::number(sel_start, 'f', 4);
				int tw = fm.horizontalAdvance(time);
				int th = fm.height();
				int x = int(timeToX(sel_start)) + 3;
				int y = h - th - 1;

				// Prevent label from running off the right edge.
				if (x + tw > width())
					x = width() - tw;

				painter.drawText(x, y + fm.ascent(), time);
				used_area1 = QRect(x, y, tw, th);
			}
			else
			{
				// Span selection: left boundary label to the left of the anchor,
				// right boundary label to the right.
				QString time1 = QString::number(sel_start, 'f', 4);
				int tw1 = fm.horizontalAdvance(time1);
				int th = fm.height();
				int x1 = std::max(0, int(timeToX(sel_start)) - tw1);
				int y = h - th - 1;
				painter.drawText(x1, y + fm.ascent(), time1);
				used_area1 = QRect(x1, y, tw1, th);

				QString time2 = QString::number(sel_end, 'f', 4);
				int tw2 = fm.horizontalAdvance(time2);
				int x_limit = used_area1.right() + 6; // minimum spacing
				int x2 = std::max(x_limit, int(timeToX(sel_end)) + 3);
				painter.drawText(x2, y + fm.ascent(), time2);
				used_area2 = QRect(x2, y, tw2, th);
			}

			painter.restore();
		}
	}

	// Window start label (left-aligned).
	{
		QString from = QString::number(m_model->windowStart(), 'f', 4);
		int tw = fm.horizontalAdvance(from);
		int th = fm.height();
		int x = 0;
		int y = h - th - 1;
		QRect rect(x, y, tw, th);

		if (!rect.intersects(used_area1) && !rect.intersects(used_area2))
			painter.drawText(x, y + fm.ascent(), from);
	}

	// Window end label (right-aligned).
	{
		QString to = QString::number(m_model->windowEnd(), 'f', 4);
		int tw = fm.horizontalAdvance(to);
		int th = fm.height();
		int x = width() - tw;
		int y = h - th - 1;
		QRect rect(x, y, tw, th);

		if (!rect.intersects(used_area1) && !rect.intersects(used_area2))
			painter.drawText(x, y + fm.ascent(), to);
	}
}

void TimeAxisWidget::mousePressEvent(QMouseEvent *)
{
	m_model->clearSelection();
}

} // namespace phonometrica
