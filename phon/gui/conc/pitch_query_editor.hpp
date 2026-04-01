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
 * Created: 29/03/2026                                                                                                 *
 *                                                                                                                     *
 * Purpose: Dialog for pitch queries. Reuses the text query infrastructure (constraints, metadata filters, file        *
 *          selection, context) and adds pitch analysis settings: algorithm, min/max pitch, voicing threshold,          *
 *          measurement location (midpoint / n-point average), and optional semitone/ERB output columns.               *
 *                                                                                                                     *
 ***********************************************************************************************************************/

#ifndef PHONOMETRICA_PITCH_QUERY_EDITOR_HPP
#define PHONOMETRICA_PITCH_QUERY_EDITOR_HPP

#include <QDialog>
#include <QLineEdit>
#include <QComboBox>
#include <QRadioButton>
#include <QSpinBox>
#include <QCheckBox>
#include <QListWidget>
#include <QPushButton>
#include <QVBoxLayout>
#include <phon/application/conc/pitch_query.hpp>
#include <phon/gui/conc/constraint_widget.hpp>
#include <phon/gui/conc/property_widget.hpp>

namespace phonometrica {

class PitchQueryEditor : public QDialog
{
	Q_OBJECT

public:

	// Create a new pitch query from scratch.
	explicit PitchQueryEditor(QWidget *parent = nullptr);

	// Edit an existing pitch query.
	explicit PitchQueryEditor(Handle<PitchQuery> query, QWidget *parent = nullptr);

	Handle<PitchQuery> query() const { return m_query; }

	Handle<Concordance> concordance() const { return m_concordance; }

private slots:

	void onExecute();
	void onSave();
	void onSaveAs();
	void onAddConstraint();
	void onRemoveConstraint();

private:

	void setupUi();

	// Panel builders
	QWidget *createSearchPanel();
	QWidget *createPitchSettingsPanel();
	QWidget *createContextPanel();
	QWidget *createMetadataPanel();
	QWidget *createFileSelector();
	QWidget *createButtonPanel();

	void parseQuery();
	bool validateQuery();
	void loadQuery();

	Handle<PitchQuery> m_query;
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

	// Pitch settings
	QComboBox *m_algorithm_combo = nullptr;
	QLineEdit *m_min_pitch_edit = nullptr;
	QLineEdit *m_max_pitch_edit = nullptr;
	QLineEdit *m_threshold_edit = nullptr;
	QLineEdit *m_time_step_edit = nullptr;

	// Praat-specific (shown/hidden based on algorithm)
	QLabel *m_silence_label = nullptr;
	QLineEdit *m_silence_edit = nullptr;
	QLabel *m_octave_cost_label = nullptr;
	QLineEdit *m_octave_cost_edit = nullptr;
	QLabel *m_octave_jump_label = nullptr;
	QLineEdit *m_octave_jump_edit = nullptr;
	QLabel *m_voicing_cost_label = nullptr;
	QLineEdit *m_voicing_cost_edit = nullptr;

	// Measurement location
	QRadioButton *m_midpoint_radio = nullptr;
	QRadioButton *m_npoint_radio = nullptr;
	QLineEdit *m_npoint_edit = nullptr;

	// N-point output format
	QRadioButton *m_wide_radio = nullptr;
	QRadioButton *m_long_radio = nullptr;
	QCheckBox *m_average_check = nullptr;

	// Output options
	QCheckBox *m_semitones_check = nullptr;
	QLineEdit *m_semitone_ref_edit = nullptr;
	QCheckBox *m_erb_check = nullptr;

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

#endif // PHONOMETRICA_PITCH_QUERY_EDITOR_HPP
