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
 * Created: 03/04/2026                                                                                                 *
 *                                                                                                                     *
 * Purpose: Dialog for applying a user-defined formula to a numeric column. Provides a formula input field with live   *
 *          preview, a column name field with auto-generation, and a help summary of available functions.              *
 *                                                                                                                     *
 ***********************************************************************************************************************/

#ifndef PHONOMETRICA_TRANSFORM_DIALOG_HPP
#define PHONOMETRICA_TRANSFORM_DIALOG_HPP

#include <QDialog>
#include <QLineEdit>
#include <QTableWidget>
#include <QLabel>
#include <QDialogButtonBox>
#include <QVector>

namespace phonometrica {

class TransformDialog final : public QDialog
{
	Q_OBJECT

public:

	/// @param column_name  Display name of the column being transformed.
	/// @param samples      First few values from the column (for the live preview).
	TransformDialog(const QString &column_name, const QVector<double> &samples, QWidget *parent = nullptr);

	/// The formula entered by the user.
	QString formula() const;

	/// The name chosen for the new column.
	QString newColumnName() const;

private:

	void updatePreview();

	QLineEdit *m_formula_edit = nullptr;
	QLineEdit *m_name_edit = nullptr;
	QTableWidget *m_preview = nullptr;
	QLabel *m_error_label = nullptr;
	QDialogButtonBox *m_buttons = nullptr;

	QVector<double> m_samples;
	QString m_column_name;
	bool m_auto_name = true;
};

} // namespace phonometrica

#endif // PHONOMETRICA_TRANSFORM_DIALOG_HPP
