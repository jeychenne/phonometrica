/***********************************************************************************************************************
 *                                                                                                                     *
 * Copyright (C) 2019-2026 Julien Eychenne                                                                             *
 *                                                                                                                     *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0. If a copy of the MPL was not   *
 * distributed with this file, You can obtain one at https://mozilla.org/MPL/2.0/.                                     *
 *                                                                                                                     *
 * Created: 26/03/2026                                                                                                 *
 *                                                                                                                     *
 * Purpose: Dialog for choosing a CSV file and separator for metadata import/export.                                   *
 *                                                                                                                     *
 ***********************************************************************************************************************/

#ifndef PHONOMETRICA_CSV_DIALOG_HPP
#define PHONOMETRICA_CSV_DIALOG_HPP

#include <QDialog>
#include <QLineEdit>
#include <QComboBox>
#include <phon/string.hpp>

namespace phonometrica {

class CsvDialog : public QDialog
{
	Q_OBJECT

public:

	// If read is true, the dialog asks for an existing file to open.
	// If read is false, the dialog asks for a file path to save to.
	CsvDialog(const QString &title, bool read, QWidget *parent = nullptr);

	String filePath() const;
	String separator() const;

private:

	QLineEdit *m_path_edit;
	QComboBox *m_sep_combo;
	bool m_read;
};

} // namespace phonometrica

#endif // PHONOMETRICA_CSV_DIALOG_HPP
