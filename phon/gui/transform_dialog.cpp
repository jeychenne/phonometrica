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

#include <cmath>
#include <QPushButton>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <phon/gui/transform_dialog.hpp>
#include <phon/gui/help_browser.hpp>
#include <phon/analysis/formula_engine.hpp>

namespace phonometrica {

// Replace standalone occurrences of 'x' in `formula` with `col_name`.
// "standalone" means not preceded/followed by a letter, digit, or underscore.
static QString replaceX(const QString &formula, const QString &col_name)
{
	QString result;
	for (int i = 0; i < formula.size(); i++)
	{
		if (formula[i] == QLatin1Char('x'))
		{
			bool left_ok = (i == 0 ||
				(!formula[i - 1].isLetterOrNumber() && formula[i - 1] != QLatin1Char('_')));
			bool right_ok = (i + 1 >= formula.size() ||
				(!formula[i + 1].isLetterOrNumber() && formula[i + 1] != QLatin1Char('_')));
			if (left_ok && right_ok) {
				result += col_name;
				continue;
			}
		}
		result += formula[i];
	}
	return result;
}

TransformDialog::TransformDialog(const QString &column_name, const QVector<double> &samples, QWidget *parent) :
	QDialog(parent), m_samples(samples), m_column_name(column_name)
{
	setWindowTitle(tr("Transform \"%1\"").arg(column_name));
	setMinimumWidth(450);

	auto *layout = new QVBoxLayout(this);

	// ── Formula input ──────────────────────────────────
	auto *formula_row = new QHBoxLayout;
	formula_row->addWidget(new QLabel(tr("Formula:")));
	m_formula_edit = new QLineEdit;
	m_formula_edit->setPlaceholderText(tr("e.g. log(x), bark(x), x/1000, st(x, 200)"));
	formula_row->addWidget(m_formula_edit);
	layout->addLayout(formula_row);

	// ── Help text ──────────────────────────────────────
	auto *help = new QLabel(tr(
		"<small>"
		"Variable: <b>x</b> &nbsp; Constants: <b>pi</b>, <b>e</b> &nbsp; "
		"Operators: + - * / ^<br>"
		"Math: log, log10, log2, sqrt, abs, exp, pow, round, floor, ceil<br>"
		"Scales: bark, erb, mel, st &nbsp; "
		"(<i>st(x)</i> uses ref=100 Hz; <i>st(x, ref)</i> for custom)"
		"</small>"));
	help->setWordWrap(true);
	layout->addWidget(help);

	// ── Error label ────────────────────────────────────
	m_error_label = new QLabel;
	m_error_label->setStyleSheet("QLabel { color: #cc3333; font-weight: bold; }");
	m_error_label->hide();
	layout->addWidget(m_error_label);

	// ── Column name ────────────────────────────────────
	auto *name_row = new QHBoxLayout;
	name_row->addWidget(new QLabel(tr("New column name:")));
	m_name_edit = new QLineEdit;
	name_row->addWidget(m_name_edit);
	layout->addLayout(name_row);

	// ── Preview table ──────────────────────────────────
	layout->addWidget(new QLabel(tr("Preview:")));
	m_preview = new QTableWidget(samples.size(), 2, this);
	m_preview->setHorizontalHeaderLabels({
		tr("x (%1)").arg(column_name), tr("Result")
	});
	m_preview->horizontalHeader()->setStretchLastSection(true);
	m_preview->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
	m_preview->verticalHeader()->hide();
	m_preview->setEditTriggers(QAbstractItemView::NoEditTriggers);
	m_preview->setMaximumHeight(230);

	for (int i = 0; i < samples.size(); i++)
	{
		auto *orig = new QTableWidgetItem(
			std::isnan(samples[i]) ? QStringLiteral("NaN")
			                       : QString::number(samples[i], 'g', 6));
		m_preview->setItem(i, 0, orig);
		m_preview->setItem(i, 1, new QTableWidgetItem(QStringLiteral("\u2014")));
	}

	layout->addWidget(m_preview);

	// ── Buttons ────────────────────────────────────────
	m_buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel | QDialogButtonBox::Help);
	m_buttons->button(QDialogButtonBox::Ok)->setEnabled(false);
	connect(m_buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
	connect(m_buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
	connect(m_buttons, &QDialogButtonBox::helpRequested, this, [this]() {
		HelpBrowser::showPage(QStringLiteral("transform"), this);
	});
	layout->addWidget(m_buttons);

	// ── Connections ────────────────────────────────────
	connect(m_formula_edit, &QLineEdit::textChanged, this, &TransformDialog::updatePreview);

	// Stop auto-naming once the user manually edits the name.
	connect(m_name_edit, &QLineEdit::textEdited, this, [this]() {
		m_auto_name = false;
	});
}

void TransformDialog::updatePreview()
{
	auto formula_text = m_formula_edit->text().trimmed();

	if (formula_text.isEmpty())
	{
		m_error_label->hide();
		for (int i = 0; i < m_samples.size(); i++)
			m_preview->item(i, 1)->setText(QStringLiteral("\u2014"));
		if (m_auto_name) m_name_edit->clear();
		m_buttons->button(QDialogButtonBox::Ok)->setEnabled(false);
		return;
	}

	auto err = FormulaEngine::validate(formula_text.toStdString());
	if (!err.empty())
	{
		m_error_label->setText(QString::fromStdString(err));
		m_error_label->show();
		for (int i = 0; i < m_samples.size(); i++)
			m_preview->item(i, 1)->setText(QStringLiteral("\u2014"));
		m_buttons->button(QDialogButtonBox::Ok)->setEnabled(false);
		return;
	}

	m_error_label->hide();

	FormulaEngine engine;
	engine.parse(formula_text.toStdString());

	for (int i = 0; i < m_samples.size(); i++)
	{
		double result = engine.evaluate(m_samples[i]);
		m_preview->item(i, 1)->setText(
			std::isnan(result) ? QStringLiteral("NaN")
			                   : QString::number(result, 'g', 6));
	}

	// Auto-generate column name: replace standalone x with column name.
	if (m_auto_name) {
		m_name_edit->setText(replaceX(formula_text, m_column_name));
	}

	m_buttons->button(QDialogButtonBox::Ok)->setEnabled(true);
}

QString TransformDialog::formula() const
{
	return m_formula_edit->text().trimmed();
}

QString TransformDialog::newColumnName() const
{
	return m_name_edit->text().trimmed();
}

} // namespace phonometrica
