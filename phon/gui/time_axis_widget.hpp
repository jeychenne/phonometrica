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
	void onCursorChanged(double time);
	void onCursorCleared();

private:

	double timeToX(double t) const;

	TimeModel *m_model;
};

} // namespace phonometrica

#endif // PHONOMETRICA_TIME_AXIS_WIDGET_HPP
