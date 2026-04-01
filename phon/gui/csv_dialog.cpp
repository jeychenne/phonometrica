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
#include <QHBoxLayout>
#include <QFormLayout>
#include <QLabel>
#include <QPushButton>
#include <QFileDialog>
#include <QDialogButtonBox>
#include <phon/gui/csv_dialog.hpp>

namespace phonometrica {

CsvDialog::CsvDialog(const QString &title, bool read, QWidget *parent) :
	QDialog(parent), m_read(read)
{
	setWindowTitle(title);
	setMinimumWidth(400);

	auto *layout = new QVBoxLayout(this);

	// File path row.
	auto *path_row = new QHBoxLayout;
	m_path_edit = new QLineEdit(this);
	m_path_edit->setPlaceholderText(tr("CSV file path..."));
	auto *browse_btn = new QPushButton(tr("Browse..."), this);
	path_row->addWidget(m_path_edit, 1);
	path_row->addWidget(browse_btn);
	layout->addLayout(path_row);

	connect(browse_btn, &QPushButton::clicked, [this]() {
		QString path;
		if (m_read)
			path = QFileDialog::getOpenFileName(this, tr("Select CSV file"), QString(), tr("CSV files (*.csv *.tsv *.txt);;All files (*)"));
		else
			path = QFileDialog::getSaveFileName(this, tr("Export to CSV"), QString(), tr("CSV files (*.csv *.tsv *.txt);;All files (*)"));
		if (!path.isEmpty())
			m_path_edit->setText(path);
	});

	// Separator choice.
	auto *form = new QFormLayout;
	m_sep_combo = new QComboBox(this);
	m_sep_combo->addItem(tr("Tab"), QStringLiteral("\t"));
	m_sep_combo->addItem(tr("Comma"), QStringLiteral(","));
	m_sep_combo->addItem(tr("Semicolon"), QStringLiteral(";"));
	m_sep_combo->setCurrentIndex(0);
	form->addRow(tr("Separator:"), m_sep_combo);
	layout->addLayout(form);

	// Buttons.
	auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
	connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
	connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
	layout->addWidget(buttons);
}

String CsvDialog::filePath() const
{
	return String(m_path_edit->text().toUtf8().constData());
}

String CsvDialog::separator() const
{
	return String(m_sep_combo->currentData().toString().toUtf8().constData());
}

} // namespace phonometrica
