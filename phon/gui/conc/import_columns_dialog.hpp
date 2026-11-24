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
 * Created: 24/11/2026                                                                                                 *
 *                                                                                                                     *
 * Purpose: dialog to import columns from a CSV file into a concordance, by ID, by line or by key lookup. The dialog   *
 *          previews the file, lets the user choose the separator, the key columns, the columns to import (and their   *
 *          names) and how to handle missing values, and reports how many rows will be matched before importing.       *
 *                                                                                                                     *
 ***********************************************************************************************************************/

#ifndef PHONOMETRICA_IMPORT_COLUMNS_DIALOG_HPP
#define PHONOMETRICA_IMPORT_COLUMNS_DIALOG_HPP

#include <QDialog>
#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QTableWidget>
#include <phon/application/conc/column_import.hpp>

namespace phonometrica {

class ImportColumnsDialog final : public QDialog
{
	Q_OBJECT

public:

	ImportColumnsDialog(Handle<Concordance> conc, ColumnImportMode mode, QWidget *parent = nullptr);

	/// Number of columns appended to the concordance (valid once the dialog has been accepted).
	int importedColumnCount() const { return m_imported_count; }

	/// Imports the columns; the dialog stays open (with an error message) if the import fails.
	void accept() override;

private:

	void browse();
	void loadFile(const QString &path);
	void reparse();
	void populateFromCsv();
	void updatePreview();
	void updateStatus();
	void onCsvKeyChanged();
	void setAllChecked(bool checked);

	QString separator() const;
	ColumnImportOptions options() const;
	QString uniqueColumnName(const QString &name, const QStringList &taken) const;

	Handle<Concordance> m_conc;
	ColumnImportMode m_mode;

	String m_text;           // raw contents of the file
	CsvTable m_csv;
	bool m_csv_valid = false;
	QString m_read_error;    // why the file could not be read or parsed
	QString m_loaded_path;   // path of the file last loaded (or attempted)
	int m_previous_csv_key = -1;
	int m_imported_count = 0;

	QLineEdit *m_path_edit = nullptr;
	QComboBox *m_sep_combo = nullptr;
	QLineEdit *m_sep_edit = nullptr;
	QCheckBox *m_header_check = nullptr;
	QTableWidget *m_preview = nullptr;

	QComboBox *m_conc_key_combo = nullptr;
	QComboBox *m_csv_key_combo = nullptr;
	QCheckBox *m_ignore_case_check = nullptr;

	QListWidget *m_column_list = nullptr;

	QComboBox *m_missing_combo = nullptr;
	QLineEdit *m_custom_edit = nullptr;

	QLabel *m_status_label = nullptr;
	QDialogButtonBox *m_buttons = nullptr;
};

} // namespace phonometrica

#endif // PHONOMETRICA_IMPORT_COLUMNS_DIALOG_HPP
