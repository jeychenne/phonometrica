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
 * Created: 28/03/2026                                                                                                 *
 *                                                                                                                     *
 * Purpose: Thin header bar shown above each view when a ViewPanel contains multiple views (split).                    *
 *          Displays the view's label and provides a button to detach the view into its own tab.                        *
 *                                                                                                                     *
 ***********************************************************************************************************************/

#ifndef PHONOMETRICA_VIEW_HEADER_HPP
#define PHONOMETRICA_VIEW_HEADER_HPP

#include <QWidget>
#include <QLabel>
#include <QToolButton>
#include <phon/gui/view.hpp>

namespace phonometrica {

class ViewHeader : public QWidget
{
	Q_OBJECT

public:

	explicit ViewHeader(View *view, QWidget *parent = nullptr);

	View *view() const { return m_view; }

	void updateLabel();

	void setDetachVisible(bool visible);
	void setCloseVisible(bool visible);

signals:

	void detachRequested(View *view);
	void closeRequested(View *view);

private:

	View *m_view;
	QLabel *m_label;
	QToolButton *m_detach_btn;
	QToolButton *m_close_btn;
};

} // namespace phonometrica

#endif // PHONOMETRICA_VIEW_HEADER_HPP
