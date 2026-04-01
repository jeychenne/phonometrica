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
 * Purpose: see header.                                                                                                *
 *                                                                                                                     *
 ***********************************************************************************************************************/

#include <QVBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QDialogButtonBox>
#include <phon/gui/batch_save_dialog.hpp>

namespace phonometrica {

BatchSaveDialog::BatchSaveDialog(const QStringList &labels, const QList<bool> &pre_checked, QWidget *parent) :
	QDialog(parent)
{
	setWindowTitle(tr("Unsaved concordances"));
	setMinimumWidth(400);

	auto *layout = new QVBoxLayout(this);

	auto *message = new QLabel(tr("The following concordances have unsaved changes.\n"
	                               "Select which ones to save:"));
	layout->addWidget(message);

	m_list = new QListWidget;
	for (int i = 0; i < labels.size(); i++)
	{
		auto *item = new QListWidgetItem(labels[i]);
		item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
		bool checked = (i < pre_checked.size()) ? pre_checked[i] : false;
		item->setCheckState(checked ? Qt::Checked : Qt::Unchecked);
		m_list->addItem(item);
	}
	layout->addWidget(m_list, 1);

	// Buttons: Save selected | Discard all | Cancel
	auto *btn_layout = new QHBoxLayout;
	btn_layout->addStretch();

	auto *save_btn = new QPushButton(tr("Save selected"));
	save_btn->setDefault(true);
	btn_layout->addWidget(save_btn);

	auto *discard_btn = new QPushButton(tr("Discard all"));
	btn_layout->addWidget(discard_btn);

	auto *cancel_btn = new QPushButton(tr("Cancel"));
	btn_layout->addWidget(cancel_btn);

	layout->addLayout(btn_layout);

	connect(save_btn, &QPushButton::clicked, this, [this]() {
		m_action = SaveSelected;
		accept();
	});

	connect(discard_btn, &QPushButton::clicked, this, [this]() {
		m_action = DiscardAll;
		accept();
	});

	connect(cancel_btn, &QPushButton::clicked, this, &QDialog::reject);
}

QList<bool> BatchSaveDialog::checkedItems() const
{
	QList<bool> result;
	for (int i = 0; i < m_list->count(); i++) {
		result.append(m_list->item(i)->checkState() == Qt::Checked);
	}
	return result;
}

} // namespace phonometrica
