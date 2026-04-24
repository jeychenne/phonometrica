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
#include <QMessageBox>
#include <phon/gui/formant_settings_dialog.hpp>
#include <phon/application/settings.hpp>

namespace phonometrica {

// Default values. Must match Settings::reset_formants() in settings.cpp. We
// duplicate them here because Reset-to-defaults populates the fields without
// touching the Settings registry (Praat semantics) and there's no API on
// Settings to read defaults without committing them.
namespace {
constexpr int    DEFAULT_NFORMANT   = 4;
constexpr int    DEFAULT_MAX_FREQ   = 5500;   // Hz
constexpr int    DEFAULT_WINDOW_MS  = 25;     // 0.025 s
constexpr int    DEFAULT_LPC_ORDER  = 10;
constexpr double DEFAULT_TIME_STEP  = 0.01;   // s
} // namespace

FormantSettingsDialog::FormantSettingsDialog(QWidget *parent) :
	QDialog(parent)
{
	setWindowTitle(tr("Formant settings"));
	setMinimumWidth(350);

	// Qt::Tool makes this a utility window that stays above its parent,
	// doesn't show in the taskbar, and doesn't grab focus from the parent.
	// Combined with non-modal show() in the owner, this matches Praat's
	// settings-dialog UX: tweak values, click Apply, see the effect,
	// iterate without having to reopen the dialog.
	setWindowFlag(Qt::Tool);

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
	// Layout: [Reset]                [Cancel]  [Apply]  [OK]
	// OK is the default (Enter triggers it) and means "apply and close".
	// Apply commits and stays open so the user can iterate (the whole point
	// of this non-modal design). Cancel reverts any Apply clicks made during
	// this session (via the snapshot taken at construction) and closes.
	// Reset only populates the fields with defaults — user still has to
	// click Apply or OK to commit, matching Praat's "Standards" semantics.
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
	connect(reset_btn, &QPushButton::clicked, this, &FormantSettingsDialog::onResetToDefaults);
	connect(m_apply_btn, &QPushButton::clicked, this, &FormantSettingsDialog::onApply);
	connect(m_ok_btn, &QPushButton::clicked, this, &FormantSettingsDialog::onOk);
	connect(cancel_btn, &QPushButton::clicked, this, &FormantSettingsDialog::onCancel);

	// Snapshot BEFORE displayCurrentValues so the snapshot reflects the true
	// pre-dialog state (not whatever the fields show after the user has
	// typed). displayCurrentValues only populates the line edits and doesn't
	// mutate Settings.
	snapshotSettings();
	displayCurrentValues();
}

void FormantSettingsDialog::onApply()
{
	// Only emit on successful commit. On validation failure, validateAndCommit
	// shows an error box and leaves the dialog open so the user can fix the
	// bad field.
	if (validateAndCommit()) {
		m_snapshot.applied_any = true;
		emit settingsApplied();
	}
}

void FormantSettingsDialog::onOk()
{
	// Apply + close. On validation failure, stay open with focus on the bad
	// field (validateAndCommit handles that).
	if (validateAndCommit()) {
		m_snapshot.applied_any = true;
		emit settingsApplied();
		accept();
	}
}

void FormantSettingsDialog::onCancel()
{
	// Delegate to reject() so the Cancel button, the Escape key, and the
	// title-bar close button all take the same revert-and-close path.
	reject();
}

void FormantSettingsDialog::reject()
{
	// If the user clicked Apply one or more times during this session, revert
	// to the values that were active when the dialog opened. If they never
	// Applied, the snapshot matches the current Settings and restore is a
	// no-op.
	if (restoreSnapshot()) {
		emit settingsApplied();
	}
	QDialog::reject();
}

void FormantSettingsDialog::onResetToDefaults()
{
	// Populate fields only. Does NOT write to Settings — the user still has
	// to click Apply to commit, matching Praat's "Standards" button.
	displayDefaultValues();
}

bool FormantSettingsDialog::validateAndCommit()
{
	bool ok;
	String category("formants");

	// ── Number of formants ────────────────────────────
	String text_nf(m_nformant_edit->text().toUtf8().constData());
	auto nformant = text_nf.to_int(&ok);
	if (!ok || nformant <= 0 || nformant > 7)
	{
		QMessageBox::critical(this, tr("Invalid setting"), tr("Invalid number of formants"));
		m_nformant_edit->setFocus();
		m_nformant_edit->selectAll();
		return false;
	}

	// ── Maximum frequency ─────────────────────────────
	String text_freq(m_max_freq_edit->text().toUtf8().constData());
	auto max_freq = text_freq.to_int(&ok);
	if (!ok || max_freq <= 0)
	{
		QMessageBox::critical(this, tr("Invalid setting"), tr("Invalid maximum frequency"));
		m_max_freq_edit->setFocus();
		m_max_freq_edit->selectAll();
		return false;
	}

	// ── Window length (displayed in ms, stored in s) ──
	String text_win(m_window_edit->text().toUtf8().constData());
	auto win_ms = text_win.to_int(&ok);
	if (!ok || win_ms <= 0)
	{
		QMessageBox::critical(this, tr("Invalid setting"), tr("Invalid window length"));
		m_window_edit->setFocus();
		m_window_edit->selectAll();
		return false;
	}

	// ── LPC order ─────────────────────────────────────
	String text_lpc(m_lpc_edit->text().toUtf8().constData());
	auto lpc_order = text_lpc.to_int(&ok);
	if (!ok || lpc_order <= nformant)
	{
		QMessageBox::critical(this, tr("Invalid setting"), tr("LPC order must be greater than the number of formants"));
		m_lpc_edit->setFocus();
		m_lpc_edit->selectAll();
		return false;
	}

	// ── Time step ─────────────────────────────────────
	String text_step(m_step_edit->text().toUtf8().constData());
	auto step = text_step.to_float(&ok);
	if (!ok || step <= 0.0)
	{
		QMessageBox::critical(this, tr("Invalid setting"), tr("Invalid time step"));
		m_step_edit->setFocus();
		m_step_edit->selectAll();
		return false;
	}

	Settings::set_value(category, "number_of_formants", nformant);
	Settings::set_value(category, "window_size", double(win_ms) / 1000.0);
	Settings::set_value(category, "lpc_order", lpc_order);
	Settings::set_value(category, "max_frequency", max_freq);
	Settings::set_value(category, "time_step", step);

	return true;
}

void FormantSettingsDialog::displayCurrentValues()
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

void FormantSettingsDialog::displayDefaultValues()
{
	m_nformant_edit->setText(QString::number(DEFAULT_NFORMANT));
	m_max_freq_edit->setText(QString::number(DEFAULT_MAX_FREQ));
	m_window_edit->setText(QString::number(DEFAULT_WINDOW_MS));
	m_lpc_edit->setText(QString::number(DEFAULT_LPC_ORDER));
	m_step_edit->setText(QString::number(DEFAULT_TIME_STEP, 'g'));
}

void FormantSettingsDialog::snapshotSettings()
{
	String category("formants");
	m_snapshot.nformant    = (int) Settings::get_number(category, "number_of_formants");
	m_snapshot.max_freq    = (int) Settings::get_number(category, "max_frequency");
	m_snapshot.window_size = Settings::get_number(category, "window_size");
	m_snapshot.lpc_order   = (int) Settings::get_number(category, "lpc_order");
	m_snapshot.time_step   = Settings::get_number(category, "time_step");
	m_snapshot.applied_any = false;
}

bool FormantSettingsDialog::restoreSnapshot()
{
	if (!m_snapshot.applied_any) return false;

	String category("formants");
	Settings::set_value(category, "number_of_formants", intptr_t(m_snapshot.nformant));
	Settings::set_value(category, "max_frequency",      intptr_t(m_snapshot.max_freq));
	Settings::set_value(category, "window_size",        m_snapshot.window_size);
	Settings::set_value(category, "lpc_order",          intptr_t(m_snapshot.lpc_order));
	Settings::set_value(category, "time_step",          m_snapshot.time_step);
	return true;
}

} // namespace phonometrica
