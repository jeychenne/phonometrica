/***********************************************************************************************************************
 *                                                                                                                     *
 * Copyright (C) 2019-2026 Julien Eychenne                                                                             *
 *                                                                                                                     *
 * This program is free software: you can redistribute it and/or modify it under the terms of the GNU General Public   *
 * License as published by the Free Software Foundation, either version 2 of the License, or (at your option) any      *
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

#include <QPainter>
#include <QPainterPath>
#include <phon/gui/sound_zoom.hpp>

namespace phonometrica {

static const QColor ZOOM_COLOR(0, 0, 204, 60);

SoundZoom::SoundZoom(QWidget *parent) : QWidget(parent)
{
	setFixedHeight(40);
	setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
}

void SoundZoom::setSelection(double x1, double x2)
{
	m_sel_x1 = x1;
	m_sel_x2 = x2;
	update();
}

void SoundZoom::setLeftOffset(int offset)
{
	m_left_offset = offset;
	update();
}

void SoundZoom::paintEvent(QPaintEvent *)
{
	if (m_sel_x1 < 0)
		return;

	QPainter p(this);
	p.setRenderHint(QPainter::Antialiasing, true);

	int w = width();
	int h = height();

	QPainterPath path;
	path.moveTo(m_left_offset, 0);
	path.lineTo(m_sel_x1, h);
	path.lineTo(m_sel_x2, h);
	path.lineTo(w, 0);
	path.closeSubpath();

	p.fillPath(path, ZOOM_COLOR);
}

} // namespace phonometrica
