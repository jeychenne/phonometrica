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

void SoundZoom::paintEvent(QPaintEvent *)
{
	if (m_sel_x1 < 0)
		return;

	QPainter p(this);
	p.setRenderHint(QPainter::Antialiasing, true);

	int w = width();
	int h = height();

	// Trapezoid: top-left (0,0) → bottom-left (sel_x1, h) → bottom-right (sel_x2, h) → top-right (w, 0).
	QPainterPath path;
	path.moveTo(0, 0);
	path.lineTo(m_sel_x1, h);
	path.lineTo(m_sel_x2, h);
	path.lineTo(w, 0);
	path.closeSubpath();

	p.fillPath(path, ZOOM_COLOR);
}

} // namespace phonometrica
