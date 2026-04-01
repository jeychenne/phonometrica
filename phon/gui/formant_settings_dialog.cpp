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
 * Created: 24/03/2026                                                                                                 *
 *                                                                                                                     *
 * Purpose: see header.                                                                                                *
 *                                                                                                                     *
 ***********************************************************************************************************************/

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QDialogButtonBox>
#include <QMessageBox>
#include <phon/gui/formant_settings_dialog.hpp>
#include <phon/application/settings.hpp>

namespace phonometrica {

FormantSettingsDialog::FormantSettingsDialog(QWidget *parent) :
	QDialog(parent)
{
	setWindowTitle(tr("Formant settings"));
	setMinimumWidth(350);

	auto *main_layout = new QVBoxLayout(this);

	main_layout->addWidget(new QLabel(tr("Number of visible formants:")));
	m_nformant_edit = new QLineEdit;
	main_layout->addWidget(m_nformant_edit);

	main_layout->addWidget(new QLabel(tr("Maximum frequency (Hz):")));
	m_max_freq_edit = new QLineEdit;
	main_layout->addWidget(m_max_freq_edit);

	main_layout->addWidget(new QLabel(tr("Window length (ms):")));
	m_window_edit = new QLineEdit;
	main_layout->addWidget(m_window_edit);

	main_layout->addWidget(new QLabel(tr("LPC order:")));
	m_lpc_edit = new QLineEdit;
	main_layout->addWidget(m_lpc_edit);

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
	connect(reset_btn, &QPushButton::clicked, this, &FormantSettingsDialog::onReset);
	connect(button_box, &QDialogButtonBox::accepted, this, &FormantSettingsDialog::onOk);
	connect(button_box, &QDialogButtonBox::rejected, this, &QDialog::reject);

	displayValues();
}

void FormantSettingsDialog::onOk()
{
	bool ok;
	String category("formants");

	// ── Number of formants ────────────────────────────
	String text_nf(m_nformant_edit->text().toUtf8().constData());
	auto nformant = text_nf.to_int(&ok);
	if (!ok || nformant <= 0 || nformant > 7)
	{
		QMessageBox::critical(this, tr("Invalid setting"), tr("Invalid number of formants"));
		return;
	}

	// ── Maximum frequency ─────────────────────────────
	String text_freq(m_max_freq_edit->text().toUtf8().constData());
	auto max_freq = text_freq.to_int(&ok);
	if (!ok || max_freq <= 0)
	{
		QMessageBox::critical(this, tr("Invalid setting"), tr("Invalid maximum frequency"));
		return;
	}

	// ── Window length (displayed in ms, stored in s) ──
	String text_win(m_window_edit->text().toUtf8().constData());
	auto win_ms = text_win.to_int(&ok);
	if (!ok || win_ms <= 0)
	{
		QMessageBox::critical(this, tr("Invalid setting"), tr("Invalid window length"));
		return;
	}

	// ── LPC order ─────────────────────────────────────
	String text_lpc(m_lpc_edit->text().toUtf8().constData());
	auto lpc_order = text_lpc.to_int(&ok);
	if (!ok || lpc_order <= nformant)
	{
		QMessageBox::critical(this, tr("Invalid setting"), tr("LPC order must be greater than the number of formants"));
		return;
	}

	// ── Time step ─────────────────────────────────────
	String text_step(m_step_edit->text().toUtf8().constData());
	auto step = text_step.to_float(&ok);
	if (!ok || step <= 0.0)
	{
		QMessageBox::critical(this, tr("Invalid setting"), tr("Invalid time step"));
		return;
	}

	Settings::set_value(category, "number_of_formants", nformant);
	Settings::set_value(category, "window_size", double(win_ms) / 1000.0);
	Settings::set_value(category, "lpc_order", lpc_order);
	Settings::set_value(category, "max_frequency", max_freq);
	Settings::set_value(category, "time_step", step);

	accept();
}

void FormantSettingsDialog::onReset()
{
	Settings::reset_formants();
	displayValues();
}

void FormantSettingsDialog::displayValues()
{
	String category("formants");

	auto nformant = (int) Settings::get_number(category, "number_of_formants");
	m_nformant_edit->setText(QString::number(nformant));

	auto max_freq = (int) Settings::get_number(category, "max_frequency");
	m_max_freq_edit->setText(QString::number(max_freq));

	auto win = Settings::get_number(category, "window_size");
	m_window_edit->setText(QString::number(int(win * 1000)));

	auto lpc_order = (int) Settings::get_number(category, "lpc_order");
	m_lpc_edit->setText(QString::number(lpc_order));

	auto step = Settings::get_number(category, "time_step");
	m_step_edit->setText(QString::number(step, 'g'));
}

} // namespace phonometrica
