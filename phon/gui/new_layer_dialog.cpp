/***********************************************************************************************************************
 *                                                                                                                     *
 * Copyright (C) 2019-2026 Julien Eychenne                                                                             *
 *                                                                                                                     *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0. If a copy of the MPL was not   *
 * distributed with this file, You can obtain one at https://mozilla.org/MPL/2.0/.                                     *
 *                                                                                                                     *
 * Created: 25/03/2026                                                                                                 *
 *                                                                                                                     *
 * Purpose: see header.                                                                                                *
 *                                                                                                                     *
 ***********************************************************************************************************************/

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFormLayout>
#include <QLabel>
#include <QGroupBox>
#include <QDialogButtonBox>
#include <phon/gui/new_layer_dialog.hpp>

namespace phonometrica {

NewLayerDialog::NewLayerDialog(QWidget *parent, intptr_t current_layer_count) :
	QDialog(parent)
{
	setWindowTitle(tr("Add new layer"));
	setMinimumWidth(300);

	auto *layout = new QVBoxLayout(this);

	auto *form = new QFormLayout;

	m_name_edit = new QLineEdit(this);
	m_name_edit->setPlaceholderText(tr("(optional)"));
	form->addRow(tr("Layer name:"), m_name_edit);

	m_index_spin = new QSpinBox(this);
	m_index_spin->setMinimum(1);
	m_index_spin->setMaximum((int)current_layer_count + 1);
	m_index_spin->setValue((int)current_layer_count + 1);
	form->addRow(tr("Position:"), m_index_spin);

	layout->addLayout(form);

	// Layer type.
	auto *type_group = new QGroupBox(tr("Layer type"), this);
	auto *type_layout = new QVBoxLayout(type_group);

	m_interval_radio = new QRadioButton(tr("Intervals"), type_group);
	m_instant_radio = new QRadioButton(tr("Instants"), type_group);
	m_interval_radio->setChecked(true);

	type_layout->addWidget(m_interval_radio);
	type_layout->addWidget(m_instant_radio);
	layout->addWidget(type_group);

	// Buttons.
	auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
	connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
	connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
	layout->addWidget(buttons);
}

QString NewLayerDialog::layerName() const
{
	return m_name_edit->text();
}

intptr_t NewLayerDialog::layerIndex() const
{
	return m_index_spin->value();
}

bool NewLayerDialog::hasInstants() const
{
	return m_instant_radio->isChecked();
}

} // namespace phonometrica
