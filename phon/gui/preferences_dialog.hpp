/***********************************************************************************************************************
 *                                                                                                                     *
 * Copyright (C) 2019-2026 Julien Eychenne                                                                             *
 *                                                                                                                     *
 * This program is free software: you can redistribute it and/or modify it under the terms of the GNU General Public   *
 * License as published by the Free Software Foundation, either version 2 of the License, or (at your option) any      *
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
