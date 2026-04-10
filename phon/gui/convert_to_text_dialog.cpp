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
 * Created: 09/04/2026                                                                                                 *
 *                                                                                                                     *
 * Purpose: see header.                                                                                                *
 *                                                                                                                     *
 ***********************************************************************************************************************/

#include <QPushButton>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <phon/gui/convert_to_text_dialog.hpp>

namespace phonometrica {

ConvertToTextDialog::ConvertToTextDialog(const QString &column_name, const QStringList &samples, QWidget *parent) :
	QDialog(parent), m_samples(samples), m_column_name(column_name)
{
	setWindowTitle(tr("Convert \"%1\" to text").arg(column_name));
	setMinimumWidth(450);

	auto *layout = new QVBoxLayout(this);

	// ── Template input ─────────────────────────────────
	auto *tmpl_row = new QHBoxLayout;
	tmpl_row->addWidget(new QLabel(tr("Template:")));
	m_template_edit = new QLineEdit;
	m_template_edit->setText(QStringLiteral("%"));
	m_template_edit->setPlaceholderText(tr("Use % as placeholder (e.g. \"Subject %\")"));
	tmpl_row->addWidget(m_template_edit);
	layout->addLayout(tmpl_row);

	// ── Help text ──────────────────────────────────────
	auto *help = new QLabel(tr(
		"<small>"
		"The <b>%</b> character is replaced by the cell value. "
		"For example, if the cell contains <i>308</i> and the template "
		"is <i>Subject %</i>, the result will be <i>Subject 308</i>."
		"</small>"));
	help->setWordWrap(true);
	layout->addWidget(help);

	// ── Column name ────────────────────────────────────
	auto *name_row = new QHBoxLayout;
	name_row->addWidget(new QLabel(tr("New column name:")));
	m_name_edit = new QLineEdit;
	m_name_edit->setText(column_name + QStringLiteral(" (text)"));
	name_row->addWidget(m_name_edit);
	layout->addLayout(name_row);

	// ── Preview table ──────────────────────────────────
	layout->addWidget(new QLabel(tr("Preview:")));
	m_preview = new QTableWidget(samples.size(), 2, this);
	m_preview->setHorizontalHeaderLabels({
		tr("Original (%1)").arg(column_name), tr("Result")
	});
	m_preview->horizontalHeader()->setStretchLastSection(true);
	m_preview->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
	m_preview->verticalHeader()->hide();
	m_preview->setEditTriggers(QAbstractItemView::NoEditTriggers);
	m_preview->setMaximumHeight(230);

	for (int i = 0; i < samples.size(); i++)
	{
		m_preview->setItem(i, 0, new QTableWidgetItem(samples[i]));
		m_preview->setItem(i, 1, new QTableWidgetItem(QString()));
	}

	layout->addWidget(m_preview);

	// ── Buttons ────────────────────────────────────────
	m_buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
	connect(m_buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
	connect(m_buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
	layout->addWidget(m_buttons);

	// ── Connections ────────────────────────────────────
	connect(m_template_edit, &QLineEdit::textChanged, this, &ConvertToTextDialog::updatePreview);

	// Stop auto-naming once the user manually edits the name.
	connect(m_name_edit, &QLineEdit::textEdited, this, [this]() {
		m_auto_name = false;
	});

	// Initial preview.
	updatePreview();
}

QString ConvertToTextDialog::applyTemplate(const QString &tmpl, const QString &value)
{
	QString result = tmpl;
	result.replace(QLatin1Char('%'), value);
	return result;
}

void ConvertToTextDialog::updatePreview()
{
	auto tmpl = m_template_edit->text();
	bool has_placeholder = tmpl.contains(QLatin1Char('%'));

	for (int i = 0; i < m_samples.size(); i++)
	{
		if (tmpl.isEmpty()) {
			m_preview->item(i, 1)->setText(QStringLiteral("\u2014"));
		}
		else {
			m_preview->item(i, 1)->setText(applyTemplate(tmpl, m_samples[i]));
		}
	}

	// Auto-generate name from template (replace % with column name).
	if (m_auto_name)
	{
		if (has_placeholder && tmpl != QStringLiteral("%")) {
			QString name = tmpl;
			name.replace(QLatin1Char('%'), m_column_name);
			m_name_edit->setText(name);
		}
		else {
			m_name_edit->setText(m_column_name + QStringLiteral(" (text)"));
		}
	}

	m_buttons->button(QDialogButtonBox::Ok)->setEnabled(!tmpl.isEmpty());
}

QString ConvertToTextDialog::templateString() const
{
	return m_template_edit->text();
}

QString ConvertToTextDialog::newColumnName() const
{
	return m_name_edit->text().trimmed();
}

} // namespace phonometrica
