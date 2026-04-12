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
 * Created: 29/03/2026                                                                                                 *
 *                                                                                                                     *
 * Purpose: Dialog for batch-saving multiple unsaved views. Shows a checkbox list so users can cherry-pick            *
 *          which ones to save and which to discard, instead of prompting one by one.                                  *
 *                                                                                                                     *
 ***********************************************************************************************************************/

#ifndef PHONOMETRICA_BATCH_SAVE_DIALOG_HPP
#define PHONOMETRICA_BATCH_SAVE_DIALOG_HPP

#include <QDialog>
#include <QListWidget>

namespace phonometrica {

class BatchSaveDialog : public QDialog
{
	Q_OBJECT

public:

	enum Action { SaveSelected, DiscardAll };

	/// labels: display names for each item.
	/// pre_checked: initial checked state for each item (e.g. true for previously saved concordances).
	BatchSaveDialog(const QStringList &labels, const QList<bool> &pre_checked, QWidget *parent = nullptr);

	/// Which button the user pressed (only meaningful after exec() == Accepted).
	Action action() const { return m_action; }

	/// Returns the checked state for each item (same order as constructor labels).
	QList<bool> checkedItems() const;

private:

	QListWidget *m_list = nullptr;
	Action m_action = SaveSelected;
};

} // namespace phonometrica

#endif // PHONOMETRICA_BATCH_SAVE_DIALOG_HPP
