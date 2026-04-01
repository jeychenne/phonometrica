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
 * Purpose: see header.                                                                                                *
 *                                                                                                                     *
 ***********************************************************************************************************************/

#include <QHBoxLayout>
#include <QLineEdit>
#include <QCheckBox>
#include <QPushButton>
#include <QLabel>
#include <QKeyEvent>
#include <phon/gui/search_bar.hpp>

namespace phonometrica {

SearchBar::SearchBar(QWidget *parent) :
	QWidget(parent)
{
	setupUi();
	hide();
}

void SearchBar::setupUi()
{
	auto *layout = new QHBoxLayout(this);
	layout->setContentsMargins(4, 2, 4, 2);

	// Close button
	m_close_btn = new QPushButton(QStringLiteral("×"), this);
	m_close_btn->setFixedSize(22, 22);
	m_close_btn->setFlat(true);
	m_close_btn->setToolTip(tr("Close (Escape)"));
	layout->addWidget(m_close_btn);
	connect(m_close_btn, &QPushButton::clicked, this, &QWidget::hide);

	// Search field
	m_search_edit = new QLineEdit(this);
	m_search_edit->setPlaceholderText(tr("Find text"));
	m_search_edit->setClearButtonEnabled(true);
	layout->addWidget(m_search_edit);

	// Find button
	m_find_btn = new QPushButton(tr("Find"), this);
	layout->addWidget(m_find_btn);
	connect(m_find_btn, &QPushButton::clicked, this, &SearchBar::findRequested);

	// Replace field
	m_replace_edit = new QLineEdit(this);
	m_replace_edit->setPlaceholderText(tr("Replace with"));
	m_replace_edit->setClearButtonEnabled(true);
	layout->addWidget(m_replace_edit);

	// Replace buttons
	m_replace_btn = new QPushButton(tr("Replace"), this);
	layout->addWidget(m_replace_btn);
	connect(m_replace_btn, &QPushButton::clicked, this, &SearchBar::replaceRequested);

	m_replace_all_btn = new QPushButton(tr("Replace all"), this);
	layout->addWidget(m_replace_all_btn);
	connect(m_replace_all_btn, &QPushButton::clicked, this, &SearchBar::replaceAllRequested);

	// Options
	m_case_check = new QCheckBox(tr("Case"), this);
	m_case_check->setToolTip(tr("Case sensitive"));
	layout->addWidget(m_case_check);

	m_regex_check = new QCheckBox(tr("Regex"), this);
	m_regex_check->setToolTip(tr("Use regular expressions"));
	layout->addWidget(m_regex_check);

	// Enter in the search field triggers find
	connect(m_search_edit, &QLineEdit::returnPressed, this, &SearchBar::findRequested);

	// Enter in the replace field triggers replace
	connect(m_replace_edit, &QLineEdit::returnPressed, this, &SearchBar::replaceRequested);
}

void SearchBar::setSearch()
{
	m_replace_edit->hide();
	m_replace_btn->hide();
	m_replace_all_btn->hide();
	show();
	m_search_edit->setFocus();
	m_search_edit->selectAll();
}

void SearchBar::setSearchAndReplace()
{
	m_replace_edit->show();
	m_replace_btn->show();
	m_replace_all_btn->show();
	show();
	m_search_edit->setFocus();
	m_search_edit->selectAll();
}

QString SearchBar::searchText() const
{
	return m_search_edit->text();
}

QString SearchBar::replacementText() const
{
	return m_replace_edit->text();
}

bool SearchBar::usesRegex() const
{
	return m_regex_check->isChecked();
}

bool SearchBar::isCaseSensitive() const
{
	return m_case_check->isChecked();
}

} // namespace phonometrica
