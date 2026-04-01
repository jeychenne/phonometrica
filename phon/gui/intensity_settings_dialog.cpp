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
 * Created: 23/03/2026                                                                                                 *
 *                                                                                                                     *
 * Purpose: see header.                                                                                                *
 *                                                                                                                     *
 ***********************************************************************************************************************/

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QDialogButtonBox>
#include <QMessageBox>
#include <phon/gui/intensity_settings_dialog.hpp>
#include <phon/application/settings.hpp>

namespace phonometrica {

IntensitySettingsDialog::IntensitySettingsDialog(QWidget *parent) :
	QDialog(parent)
{
	setWindowTitle(tr("Intensity settings"));
	setMinimumWidth(350);

	auto *main_layout = new QVBoxLayout(this);

	main_layout->addWidget(new QLabel(tr("Minimum intensity (dB):")));
	m_min_edit = new QLineEdit;
	main_layout->addWidget(m_min_edit);

	main_layout->addWidget(new QLabel(tr("Maximum intensity (dB):")));
	m_max_edit = new QLineEdit;
	main_layout->addWidget(m_max_edit);

	main_layout->addWidget(new QLabel(tr("Time step (s):")));
	m_step_edit = new QLineEdit;
	main_layout->addWidget(m_step_edit);

	// ── Buttons ──────────────────────────────────────
	main_layout->addSpacing(10);
	auto *button_layout = new QHBoxLayout;
	auto *reset_btn = new QPushButton(tr("Reset"));
	button_layout->addWidget(reset_btn);
	button_layout->addStretch();
	auto *button_box = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
	button_layout->addWidget(button_box);
	main_layout->addLayout(button_layout);

	// ── Connections ──────────────────────────────────
	connect(reset_btn, &QPushButton::clicked, this, &IntensitySettingsDialog::onReset);
	connect(button_box, &QDialogButtonBox::accepted, this, &IntensitySettingsDialog::onOk);
	connect(button_box, &QDialogButtonBox::rejected, this, &QDialog::reject);

	displayValues();
}

void IntensitySettingsDialog::onOk()
{
	bool ok;
	String category("intensity");

	// ── Minimum intensity ────────────────────────────
	String text_min(m_min_edit->text().toUtf8().constData());
	auto min_db = text_min.to_int(&ok);
	if (!ok || min_db < 0)
	{
		QMessageBox::critical(this, tr("Invalid setting"), tr("Invalid minimum intensity"));
		return;
	}

	// ── Maximum intensity ────────────────────────────
	String text_max(m_max_edit->text().toUtf8().constData());
	auto max_db = text_max.to_int(&ok);
	if (!ok || max_db <= min_db)
	{
		QMessageBox::critical(this, tr("Invalid setting"), tr("Invalid maximum intensity"));
		return;
	}

	// ── Time step ────────────────────────────────────
	String text_step(m_step_edit->text().toUtf8().constData());
	auto step = text_step.to_float(&ok);
	if (!ok || step <= 0.0)
	{
		QMessageBox::critical(this, tr("Invalid setting"), tr("Invalid time step"));
		return;
	}

	Settings::set_value(category, "minimum_intensity", min_db);
	Settings::set_value(category, "maximum_intensity", max_db);
	Settings::set_value(category, "time_step", step);

	accept();
}

void IntensitySettingsDialog::onReset()
{
	Settings::reset_intensity();
	displayValues();
}

void IntensitySettingsDialog::displayValues()
{
	String category("intensity");

	auto min_db = (int) Settings::get_number(category, "minimum_intensity");
	m_min_edit->setText(QString::number(min_db));

	auto max_db = (int) Settings::get_number(category, "maximum_intensity");
	m_max_edit->setText(QString::number(max_db));

	auto step = Settings::get_number(category, "time_step");
	m_step_edit->setText(QString::number(step, 'g'));
}

} // namespace phonometrica
