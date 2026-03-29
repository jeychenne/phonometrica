/***********************************************************************************************************************
 *                                                                                                                     *
 * Copyright (C) 2019-2026 Julien Eychenne                                                                             *
 *                                                                                                                     *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0. If a copy of the MPL was not   *
 * distributed with this file, You can obtain one at https://mozilla.org/MPL/2.0/.                                     *
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
