/***********************************************************************************************************************
 *                                                                                                                     *
 * Copyright (C) 2019-2026 Julien Eychenne                                                                             *
 *                                                                                                                     *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0. If a copy of the MPL was not   *
 * distributed with this file, You can obtain one at https://mozilla.org/MPL/2.0/.                                     *
 *                                                                                                                     *
 * Created: 22/03/2026                                                                                                 *
 *                                                                                                                     *
 * Purpose: Find/Replace bar for the script editor.                                                                    *
 *                                                                                                                     *
 ***********************************************************************************************************************/

#ifndef PHONOMETRICA_SEARCH_BAR_HPP
#define PHONOMETRICA_SEARCH_BAR_HPP

#include <QWidget>

class QLineEdit;
class QCheckBox;
class QPushButton;

namespace phonometrica {

class SearchBar : public QWidget
{
	Q_OBJECT

public:

	explicit SearchBar(QWidget *parent = nullptr);

	// Show only the search field.
	void setSearch();

	// Show both search and replace fields.
	void setSearchAndReplace();

	QString searchText() const;
	QString replacementText() const;
	bool usesRegex() const;
	bool isCaseSensitive() const;

signals:

	void findRequested();
	void replaceRequested();
	void replaceAllRequested();

private:

	void setupUi();

	QLineEdit *m_search_edit = nullptr;
	QLineEdit *m_replace_edit = nullptr;
	QCheckBox *m_case_check = nullptr;
	QCheckBox *m_regex_check = nullptr;
	QPushButton *m_find_btn = nullptr;
	QPushButton *m_replace_btn = nullptr;
	QPushButton *m_replace_all_btn = nullptr;
	QPushButton *m_close_btn = nullptr;
};

} // namespace phonometrica

#endif // PHONOMETRICA_SEARCH_BAR_HPP
