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
 * Purpose: see header.                                                                                                *
 *                                                                                                                     *
 ***********************************************************************************************************************/

#include <QStylePainter>
#include <QMouseEvent>
#include <QAbstractItemView>
#include <phon/gui/checkable_combo_box.hpp>

namespace phonometrica {

// Number of action rows at the top of the model (Select all / Clear).
static constexpr int ACTION_ROWS = 2;

CheckableComboBox::CheckableComboBox(QWidget *parent) :
	QComboBox(parent)
{
	m_model = new QStandardItemModel(this);
	setModel(m_model);

	connect(m_model, &QStandardItemModel::itemChanged, this, &CheckableComboBox::onItemChanged);
	view()->viewport()->installEventFilter(this);
}

void CheckableComboBox::setItems(const QStringList &items)
{
	m_model->blockSignals(true);
	m_model->clear();

	// Action rows: Select all / Clear selection.
	auto *select_all = new QStandardItem(tr("Select all"));
	select_all->setFlags(Qt::ItemIsEnabled); // not checkable
	QFont bold_font = select_all->font();
	bold_font.setBold(true);
	select_all->setFont(bold_font);
	m_model->appendRow(select_all);

	auto *clear_all = new QStandardItem(tr("Clear selection"));
	clear_all->setFlags(Qt::ItemIsEnabled);
	clear_all->setFont(bold_font);
	m_model->appendRow(clear_all);

	// Checkable value rows.
	for (auto &text : items) {
		auto *item = new QStandardItem(text);
		item->setFlags(Qt::ItemIsEnabled | Qt::ItemIsUserCheckable);
		item->setCheckState(Qt::Unchecked);
		m_model->appendRow(item);
	}

	m_model->blockSignals(false);
	updateDisplayText();
}

QStringList CheckableComboBox::checkedItems() const
{
	QStringList result;
	for (int i = ACTION_ROWS; i < m_model->rowCount(); i++) {
		auto *item = m_model->item(i);
		if (item && item->checkState() == Qt::Checked) {
			result << item->text();
		}
	}
	return result;
}

void CheckableComboBox::setCheckedItems(const QStringList &items)
{
	QSet<QString> set(items.begin(), items.end());

	m_batch = true;
	for (int i = ACTION_ROWS; i < m_model->rowCount(); i++) {
		auto *item = m_model->item(i);
		if (item) {
			item->setCheckState(set.contains(item->text()) ? Qt::Checked : Qt::Unchecked);
		}
	}
	m_batch = false;

	updateDisplayText();
	emit checkedItemsChanged(checkedItems());
}

void CheckableComboBox::checkAll(bool checked)
{
	m_batch = true;
	for (int i = ACTION_ROWS; i < m_model->rowCount(); i++) {
		auto *item = m_model->item(i);
		if (item) {
			item->setCheckState(checked ? Qt::Checked : Qt::Unchecked);
		}
	}
	m_batch = false;

	updateDisplayText();
	emit checkedItemsChanged(checkedItems());
}

void CheckableComboBox::hidePopup()
{
	// Only suppress close while we're toggling a checkbox.
	// All other close triggers (click outside, Escape, focus loss) work normally.
	if (m_toggling) return;
	QComboBox::hidePopup();
}

void CheckableComboBox::paintEvent(QPaintEvent *)
{
	QStylePainter painter(this);
	QStyleOptionComboBox opt;
	initStyleOption(&opt);
	opt.currentText = m_display_text;
	painter.drawComplexControl(QStyle::CC_ComboBox, opt);
	painter.drawControl(QStyle::CE_ComboBoxLabel, opt);
}

bool CheckableComboBox::eventFilter(QObject *obj, QEvent *event)
{
	if (obj == view()->viewport()) {
		if (event->type() == QEvent::MouseButtonRelease) {
			auto *me = static_cast<QMouseEvent *>(event);
			auto index = view()->indexAt(me->pos());
			if (index.isValid()) {
				m_toggling = true;
				int row = index.row();
				if (row == 0) {
					checkAll(true);
				}
				else if (row == 1) {
					checkAll(false);
				}
				else {
					auto *item = m_model->itemFromIndex(index);
					if (item) {
						item->setCheckState(item->checkState() == Qt::Checked
						                    ? Qt::Unchecked : Qt::Checked);
					}
				}
				m_toggling = false;
			}
			return true;
		}
		if (event->type() == QEvent::MouseButtonPress) {
			return true;
		}
	}

	return QComboBox::eventFilter(obj, event);
}

void CheckableComboBox::onItemChanged(QStandardItem *)
{
	if (m_batch) return;
	updateDisplayText();
	emit checkedItemsChanged(checkedItems());
}

void CheckableComboBox::updateDisplayText()
{
	auto items = checkedItems();
	int total = m_model->rowCount() - ACTION_ROWS;

	if (items.isEmpty()) {
		m_display_text = tr("(all)");
	}
	else if (items.size() == total) {
		m_display_text = tr("(all)");
	}
	else if (items.size() <= 3) {
		m_display_text = items.join(QStringLiteral(", "));
	}
	else {
		m_display_text = tr("%1 of %2 selected").arg(items.size()).arg(total);
	}

	update();
}

} // namespace phonometrica
