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
 * Purpose: Find/Replace bar for the script editor.                                                                    *
 *                                                                                                                     *
 ***********************************************************************************************************************/

#ifndef PHONOMETRICA_SEARCH_BAR_HPP
#define PHONOMETRICA_SEARCH_BAR_HPP

#include <QWidget>

class QLineEdit;
class QCheckBox;
class QComboBox;
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

	// Populate and show a layer selector combo.
	// The first item is always "(All layers)"; subsequent items correspond to
	// 1-based layer indices. When not called, the combo stays hidden.
	void setLayerChoices(const QStringList &layer_names);

	QString searchText() const;
	QString replacementText() const;
	bool usesRegex() const;
	bool isCaseSensitive() const;

	// Returns 0 for "(All layers)", or a 1-based layer index.
	int selectedLayer() const;

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
	QComboBox *m_layer_combo = nullptr;
	QPushButton *m_find_btn = nullptr;
	QPushButton *m_replace_btn = nullptr;
	QPushButton *m_replace_all_btn = nullptr;
	QPushButton *m_close_btn = nullptr;
};

} // namespace phonometrica

#endif // PHONOMETRICA_SEARCH_BAR_HPP
