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
 * Created: 02/04/2026                                                                                                 *
 *                                                                                                                     *
 * Purpose: A QComboBox that presents checkable items for multi-selection. The popup stays open when items are          *
 *          clicked. The display text shows either the checked item names or a count summary.                          *
 *                                                                                                                     *
 ***********************************************************************************************************************/

#ifndef PHONOMETRICA_CHECKABLE_COMBO_BOX_HPP
#define PHONOMETRICA_CHECKABLE_COMBO_BOX_HPP

#include <QComboBox>
#include <QStandardItemModel>
#include <QStyledItemDelegate>
#include <QStringList>

namespace phonometrica {

class CheckableComboBox final : public QComboBox
{
	Q_OBJECT

public:

	explicit CheckableComboBox(QWidget *parent = nullptr);

	// Set the list of items (replaces all existing items).
	void setItems(const QStringList &items);

	// Get/set checked items.
	QStringList checkedItems() const;
	void setCheckedItems(const QStringList &items);

	// Check or uncheck all items.
	void checkAll(bool checked);

signals:

	// Emitted whenever the set of checked items changes.
	void checkedItemsChanged(const QStringList &items);

protected:

	// Keep popup open on item click.
	void hidePopup() override;
	void paintEvent(QPaintEvent *event) override;
	bool eventFilter(QObject *obj, QEvent *event) override;

private:

	void onItemChanged(QStandardItem *item);
	void updateDisplayText();

	QStandardItemModel *m_model = nullptr;
	QString m_display_text;
	bool m_toggling = false;  // suppress hidePopup during checkbox toggle
	bool m_batch = false;     // suppress per-item signals during bulk updates
};

} // namespace phonometrica

#endif // PHONOMETRICA_CHECKABLE_COMBO_BOX_HPP
