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
 * Purpose: Create a dialog based on a user-provided Table specification. Used by the create_dialog() scripting        *
 *          function to let plugin scripts build custom GUI forms.                                                     *
 *                                                                                                                     *
 ***********************************************************************************************************************/

#ifndef PHONOMETRICA_USER_DIALOG_HPP
#define PHONOMETRICA_USER_DIALOG_HPP

#include <QDialog>
#include <QCheckBox>
#include <QComboBox>
#include <QLineEdit>
#include <QListWidget>
#include <QRadioButton>
#include <QButtonGroup>
#include <QBoxLayout>
#include <phon/dictionary.hpp>
#include <phon/runtime.hpp>

namespace phonometrica {

// Simple file-picker composite widget (line edit + browse button).
class FilePicker : public QWidget
{
	Q_OBJECT
public:
	FilePicker(const QString &title, const QString &filter, bool save, QWidget *parent = nullptr);
	QString path() const;
	void setPath(const QString &p);
private:
	QLineEdit *m_edit;
	QString m_title;
	QString m_filter;
	bool m_save;
};

// ------------------------------------------------------------------

class UserDialog final : public QDialog
{
	Q_OBJECT

public:

	// Construct from a Table (passed by the scripting engine).
	UserDialog(QWidget *parent, Runtime &rt, const Table &spec);

	// Construct from a script string that evaluates to a Table.
	UserDialog(QWidget *parent, Runtime &rt, const String &str);

	// Collect all named widget values into a Table variant.
	Variant getResult() const;

private:

	void addButtons(bool yes_no);

	bool parse(const Table &spec);

	void parseItem(const Table &item);

	String getName(const Table &item);

	void add(QWidget *widget);

	void addLabel(const Table &item);
	void addButton(const Table &item);
	void addCheckBox(const Table &item);
	void addComboBox(const Table &item);
	void addLineEdit(const Table &item);
	void addCheckList(const Table &item);
	void addRadioButtons(const Table &item);
	void addFileSelector(const Table &item);
	void addContainer(const Table &item);
	void addSpacing(const Table &item);

	QBoxLayout *m_current_layout;

	Dictionary<QCheckBox *> m_checkboxes;
	Dictionary<QComboBox *> m_comboboxes;
	Dictionary<QLineEdit *> m_fields;
	Dictionary<QListWidget *> m_checklists;
	Dictionary<QButtonGroup *> m_radiogroups;
	Dictionary<FilePicker *> m_filepickers;

	Runtime &m_runtime;
};

} // namespace phonometrica

#endif // PHONOMETRICA_USER_DIALOG_HPP
