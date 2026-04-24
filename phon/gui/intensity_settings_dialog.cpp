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
#include <QMessageBox>
#include <phon/gui/intensity_settings_dialog.hpp>
#include <phon/application/settings.hpp>

namespace phonometrica {

// Default values. Must match Settings::reset_intensity() in settings.cpp.
namespace {
constexpr int    DEFAULT_MIN_DB    = 50;
constexpr int    DEFAULT_MAX_DB    = 100;
constexpr double DEFAULT_TIME_STEP = 0.01;
} // namespace

IntensitySettingsDialog::IntensitySettingsDialog(QWidget *parent) :
	QDialog(parent)
{
	setWindowTitle(tr("Intensity settings"));
	setMinimumWidth(350);
	setWindowFlag(Qt::Tool);

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
	// Layout: [Reset]                [Cancel]  [Apply]  [OK]
	// See formant_settings_dialog.cpp for the full button semantics rationale.
	main_layout->addSpacing(10);
	auto *button_layout = new QHBoxLayout;
	auto *reset_btn = new QPushButton(tr("Reset to defaults"));
	button_layout->addWidget(reset_btn);
	button_layout->addStretch();
	auto *cancel_btn = new QPushButton(tr("Cancel"));
	m_apply_btn = new QPushButton(tr("Apply"));
	m_ok_btn = new QPushButton(tr("OK"));
	m_ok_btn->setDefault(true);
	button_layout->addWidget(cancel_btn);
	button_layout->addWidget(m_apply_btn);
	button_layout->addWidget(m_ok_btn);
	main_layout->addLayout(button_layout);

	// ── Connections ──────────────────────────────────
	connect(reset_btn, &QPushButton::clicked, this, &IntensitySettingsDialog::onResetToDefaults);
	connect(m_apply_btn, &QPushButton::clicked, this, &IntensitySettingsDialog::onApply);
	connect(m_ok_btn, &QPushButton::clicked, this, &IntensitySettingsDialog::onOk);
	connect(cancel_btn, &QPushButton::clicked, this, &IntensitySettingsDialog::onCancel);

	snapshotSettings();
	displayCurrentValues();
}

void IntensitySettingsDialog::onApply()
{
	if (validateAndCommit()) {
		m_snapshot.applied_any = true;
		emit settingsApplied();
	}
}

void IntensitySettingsDialog::onOk()
{
	if (validateAndCommit()) {
		m_snapshot.applied_any = true;
		emit settingsApplied();
		accept();
	}
}

void IntensitySettingsDialog::onCancel()
{
	reject();
}

void IntensitySettingsDialog::onResetToDefaults()
{
	displayDefaultValues();
}

void IntensitySettingsDialog::reject()
{
	if (restoreSnapshot()) {
		emit settingsApplied();
	}
	QDialog::reject();
}

bool IntensitySettingsDialog::validateAndCommit()
{
	bool ok;
	String category("intensity");

	// ── Minimum intensity ────────────────────────────
	String text_min(m_min_edit->text().toUtf8().constData());
	auto min_db = text_min.to_int(&ok);
	if (!ok || min_db < 0)
	{
		QMessageBox::critical(this, tr("Invalid setting"), tr("Invalid minimum intensity"));
		m_min_edit->setFocus();
		m_min_edit->selectAll();
		return false;
	}

	// ── Maximum intensity ────────────────────────────
	String text_max(m_max_edit->text().toUtf8().constData());
	auto max_db = text_max.to_int(&ok);
	if (!ok || max_db <= min_db)
	{
		QMessageBox::critical(this, tr("Invalid setting"), tr("Invalid maximum intensity"));
		m_max_edit->setFocus();
		m_max_edit->selectAll();
		return false;
	}

	// ── Time step ────────────────────────────────────
	String text_step(m_step_edit->text().toUtf8().constData());
	auto step = text_step.to_float(&ok);
	if (!ok || step <= 0.0)
	{
		QMessageBox::critical(this, tr("Invalid setting"), tr("Invalid time step"));
		m_step_edit->setFocus();
		m_step_edit->selectAll();
		return false;
	}

	Settings::set_value(category, "minimum_intensity", min_db);
	Settings::set_value(category, "maximum_intensity", max_db);
	Settings::set_value(category, "time_step", step);

	return true;
}

void IntensitySettingsDialog::displayCurrentValues()
{
	String category("intensity");

	auto min_db = (int) Settings::get_number(category, "minimum_intensity");
	m_min_edit->setText(QString::number(min_db));

	auto max_db = (int) Settings::get_number(category, "maximum_intensity");
	m_max_edit->setText(QString::number(max_db));

	auto step = Settings::get_number(category, "time_step");
	m_step_edit->setText(QString::number(step, 'g'));
}

void IntensitySettingsDialog::displayDefaultValues()
{
	m_min_edit->setText(QString::number(DEFAULT_MIN_DB));
	m_max_edit->setText(QString::number(DEFAULT_MAX_DB));
	m_step_edit->setText(QString::number(DEFAULT_TIME_STEP, 'g'));
}

void IntensitySettingsDialog::snapshotSettings()
{
	String category("intensity");
	m_snapshot.min_db      = (int) Settings::get_number(category, "minimum_intensity");
	m_snapshot.max_db      = (int) Settings::get_number(category, "maximum_intensity");
	m_snapshot.time_step   = Settings::get_number(category, "time_step");
	m_snapshot.applied_any = false;
}

bool IntensitySettingsDialog::restoreSnapshot()
{
	if (!m_snapshot.applied_any) return false;

	String category("intensity");
	Settings::set_value(category, "minimum_intensity", intptr_t(m_snapshot.min_db));
	Settings::set_value(category, "maximum_intensity", intptr_t(m_snapshot.max_db));
	Settings::set_value(category, "time_step",         m_snapshot.time_step);
	return true;
}

} // namespace phonometrica
