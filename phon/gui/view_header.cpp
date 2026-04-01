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
 * Purpose: see header.                                                                                                *
 *                                                                                                                     *
 ***********************************************************************************************************************/

#include <QHBoxLayout>
#include <phon/gui/view_header.hpp>

namespace phonometrica {

ViewHeader::ViewHeader(View *view, QWidget *parent) :
	QWidget(parent), m_view(view)
{
	setFixedHeight(24);

	auto *layout = new QHBoxLayout(this);
	layout->setContentsMargins(6, 0, 2, 0);
	layout->setSpacing(4);

	m_label = new QLabel;
	m_label->setStyleSheet(QStringLiteral(
		"font-size: 11px; font-weight: bold; color: palette(text);"));
	layout->addWidget(m_label, 1);

	m_detach_btn = new QToolButton;
	m_detach_btn->setIcon(QIcon(QStringLiteral(":/icons/maximize.svg")));
	m_detach_btn->setIconSize(QSize(14, 14));
	m_detach_btn->setAutoRaise(true);
	m_detach_btn->setFixedSize(20, 20);
	m_detach_btn->setToolTip(tr("Detach to own tab"));
	layout->addWidget(m_detach_btn);

	m_close_btn = new QToolButton;
	m_close_btn->setIcon(QIcon(QStringLiteral(":/icons/circle-x.svg")));
	m_close_btn->setIconSize(QSize(14, 14));
	m_close_btn->setAutoRaise(true);
	m_close_btn->setFixedSize(20, 20);
	m_close_btn->setToolTip(tr("Close view"));
	layout->addWidget(m_close_btn);

	// Subtle bottom border to separate the header from the view.
	setStyleSheet(QStringLiteral(
		"ViewHeader { border-bottom: 1px solid palette(mid); }"));

	updateLabel();

	connect(m_detach_btn, &QToolButton::clicked, this, [this]() {
		emit detachRequested(m_view);
	});

	connect(m_close_btn, &QToolButton::clicked, this, [this]() {
		emit closeRequested(m_view);
	});

	connect(m_view, &View::titleChanged, this, [this]() {
		updateLabel();
	});
}

void ViewHeader::updateLabel()
{
	m_label->setText(m_view->label());
}

void ViewHeader::setDetachVisible(bool visible)
{
	m_detach_btn->setVisible(visible);
}

void ViewHeader::setCloseVisible(bool visible)
{
	m_close_btn->setVisible(visible);
}

} // namespace phonometrica
