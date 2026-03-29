/***********************************************************************************************************************
 *                                                                                                                     *
 * Copyright (C) 2019-2026 Julien Eychenne                                                                             *
 *                                                                                                                     *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0. If a copy of the MPL was not   *
 * distributed with this file, You can obtain one at https://mozilla.org/MPL/2.0/.                                     *
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
#include <phon/gui/preferences_dialog.hpp>
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

	layout->addWidget(m_autoload);
	layout->addWidget(m_restore_views);
	layout->addWidget(m_autosave);
	layout->addWidget(m_autohints);
	layout->addWidget(m_discard_empty);
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
		m_font_combo->setCurrentFont(QFont("Monospace"));
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

	// Appearance — platform defaults
#if defined(Q_OS_MACOS)
	m_font_combo->setCurrentFont(QFont("Monaco"));
	m_font_size->setValue(13);
#elif defined(Q_OS_WIN)
	m_font_combo->setCurrentFont(QFont("Consolas"));
	m_font_size->setValue(10);
#else
	m_font_combo->setCurrentFont(QFont("Monospace"));
	m_font_size->setValue(12);
#endif
}

} // namespace phonometrica
