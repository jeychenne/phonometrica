/***********************************************************************************************************************
 *                                                                                                                     *
 * Copyright (C) 2019-2026 Julien Eychenne                                                                             *
 *                                                                                                                     *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0. If a copy of the MPL was not   *
 * distributed with this file, You can obtain one at https://mozilla.org/MPL/2.0/.                                     *
 *                                                                                                                     *
 * Created: 28/03/2026                                                                                                 *
 *                                                                                                                     *
 * Purpose: Create a dialog based on a user-provided JSON/Table specification. Used by the create_dialog() scripting   *
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
#include <phon/runtime/json.hpp>
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
	UserDialog(QWidget *parent, Runtime &rt, const Json &js);

	// Construct from a script string that evaluates to a Table.
	UserDialog(QWidget *parent, Runtime &rt, const String &str);

	// Collect all named widget values into a Table variant.
	Variant getResult() const;

private:

	void addButtons(bool yes_no);

	bool parse(const Json &js);

	void parseItem(Json item);

	String getName(const Json &item);

	void add(QWidget *widget);

	void addLabel(const Json &item);
	void addButton(const Json &item);
	void addCheckBox(const Json &item);
	void addComboBox(const Json &item);
	void addLineEdit(const Json &item);
	void addCheckList(const Json &item);
	void addRadioButtons(const Json &item);
	void addFileSelector(const Json &item);
	void addContainer(const Json &item);
	void addSpacing(const Json &item);

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
