/***********************************************************************************************************************
 *                                                                                                                     *
 * Copyright (C) 2019-2026 Julien Eychenne                                                                             *
 *                                                                                                                     *
 * This program is free software: you can redistribute it and/or modify it under the terms of the GNU General Public   *
 * License as published by the Free Software Foundation, either version 2 of the License, or (at your option) any      *
 * later version.                                                                                                      *
 *                                                                                                                     *
 * This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied  *
 * warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for more       *
 * details.                                                                                                            *
 *                                                                                                                     *
 * You should have received a copy of the GNU General Public License along with this program. If not, see              *
 * <http://www.gnu.org/licenses/>.                                                                                     *
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
