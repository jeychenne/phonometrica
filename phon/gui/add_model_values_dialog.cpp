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
 ***********************************************************************************************************************/

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFormLayout>
#include <QGridLayout>
#include <QLabel>
#include <QRegularExpression>
#include <QPushButton>
#include <QSet>
#include <phon/gui/add_model_values_dialog.hpp>

namespace phonometrica {

AddModelValuesDialog::AddModelValuesDialog(const QString &model_label,
                                            const QString &source_name,
                                            const QStringList &existing_headers,
                                            bool scaled_available,
                                            QWidget *parent) :
	QDialog(parent),
	m_existing_headers(existing_headers),
	m_scaled_available(scaled_available)
{
	setWindowTitle(tr("Add model values to data"));
	setModal(true);

	auto *layout = new QVBoxLayout(this);
	layout->setContentsMargins(12, 12, 12, 12);
	layout->setSpacing(8);

	auto *info = new QLabel(tr("Append per-observation values from <b>%1</b> "
	                            "as new numeric columns to <b>%2</b>. Rows that "
	                            "were excluded from fitting (because of missing "
	                            "values) will receive empty cells.")
	                        .arg(model_label.toHtmlEscaped())
	                        .arg(source_name.toHtmlEscaped()));
	info->setWordWrap(true);
	layout->addWidget(info);

	// Prefix row.
	auto *prefix_row = new QHBoxLayout;
	prefix_row->addWidget(new QLabel(tr("Column prefix:")));
	m_prefix_edit = new QLineEdit;
	QString initial = sanitizePrefix(model_label);
	if (initial.isEmpty()) initial = QStringLiteral("model");
	m_prefix_edit->setText(initial);
	prefix_row->addWidget(m_prefix_edit, 1);
	layout->addLayout(prefix_row);

	// Checkbox + preview grid.
	auto *grid = new QGridLayout;
	grid->setColumnStretch(1, 1);
	grid->setHorizontalSpacing(10);
	grid->setVerticalSpacing(4);

	m_fitted_check = new QCheckBox(tr("Fitted values"));
	m_fitted_check->setChecked(true);
	m_fitted_preview = new QLabel;
	m_fitted_preview->setStyleSheet(QStringLiteral("color: palette(mid);"));
	grid->addWidget(m_fitted_check, 0, 0);
	grid->addWidget(m_fitted_preview, 0, 1);

	m_resid_check = new QCheckBox(tr("Residuals"));
	m_resid_check->setChecked(true);
	m_resid_preview = new QLabel;
	m_resid_preview->setStyleSheet(QStringLiteral("color: palette(mid);"));
	grid->addWidget(m_resid_check, 1, 0);
	grid->addWidget(m_resid_preview, 1, 1);

	m_scaled_check = new QCheckBox(tr("Scaled residuals"));
	m_scaled_check->setChecked(false);
	m_scaled_check->setEnabled(scaled_available);
	if (!scaled_available) {
		m_scaled_check->setToolTip(tr("Scaled residuals could not be computed for this model."));
	}
	m_scaled_preview = new QLabel;
	m_scaled_preview->setStyleSheet(QStringLiteral("color: palette(mid);"));
	grid->addWidget(m_scaled_check, 2, 0);
	grid->addWidget(m_scaled_preview, 2, 1);

	layout->addLayout(grid);

	m_error_label = new QLabel;
	m_error_label->setWordWrap(true);
	m_error_label->setStyleSheet(QStringLiteral("color: #A32D2D;")); // c-red 600
	m_error_label->setVisible(false);
	layout->addWidget(m_error_label);

	layout->addStretch();

	m_buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
	layout->addWidget(m_buttons);

	connect(m_buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
	connect(m_buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

	connect(m_prefix_edit, &QLineEdit::textChanged, this, [this](const QString &) {
		refreshValidation();
	});
	connect(m_fitted_check, &QCheckBox::toggled, this, [this](bool) { refreshValidation(); });
	connect(m_resid_check,  &QCheckBox::toggled, this, [this](bool) { refreshValidation(); });
	connect(m_scaled_check, &QCheckBox::toggled, this, [this](bool) { refreshValidation(); });

	refreshValidation();
}

QString AddModelValuesDialog::prefix() const
{
	return m_prefix_edit->text().trimmed();
}

bool AddModelValuesDialog::wantsFitted() const { return m_fitted_check->isChecked(); }
bool AddModelValuesDialog::wantsResiduals() const { return m_resid_check->isChecked(); }
bool AddModelValuesDialog::wantsScaledResiduals() const
{
	return m_scaled_available && m_scaled_check->isChecked();
}

QString AddModelValuesDialog::fittedColumnName() const
{
	return prefix() + QStringLiteral("_fitted");
}

QString AddModelValuesDialog::residualsColumnName() const
{
	return prefix() + QStringLiteral("_resid");
}

QString AddModelValuesDialog::scaledResidualsColumnName() const
{
	return prefix() + QStringLiteral("_scaled_resid");
}

QString AddModelValuesDialog::sanitizePrefix(const QString &label)
{
	QString trimmed = label.trimmed();
	if (trimmed.isEmpty()) return QString();

	// Replace any run of non-alphanumeric/underscore chars with a single '_'.
	static const QRegularExpression kBad(QStringLiteral("[^A-Za-z0-9_]+"));
	QString s = trimmed;
	s.replace(kBad, QStringLiteral("_"));
	// Strip leading/trailing underscores.
	while (s.startsWith('_')) s.remove(0, 1);
	while (s.endsWith('_'))   s.chop(1);
	return s;
}

void AddModelValuesDialog::refreshValidation()
{
	QString err;
	QString p = prefix();
	QStringList selected_names;

	// Update preview labels (always, regardless of check state).
	m_fitted_preview->setText(QStringLiteral("→ %1").arg(fittedColumnName()));
	m_resid_preview->setText(QStringLiteral("→ %1").arg(residualsColumnName()));
	m_scaled_preview->setText(QStringLiteral("→ %1").arg(scaledResidualsColumnName()));

	// Gather the names that would actually be added.
	if (wantsFitted())           selected_names << fittedColumnName();
	if (wantsResiduals())        selected_names << residualsColumnName();
	if (wantsScaledResiduals())  selected_names << scaledResidualsColumnName();

	if (p.isEmpty())
	{
		err = tr("Column prefix cannot be empty.");
	}
	else if (QRegularExpression(QStringLiteral("[^A-Za-z0-9_]")).match(p).hasMatch())
	{
		err = tr("Column prefix may only contain letters, digits, and underscores.");
	}
	else if (selected_names.isEmpty())
	{
		err = tr("Select at least one value type to add.");
	}
	else
	{
		// Collision checks.
		QSet<QString> existing(m_existing_headers.begin(), m_existing_headers.end());
		QStringList collisions;
		for (const QString &name : selected_names) {
			if (existing.contains(name)) collisions << name;
		}
		if (!collisions.isEmpty())
		{
			err = tr("The source already has a column named %1. "
			         "Change the prefix or uncheck that item.")
			       .arg(collisions.size() == 1
			            ? QStringLiteral("'%1'").arg(collisions.first())
			            : QStringLiteral("'%1'").arg(collisions.join(QStringLiteral("', '"))));
		}
	}

	m_error_label->setText(err);
	m_error_label->setVisible(!err.isEmpty());

	auto *ok = m_buttons->button(QDialogButtonBox::Ok);
	if (ok) ok->setEnabled(err.isEmpty());
}

} // namespace phonometrica
