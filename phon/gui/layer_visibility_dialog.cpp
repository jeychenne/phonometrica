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
	for (intptr_t i = 0; i < count; i++)
	{
		auto label = annot->get_layer_label(i);
		bool has_instants = annot->layer_has_instants(i);
		auto type = has_instants ? tr("instants") : tr("intervals");
		auto text = QStringLiteral("%1. %2 (%3)").arg(i + 1).arg(label).arg(type);

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
	std::vector<bool> result;

	for (auto *cb : m_checks)
		result.push_back(cb->isChecked());

	return result;
}

} // namespace phonometrica
