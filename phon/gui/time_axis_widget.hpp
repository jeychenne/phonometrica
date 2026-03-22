/***********************************************************************************************************************
 *                                                                                                                     *
 * Copyright (C) 2019-2026 Julien Eychenne                                                                             *
 *                                                                                                                     *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0. If a copy of the MPL was not   *
 * distributed with this file, You can obtain one at https://mozilla.org/MPL/2.0/.                                     *
 *                                                                                                                     *
 * Created: 22/03/2026                                                                                                 *
 *                                                                                                                     *
 * Purpose: Widget to display time information between the toolbar and the plots in sound and annotation views.        *
 *          Shows the current viewport boundaries (left/right) and selection times in a contrasting colour.             *
 *          Clicking the widget clears the current selection.                                                           *
 *                                                                                                                     *
 ***********************************************************************************************************************/

#ifndef PHONOMETRICA_TIME_AXIS_WIDGET_HPP
#define PHONOMETRICA_TIME_AXIS_WIDGET_HPP

#include <QWidget>
#include <phon/gui/time_model.hpp>

namespace phonometrica {

class TimeAxisWidget final : public QWidget
{
	Q_OBJECT

public:

	TimeAxisWidget(TimeModel *model, QWidget *parent = nullptr);

protected:

	void paintEvent(QPaintEvent *event) override;
	void mousePressEvent(QMouseEvent *event) override;

private slots:

	void onViewportChanged(double start, double end);
	void onSelectionChanged(double t1, double t2);
	void onSelectionCleared();

private:

	double timeToX(double t) const;

	TimeModel *m_model;
};

} // namespace phonometrica

#endif // PHONOMETRICA_TIME_AXIS_WIDGET_HPP
