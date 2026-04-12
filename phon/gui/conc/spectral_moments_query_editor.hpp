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
 * Created: 11/04/2026                                                                                                 *
 *                                                                                                                     *
 * Purpose: Dialog for spectral moments queries. Adds analysis parameters (bandwidth, window length/type,              *
 *          pre-emphasis, moment selection) on top of the standard text query infrastructure.                           *
 *                                                                                                                     *
 ***********************************************************************************************************************/

#ifndef PHONOMETRICA_SPECTRAL_MOMENTS_QUERY_EDITOR_HPP
#define PHONOMETRICA_SPECTRAL_MOMENTS_QUERY_EDITOR_HPP

#include <QDialog>
#include <QLineEdit>
#include <QComboBox>
#include <QRadioButton>
#include <QSpinBox>
#include <QCheckBox>
#include <QListWidget>
#include <QPushButton>
#include <QVBoxLayout>
#include <phon/application/conc/spectral_moments_query.hpp>
#include <phon/gui/conc/constraint_widget.hpp>
#include <phon/gui/conc/property_widget.hpp>

namespace phonometrica {

class SpectralMomentsQueryEditor : public QDialog
{
	Q_OBJECT

public:

	explicit SpectralMomentsQueryEditor(QWidget *parent = nullptr);
	explicit SpectralMomentsQueryEditor(Handle<SpectralMomentsQuery> query, QWidget *parent = nullptr);

	Handle<SpectralMomentsQuery> query() const { return m_query; }
	Handle<Concordance> concordance() const { return m_concordance; }

private slots:

	void onExecute();
	void onSave();
	void onSaveAs();
	void onAddConstraint();
	void onRemoveConstraint();

private:

	void setupUi();
	QWidget *createSearchPanel();
	QWidget *createSpectralMomentsSettingsPanel();
	QWidget *createContextPanel();
	QWidget *createMetadataPanel();
	QWidget *createFileSelector();
	QWidget *createButtonPanel();
	void parseQuery();
	bool validateQuery();
	void loadQuery();

	Handle<SpectralMomentsQuery> m_query;
	Handle<Concordance> m_concordance;

	QLineEdit *m_name_edit = nullptr;
	QComboBox *m_desc_op_combo = nullptr;
	QLineEdit *m_desc_edit = nullptr;

	QVBoxLayout *m_constraint_layout = nullptr;
	Array<ConstraintWidget*> m_constraints;
	QPushButton *m_add_btn = nullptr;
	QPushButton *m_remove_btn = nullptr;

	QRadioButton *m_ctx_none = nullptr;
	QRadioButton *m_ctx_labels = nullptr;
	QRadioButton *m_ctx_kwic = nullptr;
	QSpinBox *m_ctx_length = nullptr;
	QSpinBox *m_ref_constraint = nullptr;

	// Duration
	QCheckBox *m_duration_check = nullptr;
	QRadioButton *m_duration_s = nullptr;
	QRadioButton *m_duration_ms = nullptr;

	// Measurement location
	QRadioButton *m_midpoint_radio = nullptr;
	QRadioButton *m_npoint_radio = nullptr;
	QLineEdit *m_npoint_edit = nullptr;

	// Output format
	QRadioButton *m_wide_radio = nullptr;
	QRadioButton *m_long_radio = nullptr;
	QCheckBox *m_average_check = nullptr;

	// Analysis settings
	QLineEdit *m_window_duration_edit = nullptr;
	QComboBox *m_window_type_combo = nullptr;
	QLineEdit *m_min_freq_edit = nullptr;
	QLineEdit *m_max_freq_edit = nullptr;
	QCheckBox *m_preemph_check = nullptr;
	QLineEdit *m_preemph_edit = nullptr;

	// Moment selection
	QCheckBox *m_cog_check = nullptr;
	QCheckBox *m_spread_check = nullptr;
	QCheckBox *m_skewness_check = nullptr;
	QCheckBox *m_kurtosis_check = nullptr;

	QListWidget *m_file_list = nullptr;
	Array<PropertyWidget*> m_properties;

	QPushButton *m_save_btn = nullptr;
	QPushButton *m_save_as_btn = nullptr;

	static int s_query_id;
};

} // namespace phonometrica

#endif // PHONOMETRICA_SPECTRAL_MOMENTS_QUERY_EDITOR_HPP
