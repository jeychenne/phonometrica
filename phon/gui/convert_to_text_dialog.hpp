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
 * Created: 09/04/2026                                                                                                 *
 *                                                                                                                     *
 * Purpose: Dialog for converting a numeric column to a text column. The user specifies a template string where '%'    *
 *          is replaced by the cell value, allowing optional prefixes/suffixes (e.g. "Subject %" -> "Subject 308").    *
 *                                                                                                                     *
 ***********************************************************************************************************************/

#ifndef PHONOMETRICA_CONVERT_TO_TEXT_DIALOG_HPP
#define PHONOMETRICA_CONVERT_TO_TEXT_DIALOG_HPP

#include <QDialog>
#include <QLineEdit>
#include <QTableWidget>
#include <QDialogButtonBox>

namespace phonometrica {

class ConvertToTextDialog final : public QDialog
{
	Q_OBJECT

public:

	/// @param column_name  Display name of the source column.
	/// @param samples      First few cell values (already formatted as strings via get_cell).
	ConvertToTextDialog(const QString &column_name, const QStringList &samples, QWidget *parent = nullptr);

	/// The template entered by the user (with '%' as placeholder).
	QString templateString() const;

	/// The name chosen for the new column.
	QString newColumnName() const;

	/// Apply the template to a single cell value.
	static QString applyTemplate(const QString &tmpl, const QString &value);

private:

	void updatePreview();

	QLineEdit *m_template_edit = nullptr;
	QLineEdit *m_name_edit = nullptr;
	QTableWidget *m_preview = nullptr;
	QDialogButtonBox *m_buttons = nullptr;

	QStringList m_samples;
	QString m_column_name;
	bool m_auto_name = true;
};

} // namespace phonometrica

#endif // PHONOMETRICA_CONVERT_TO_TEXT_DIALOG_HPP
