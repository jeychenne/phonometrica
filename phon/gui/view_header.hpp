/***********************************************************************************************************************
 *                                                                                                                     *
 * Copyright (C) 2019-2026 Julien Eychenne                                                                             *
 *                                                                                                                     *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0. If a copy of the MPL was not   *
 * distributed with this file, You can obtain one at https://mozilla.org/MPL/2.0/.                                     *
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
