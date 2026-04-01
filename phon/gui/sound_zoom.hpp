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
 * Purpose: Visual connector widget between the waveform area and the wavebar. Draws a filled trapezoid from the       *
 *          full width at the top (representing the waveform area) to the viewport selection in the wavebar at the     *
 *          bottom, creating a zoom-funnel effect.                                                                     *
 *                                                                                                                     *
 ***********************************************************************************************************************/

#ifndef PHONOMETRICA_SOUND_ZOOM_HPP
#define PHONOMETRICA_SOUND_ZOOM_HPP

#include <QWidget>

namespace phonometrica {

class SoundZoom : public QWidget
{
	Q_OBJECT

public:

	explicit SoundZoom(QWidget *parent = nullptr);

	void setLeftOffset(int offset);

public slots:

	// Called when the wavebar viewport pixel coordinates change.
	void setSelection(double x1, double x2);

protected:

	void paintEvent(QPaintEvent *event) override;

private:

	double m_sel_x1 = -1;
	double m_sel_x2 = -1;
	int m_left_offset = 0;
};

} // namespace phonometrica

#endif // PHONOMETRICA_SOUND_ZOOM_HPP
