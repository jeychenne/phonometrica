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
 * Purpose: Dialog for recoding the levels of a categorical (text) column. Displays a two-column table where the       *
 *          left column shows the original values (read-only) and the right column shows the new values (editable,     *
 *          defaulting to the original). The result is used to create a new column with remapped values.               *
 *                                                                                                                     *
 ***********************************************************************************************************************/

#ifndef PHONOMETRICA_RECODE_DIALOG_HPP
#define PHONOMETRICA_RECODE_DIALOG_HPP

#include <QDialog>
#include <QTableWidget>
#include <QLineEdit>

namespace phonometrica {

class RecodeDialog final : public QDialog
{
	Q_OBJECT

public:

	/// @param column_name  Display name of the column being recoded.
	/// @param levels       Unique values in the column (sorted).
	RecodeDialog(const QString &column_name, const QStringList &levels, QWidget *parent = nullptr);

	/// Name chosen for the new column.
	QString newColumnName() const;

	/// Mapping from original value to new value.
	/// Keys are all original levels; values are the (possibly edited) new labels.
	QMap<QString, QString> mapping() const;

private:

	QTableWidget *m_table = nullptr;
	QLineEdit *m_name_edit = nullptr;
	QStringList m_levels;
};

} // namespace phonometrica

#endif // PHONOMETRICA_RECODE_DIALOG_HPP
