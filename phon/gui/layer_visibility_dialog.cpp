/***********************************************************************************************************************
 *                                                                                                                     *
 * Copyright (C) 2019-2026 Julien Eychenne                                                                             *
 *                                                                                                                     *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0. If a copy of the MPL was not   *
 * distributed with this file, You can obtain one at https://mozilla.org/MPL/2.0/.                                     *
 *                                                                                                                     *
 * Created: 26/03/2026                                                                                                 *
 *                                                                                                                     *
 * Purpose: see header.                                                                                                *
 *                                                                                                                     *
 ***********************************************************************************************************************/

#include <QVBoxLayout>
#include <QDialogButtonBox>
#include <QLabel>
#include <QPushButton>
#include <phon/gui/layer_visibility_dialog.hpp>

namespace phonometrica {

LayerVisibilityDialog::LayerVisibilityDialog(QWidget *parent, const Handle<Annotation> &annot,
                                             const std::vector<bool> &current_visibility) :
	QDialog(parent)
{
	setWindowTitle(tr("Select visible layers"));
	setMinimumWidth(300);

	auto *layout = new QVBoxLayout(this);

	// One checkbox per layer.
	intptr_t count = annot->size();
	for (intptr_t i = 1; i <= count; i++)
	{
		auto label = annot->get_layer_label(i);
		bool has_instants = annot->layer_has_instants(i);
		auto type = has_instants ? tr("instants") : tr("intervals");
		auto text = QStringLiteral("%1. %2 (%3)").arg(i).arg(label).arg(type);

		auto *cb = new QCheckBox(text, this);
		// If we have visibility info for this layer, use it; otherwise default to visible.
		bool visible = (i < (intptr_t)current_visibility.size()) ? current_visibility[i] : true;
		cb->setChecked(visible);
		layout->addWidget(cb);
		m_checks.push_back(cb);
	}

	layout->addSpacing(8);

	// Select all / deselect all buttons.
	auto *btn_row = new QHBoxLayout;
	auto *select_all = new QPushButton(tr("Select all"), this);
	auto *deselect_all = new QPushButton(tr("Deselect all"), this);
	btn_row->addWidget(select_all);
	btn_row->addWidget(deselect_all);
	btn_row->addStretch();
	layout->addLayout(btn_row);

	connect(select_all, &QPushButton::clicked, this, [this]() {
		for (auto *cb : m_checks) cb->setChecked(true);
	});
	connect(deselect_all, &QPushButton::clicked, this, [this]() {
		for (auto *cb : m_checks) cb->setChecked(false);
	});

	// Standard OK / Cancel buttons.
	auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
	connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
	connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
	layout->addWidget(buttons);
}

std::vector<bool> LayerVisibilityDialog::visibility() const
{
	// Index 0 is unused (layers are 1-based).
	std::vector<bool> result;
	result.push_back(false); // placeholder for index 0

	for (auto *cb : m_checks)
		result.push_back(cb->isChecked());

	return result;
}

} // namespace phonometrica
