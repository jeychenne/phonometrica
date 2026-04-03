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
 * Purpose: see header.                                                                                                *
 *                                                                                                                     *
 ***********************************************************************************************************************/

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QHeaderView>
#include <QDialogButtonBox>
#include <phon/gui/recode_dialog.hpp>

namespace phonometrica {

RecodeDialog::RecodeDialog(const QString &column_name, const QStringList &levels, QWidget *parent) :
	QDialog(parent), m_levels(levels)
{
	setWindowTitle(tr("Recode \"%1\"").arg(column_name));
	setMinimumWidth(400);

	auto *layout = new QVBoxLayout(this);

	// ── Column name ────────────────────────────────────
	auto *name_row = new QHBoxLayout;
	name_row->addWidget(new QLabel(tr("New column name:")));
	m_name_edit = new QLineEdit(column_name + QStringLiteral("_recoded"));
	name_row->addWidget(m_name_edit);
	layout->addLayout(name_row);

	// ── Mapping table ──────────────────────────────────
	layout->addWidget(new QLabel(tr("Edit the new values for each level:")));

	m_table = new QTableWidget(levels.size(), 2, this);
	m_table->setHorizontalHeaderLabels({tr("Original value"), tr("New value")});
	m_table->horizontalHeader()->setStretchLastSection(true);
	m_table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
	m_table->verticalHeader()->hide();
	m_table->setSelectionMode(QAbstractItemView::SingleSelection);

	for (int i = 0; i < levels.size(); i++)
	{
		// Original value (read-only).
		auto *original = new QTableWidgetItem(levels[i]);
		original->setFlags(original->flags() & ~Qt::ItemIsEditable);
		m_table->setItem(i, 0, original);

		// New value (editable, defaults to original).
		auto *recoded = new QTableWidgetItem(levels[i]);
		m_table->setItem(i, 1, recoded);
	}

	layout->addWidget(m_table, 1);

	// ── Buttons ────────────────────────────────────────
	auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
	connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
	connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
	layout->addWidget(buttons);
}

QString RecodeDialog::newColumnName() const
{
	return m_name_edit->text().trimmed();
}

QMap<QString, QString> RecodeDialog::mapping() const
{
	QMap<QString, QString> map;
	for (int i = 0; i < m_levels.size(); i++)
	{
		auto new_val = m_table->item(i, 1)->text().trimmed();
		if (new_val.isEmpty()) {
			new_val = m_levels[i]; // Fall back to original if left empty.
		}
		map[m_levels[i]] = new_val;
	}
	return map;
}

} // namespace phonometrica
