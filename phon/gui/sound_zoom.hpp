/***********************************************************************************************************************
 *                                                                                                                     *
 * Copyright (C) 2019-2026 Julien Eychenne                                                                             *
 *                                                                                                                     *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0. If a copy of the MPL was not   *
 * distributed with this file, You can obtain one at https://mozilla.org/MPL/2.0/.                                     *
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

public slots:

	// Called when the wavebar viewport pixel coordinates change.
	void setSelection(double x1, double x2);

protected:

	void paintEvent(QPaintEvent *event) override;

private:

	double m_sel_x1 = -1;
	double m_sel_x2 = -1;
};

} // namespace phonometrica

#endif // PHONOMETRICA_SOUND_ZOOM_HPP
