/***********************************************************************************************************************
 *                                                                                                                     *
 * Copyright (C) 2019-2026 Julien Eychenne                                                                             *
 *                                                                                                                     *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0. If a copy of the MPL was not   *
 * distributed with this file, You can obtain one at https://mozilla.org/MPL/2.0/.                                     *
 *                                                                                                                     *
 * Created: 29/03/2026                                                                                                 *
 *                                                                                                                     *
 * Purpose: Dialog for batch-saving multiple unsaved concordances. Shows a checkbox list so users can cherry-pick      *
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
