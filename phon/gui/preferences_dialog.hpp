/***********************************************************************************************************************
 *                                                                                                                     *
 * Copyright (C) 2019-2026 Julien Eychenne                                                                             *
 *                                                                                                                     *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0. If a copy of the MPL was not   *
 * distributed with this file, You can obtain one at https://mozilla.org/MPL/2.0/.                                     *
 *                                                                                                                     *
 * Created: 28/03/2026                                                                                                 *
 *                                                                                                                     *
 * Purpose: Application preferences dialog with General and Appearance tabs.                                           *
 *                                                                                                                     *
 ***********************************************************************************************************************/

#ifndef PHONOMETRICA_PREFERENCES_DIALOG_HPP
#define PHONOMETRICA_PREFERENCES_DIALOG_HPP

#include <QDialog>
#include <QCheckBox>
#include <QFontComboBox>
#include <QRadioButton>
#include <QSpinBox>

namespace phonometrica {

class PreferencesDialog : public QDialog
{
	Q_OBJECT

public:

	explicit PreferencesDialog(QWidget *parent = nullptr);

private:

	QWidget *createGeneralPage();
	QWidget *createMeasurementPage();
	QWidget *createAppearancePage();

	void accept() override;
	void reset();

	// General
	QCheckBox *m_autoload = nullptr;
	QCheckBox *m_restore_views = nullptr;
	QCheckBox *m_autosave = nullptr;
	QCheckBox *m_autohints = nullptr;
	QCheckBox *m_discard_empty = nullptr;

	// Measurement — display
	QSpinBox *m_hz_decimals = nullptr;

	// Measurement — default query context
	QRadioButton *m_ctx_none = nullptr;
	QRadioButton *m_ctx_labels = nullptr;
	QRadioButton *m_ctx_kwic = nullptr;
	QSpinBox *m_ctx_length = nullptr;

	// Appearance
	QFontComboBox *m_font_combo = nullptr;
	QSpinBox *m_font_size = nullptr;

	// Track initial font state to avoid spurious "font changed" messages.
	QString m_initial_font_name;
	int m_initial_font_size = 0;
};

} // namespace phonometrica

#endif // PHONOMETRICA_PREFERENCES_DIALOG_HPP
