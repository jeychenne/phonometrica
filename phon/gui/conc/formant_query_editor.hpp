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
 * Created: 27/03/2026                                                                                                 *
 *                                                                                                                     *
 * Purpose: Dialog for formant queries. Reuses the text query infrastructure (constraints, metadata filters, file      *
 *          selection, context) and adds LPC/formant analysis settings: number of formants, max frequency, window       *
 *          size, LPC order, manual vs. automatic parameter selection (Weenink's method), measurement location          *
 *          (midpoint / n-point average), and optional bandwidth/ERB/Bark output columns.                              *
 *                                                                                                                     *
 ***********************************************************************************************************************/

#ifndef PHONOMETRICA_FORMANT_QUERY_EDITOR_HPP
#define PHONOMETRICA_FORMANT_QUERY_EDITOR_HPP

#include <QDialog>
#include <QLineEdit>
#include <QComboBox>
#include <QRadioButton>
#include <QSpinBox>
#include <QCheckBox>
#include <QListWidget>
#include <QPushButton>
#include <QVBoxLayout>
#include <QStackedWidget>
#include <phon/application/conc/formant_query.hpp>
#include <phon/gui/conc/constraint_widget.hpp>
#include <phon/gui/conc/property_widget.hpp>

namespace phonometrica {

class FormantQueryEditor : public QDialog
{
	Q_OBJECT

public:

	// Create a new formant query from scratch.
	explicit FormantQueryEditor(QWidget *parent = nullptr);

	// Edit an existing formant query.
	explicit FormantQueryEditor(Handle<FormantQuery> query, QWidget *parent = nullptr);

	Handle<FormantQuery> query() const { return m_query; }

	Handle<Concordance> concordance() const { return m_concordance; }

private slots:

	void onExecute();
	void onSave();
	void onSaveAs();
	void onAddConstraint();
	void onRemoveConstraint();

private:

	void setupUi();

	// Panel builders (same structure as QueryEditor + formant settings)
	QWidget *createSearchPanel();
	QWidget *createFormantSettingsPanel();
	QWidget *createContextPanel();
	QWidget *createMetadataPanel();
	QWidget *createFileSelector();
	QWidget *createButtonPanel();

	void parseQuery();
	bool validateQuery();
	void loadQuery();

	Handle<FormantQuery> m_query;
	Handle<Concordance> m_concordance;

	// Header
	QLineEdit *m_name_edit = nullptr;

	// Description filter
	QComboBox *m_desc_op_combo = nullptr;
	QLineEdit *m_desc_edit = nullptr;

	// Search constraints
	QVBoxLayout *m_constraint_layout = nullptr;
	Array<ConstraintWidget*> m_constraints;
	QPushButton *m_add_btn = nullptr;
	QPushButton *m_remove_btn = nullptr;

	// Context
	QRadioButton *m_ctx_none = nullptr;
	QRadioButton *m_ctx_labels = nullptr;
	QRadioButton *m_ctx_kwic = nullptr;
	QSpinBox *m_ctx_length = nullptr;
	QSpinBox *m_ref_constraint = nullptr;

	// Duration
	QCheckBox *m_duration_check = nullptr;
	QRadioButton *m_duration_s = nullptr;
	QRadioButton *m_duration_ms = nullptr;

	// Formant settings — shared
	QSpinBox *m_nformant_spin = nullptr;
	QLineEdit *m_win_size_edit = nullptr;
	QLineEdit *m_max_bw_edit = nullptr;

	// Manual / automatic switch
	QRadioButton *m_manual_radio = nullptr;
	QRadioButton *m_auto_radio = nullptr;
	QStackedWidget *m_method_stack = nullptr;

	// Manual panel
	QLineEdit *m_max_freq_edit = nullptr;
	QSpinBox *m_lpc_order_spin = nullptr;

	// Automatic (Weenink) panel
	QLineEdit *m_auto_freq_low_edit = nullptr;
	QLineEdit *m_auto_freq_high_edit = nullptr;
	QLineEdit *m_auto_freq_step_edit = nullptr;
	QSpinBox *m_auto_lpc_low_spin = nullptr;
	QSpinBox *m_auto_lpc_high_spin = nullptr;

	// Measurement location
	QRadioButton *m_midpoint_radio = nullptr;
	QRadioButton *m_npoint_radio = nullptr;
	QLineEdit *m_npoint_edit = nullptr;

	// N-point output format
	QRadioButton *m_wide_radio = nullptr;
	QRadioButton *m_long_radio = nullptr;
	QCheckBox *m_average_check = nullptr;

	// Output options
	QCheckBox *m_bw_check = nullptr;
	QCheckBox *m_erb_check = nullptr;
	QCheckBox *m_bark_check = nullptr;
	QCheckBox *m_time_check = nullptr;

	// File selection
	QListWidget *m_file_list = nullptr;

	// Properties
	Array<PropertyWidget*> m_properties;

	// Buttons
	QPushButton *m_save_btn = nullptr;
	QPushButton *m_save_as_btn = nullptr;

	static int s_query_id;
};

} // namespace phonometrica

#endif // PHONOMETRICA_FORMANT_QUERY_EDITOR_HPP
