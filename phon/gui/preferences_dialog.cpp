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
 * Created: 28/03/2026                                                                                                 *
 *                                                                                                                     *
 * Purpose: see header.                                                                                                *
 *                                                                                                                     *
 ***********************************************************************************************************************/

#include <QTabWidget>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QMessageBox>
#include <phon/gui/file_dialog.hpp>
#include <phon/gui/preferences_dialog.hpp>
#include <phon/gui/font_helpers.hpp>
#include <phon/application/settings.hpp>

namespace phonometrica {

PreferencesDialog::PreferencesDialog(QWidget *parent) :
	QDialog(parent)
{
	setWindowTitle(tr("Preferences"));
	setMinimumWidth(500);

	auto *layout = new QVBoxLayout(this);

	auto *tabs = new QTabWidget;
	tabs->addTab(createGeneralPage(), tr("General"));
	tabs->addTab(createMeasurementPage(), tr("Measurement"));
	tabs->addTab(createAppearancePage(), tr("Appearance"));
	layout->addWidget(tabs);

	// Buttons
	auto *btn_layout = new QHBoxLayout;
	auto *reset_btn = new QPushButton(tr("Reset to defaults"));
	auto *cancel_btn = new QPushButton(tr("Cancel"));
	auto *ok_btn = new QPushButton(tr("OK"));
	ok_btn->setDefault(true);

	btn_layout->addWidget(reset_btn);
	btn_layout->addStretch();
	btn_layout->addWidget(cancel_btn);
	btn_layout->addWidget(ok_btn);
	layout->addLayout(btn_layout);

	connect(ok_btn, &QPushButton::clicked, this, &PreferencesDialog::accept);
	connect(cancel_btn, &QPushButton::clicked, this, &QDialog::reject);
	connect(reset_btn, &QPushButton::clicked, this, &PreferencesDialog::reset);
}

QWidget *PreferencesDialog::createGeneralPage()
{
	auto *page = new QWidget;
	auto *layout = new QVBoxLayout(page);

	m_autoload = new QCheckBox(tr("Load most recent project on startup"));
	m_autoload->setChecked(Settings::get_boolean("autoload"));

	m_restore_views = new QCheckBox(tr("Restore open views on startup"));
	m_restore_views->setChecked(Settings::get_boolean("restore_views"));
	m_restore_views->setEnabled(m_autoload->isChecked());
	connect(m_autoload, &QCheckBox::toggled, m_restore_views, &QCheckBox::setEnabled);

	m_autosave = new QCheckBox(tr("Automatically save project on exit"));
	m_autosave->setChecked(Settings::get_boolean("autosave"));

	m_autohints = new QCheckBox(tr("Activate syntax hints in script views by default"));
	m_autohints->setChecked(Settings::get_boolean("autohints"));

	m_discard_empty = new QCheckBox(tr("Discard empty queries"));
	m_discard_empty->setChecked(Settings::get_boolean("concordance", "discard_empty"));

	m_whisper_log = new QCheckBox(tr("Show whisper transcription logs in output panel"));
	m_whisper_log->setChecked(Settings::get_boolean("whisper_log"));

	layout->addWidget(m_autoload);
	layout->addWidget(m_restore_views);
	layout->addWidget(m_autosave);
	layout->addWidget(m_autohints);
	layout->addWidget(m_discard_empty);
	layout->addWidget(m_whisper_log);

	// ── Praat path ────────────────────────────────────────────────────────
	layout->addSpacing(12);
	layout->addWidget(new QLabel(tr("<b>Praat integration</b>")));

	auto *praat_row = new QHBoxLayout;
	praat_row->addWidget(new QLabel(tr("Path to Praat:")));

	m_praat_path = new QLineEdit;
	m_praat_path->setPlaceholderText(tr("(not configured)"));
	try {
		auto p = Settings::get_string("praat_path");
		m_praat_path->setText(QString::fromUtf8(p.data(), (int) p.size()));
	} catch (...) {}
	m_initial_praat_path = m_praat_path->text();

	auto *browse_btn = new QPushButton(tr("Browse..."));
	connect(browse_btn, &QPushButton::clicked, this, [this]() {
#ifdef Q_OS_WIN
		QString filter = tr("Praat (Praat.exe);;All files (*.*)");
#else
		QString filter = tr("All files (*)");
#endif
		auto path = QFileDialog::getOpenFileName(this, tr("Select Praat executable"), m_praat_path->text(), filter);
		if (!path.isEmpty())
			m_praat_path->setText(path);
	});

	praat_row->addWidget(m_praat_path, 1);
	praat_row->addWidget(browse_btn);
	layout->addLayout(praat_row);

	// ── Statistics ────────────────────────────────────────────────────────
	layout->addSpacing(12);
	layout->addWidget(new QLabel(tr("<b>Statistics</b>")));

	auto *est_row = new QHBoxLayout;
	est_row->addWidget(new QLabel(tr("Default estimation method:")));

	m_estimation_combo = new QComboBox;
	m_estimation_combo->addItem(tr("Frequentist"), QStringLiteral("frequentist"));
	m_estimation_combo->addItem(tr("Bayesian"), QStringLiteral("bayesian"));

	QString current;
	try {
		auto s = Settings::get_string("statistics", "estimation");
		current = QString::fromUtf8(s.data(), (int)s.size());
	} catch (...) {
		current = QStringLiteral("frequentist");
	}
	m_estimation_combo->setCurrentIndex(current == QStringLiteral("bayesian") ? 1 : 0);

	est_row->addWidget(m_estimation_combo);
	est_row->addStretch();
	layout->addLayout(est_row);

	layout->addStretch();

	return page;
}

QWidget *PreferencesDialog::createMeasurementPage()
{
	auto *page = new QWidget;
	auto *layout = new QVBoxLayout(page);

	// ── Default query context ────────────────────────────────────────────
	layout->addWidget(new QLabel(tr("<b>Default query context</b>")));

	m_ctx_none = new QRadioButton(tr("No context"));
	m_ctx_labels = new QRadioButton(tr("Surrounding labels"));
	m_ctx_kwic = new QRadioButton(tr("Number of characters"));

	m_ctx_length = new QSpinBox;
	m_ctx_length->setRange(1, 1000);
	m_ctx_length->setValue(Settings::get_int("concordance", "context_length"));
	m_ctx_length->setToolTip(tr("Number of characters in left/right context"));

	// Read current default
	try {
		auto ctx = Settings::get_string("concordance", "default_context");
		if (ctx == "none") {
			m_ctx_none->setChecked(true);
			m_ctx_length->setEnabled(false);
		} else if (ctx == "labels") {
			m_ctx_labels->setChecked(true);
			m_ctx_length->setEnabled(false);
		} else {
			m_ctx_kwic->setChecked(true);
		}
	} catch (...) {
		m_ctx_kwic->setChecked(true);
	}

	connect(m_ctx_none, &QRadioButton::toggled, this, [this](bool on) {
		if (on) m_ctx_length->setEnabled(false);
	});
	connect(m_ctx_labels, &QRadioButton::toggled, this, [this](bool on) {
		if (on) m_ctx_length->setEnabled(false);
	});
	connect(m_ctx_kwic, &QRadioButton::toggled, this, [this](bool on) {
		if (on) m_ctx_length->setEnabled(true);
	});

	layout->addWidget(m_ctx_none);
	layout->addWidget(m_ctx_labels);

	auto *kwic_row = new QHBoxLayout;
	kwic_row->addWidget(m_ctx_kwic);
	kwic_row->addWidget(m_ctx_length);
	kwic_row->addStretch();
	layout->addLayout(kwic_row);

	// ── Display ──────────────────────────────────────────────────────────
	layout->addSpacing(12);
	layout->addWidget(new QLabel(tr("<b>Display</b>")));

	auto *prec_row = new QHBoxLayout;
	prec_row->addWidget(new QLabel(tr("Decimal places for Hz values:")));
	m_hz_decimals = new QSpinBox;
	m_hz_decimals->setRange(0, 6);
	m_hz_decimals->setToolTip(tr("Number of decimal places for frequency values (formants, pitch, bandwidth).\n"
	                              "0 = round to nearest Hz.\n"
	                              "ERB and Bark values automatically use 2 additional decimal places."));
	try {
		m_hz_decimals->setValue(Settings::get_int("display", "hz_decimals"));
	} catch (...) {
		m_hz_decimals->setValue(0);
	}
	prec_row->addWidget(m_hz_decimals);
	prec_row->addStretch();
	layout->addLayout(prec_row);

	layout->addStretch();

	return page;
}

QWidget *PreferencesDialog::createAppearancePage()
{
	auto *page = new QWidget;
	auto *layout = new QVBoxLayout(page);

	auto *font_layout = new QHBoxLayout;
	font_layout->addWidget(new QLabel(tr("Monospaced (fixed-width) font:")));

	m_font_combo = new QFontComboBox;
	m_font_combo->setFontFilters(QFontComboBox::MonospacedFonts);

	m_font_size = new QSpinBox;
	m_font_size->setRange(6, 36);

	try
	{
		auto name = Settings::get_string("font", "name");
		auto size = (int) Settings::get_number("font", "size");
		m_font_combo->setCurrentFont(QFont(QString::fromUtf8(name.data(), (int) name.size())));
		m_font_size->setValue(size);
	}
	catch (...)
	{
		m_font_combo->setCurrentFont(defaultMonoFont());
		m_font_size->setValue(12);
	}

	font_layout->addWidget(m_font_combo, 1);
	font_layout->addWidget(m_font_size);
	layout->addLayout(font_layout);

	// Capture the initial state so accept() can detect actual changes.
	m_initial_font_name = m_font_combo->currentFont().family();
	m_initial_font_size = m_font_size->value();

	layout->addStretch();

	return page;
}

void PreferencesDialog::accept()
{
	// General
	Settings::set_value("autoload", m_autoload->isChecked());
	Settings::set_value("autosave", m_autosave->isChecked());
	Settings::set_value("autohints", m_autohints->isChecked());
	Settings::set_value("restore_views", m_restore_views->isChecked());
	Settings::set_value("concordance", "discard_empty", m_discard_empty->isChecked());
	Settings::set_value("whisper_log", m_whisper_log->isChecked());

	// Praat path
	auto praat_text = m_praat_path->text().trimmed();
	Settings::set_value("praat_path", String(praat_text.toUtf8().constData()));
	m_praat_path_changed = (praat_text != m_initial_praat_path);

	// Statistics
	Settings::set_value("statistics", "estimation",
	                     String(m_estimation_combo->currentData().toString().toUtf8().constData()));

	// Measurement — default query context
	if (m_ctx_none->isChecked())
		Settings::set_value("concordance", "default_context", String("none"));
	else if (m_ctx_labels->isChecked())
		Settings::set_value("concordance", "default_context", String("labels"));
	else
		Settings::set_value("concordance", "default_context", String("kwic"));
	Settings::set_value("concordance", "context_length", intptr_t(m_ctx_length->value()));

	// Measurement — display
	Settings::set_value("display", "hz_decimals", intptr_t(m_hz_decimals->value()));

	// Appearance — update the font table
	auto font_name = m_font_combo->currentFont().family();
	auto font_size = m_font_size->value();

	Settings::set_value("font", "name", String(font_name.toUtf8().constData()));
	Settings::set_value("font", "size", (intptr_t) font_size);

	// Only show the message if the user actually changed the font in the UI.
	if (font_name != m_initial_font_name || font_size != m_initial_font_size)
	{
		QMessageBox::information(this, tr("Font changed"),
			tr("The font change will take effect when script views are reloaded."));
	}

	QDialog::accept();
}

void PreferencesDialog::reset()
{
	// General
	m_autoload->setChecked(false);
	m_restore_views->setChecked(false);
	m_autosave->setChecked(false);
	m_autohints->setChecked(true);
	m_discard_empty->setChecked(true);
	m_praat_path->clear();

	// Statistics
	m_estimation_combo->setCurrentIndex(0);  // Frequentist

	// Measurement
	m_ctx_kwic->setChecked(true);
	m_ctx_length->setValue(40);
	m_hz_decimals->setValue(0);

	// Appearance — platform defaults
	auto font = defaultMonoFont();
	m_font_combo->setCurrentFont(font);
	m_font_size->setValue(font.pointSize());
}

} // namespace phonometrica
