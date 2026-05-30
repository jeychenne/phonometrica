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
 * Created: 17/05/2026                                                                                                 *
 *                                                                                                                     *
 * Purpose: Dialog for voice quality queries. Adds an F0 range and a 14-feature checkbox grid (with "select all" /     *
 *          "select essentials" presets) on top of the standard text query infrastructure. Voice quality is always     *
 *          measured over the whole interval — there is no measurement-location panel.                                  *
 *                                                                                                                     *
 ***********************************************************************************************************************/

#ifndef PHONOMETRICA_VOICE_QUALITY_QUERY_EDITOR_HPP
#define PHONOMETRICA_VOICE_QUALITY_QUERY_EDITOR_HPP

#include <QDialog>
#include <QLineEdit>
#include <QComboBox>
#include <QRadioButton>
#include <QSpinBox>
#include <QCheckBox>
#include <QListWidget>
#include <QPushButton>
#include <QToolButton>
#include <QTableWidget>
#include <QGroupBox>
#include <QLabel>
#include <QVBoxLayout>
#include <phon/application/conc/voice_quality_query.hpp>
#include <phon/gui/conc/constraint_widget.hpp>
#include <phon/gui/conc/property_widget.hpp>
#include <phon/hashmap.hpp>

namespace phonometrica {

class VoiceQualityQueryEditor : public QDialog
{
	Q_OBJECT

public:

	explicit VoiceQualityQueryEditor(QWidget *parent = nullptr);
	explicit VoiceQualityQueryEditor(Handle<VoiceQualityQuery> query, QWidget *parent = nullptr);

	Handle<VoiceQualityQuery> query() const { return m_query; }
	Handle<Concordance> concordance() const { return m_concordance; }

private slots:

	void onExecute();
	void onSave();
	void onSaveAs();
	void onAddConstraint();
	void onRemoveConstraint();
	void onSelectAll();
	void onSelectDefault();

private:

	void setupUi();
	QWidget *createSearchPanel();
	QWidget *createVoiceQualitySettingsPanel();
	QWidget *createContextPanel();
	QWidget *createMetadataPanel();
	QWidget *createFileSelector();
	QWidget *createButtonPanel();
	void parseQuery();
	bool validateQuery();
	void loadQuery();

	// Override-section internals
	QWidget *buildOverrideSection();        // panel embedded below the F0-range row
	void refreshOverrideCategoryCombo();
	void refreshOverrideTable();
	void syncOverrideCellToCache(int row, int col);
	void setOverrideExpanded(bool expanded);
	void applyOverrideEnabledState();       // per-field disable of f0_min / f0_max when fully covered
	void updateOverrideStatus();

	Handle<VoiceQualityQuery> m_query;
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
	QRadioButton *m_ctx_event = nullptr;
	QSpinBox *m_ctx_length = nullptr;
	QSpinBox *m_ref_constraint = nullptr;

	// Duration
	QCheckBox *m_duration_check = nullptr;
	QRadioButton *m_duration_s = nullptr;
	QRadioButton *m_duration_ms = nullptr;

	// Analysis settings (F0 range)
	QLineEdit *m_f0_min_edit = nullptr;
	QLineEdit *m_f0_max_edit = nullptr;

	// Per-property F0-range override (inline disclosure below the F0-range row).
	// Mirrors the pitch editor: the triangle toggles panel visibility only; the
	// override is active iff there are pending entries with at least one
	// non-zero value. The global F0 edits are disabled per-field when fully
	// covered.
	QToolButton  *m_override_triangle      = nullptr;
	QWidget      *m_override_body          = nullptr;
	QComboBox    *m_override_category_combo = nullptr;
	QLabel       *m_override_defaults_lbl  = nullptr;
	QTableWidget *m_override_table         = nullptr;
	QLabel       *m_override_status_lbl    = nullptr;
	QCheckBox    *m_show_params_check      = nullptr;
	Hashmap<String, VoiceQualityQuery::LevelOverride> m_pending_overrides;
	bool m_override_table_updating = false;

	// Feature selection (matching speech::VoiceReport order)
	QCheckBox *m_num_pulses_check       = nullptr;
	QCheckBox *m_voicing_check          = nullptr;
	QCheckBox *m_mean_period_check      = nullptr;
	QCheckBox *m_mean_f0_check          = nullptr;
	QCheckBox *m_jitter_local_check     = nullptr;
	QCheckBox *m_jitter_local_abs_check = nullptr;
	QCheckBox *m_jitter_rap_check       = nullptr;
	QCheckBox *m_jitter_ppq5_check      = nullptr;
	QCheckBox *m_jitter_ddp_check       = nullptr;
	QCheckBox *m_shimmer_local_check    = nullptr;
	QCheckBox *m_shimmer_local_db_check = nullptr;
	QCheckBox *m_shimmer_apq3_check     = nullptr;
	QCheckBox *m_shimmer_apq5_check     = nullptr;
	QCheckBox *m_shimmer_apq11_check    = nullptr;
	QCheckBox *m_hnr_check              = nullptr;

	QListWidget *m_file_list = nullptr;
	Array<PropertyWidget*> m_properties;

	QPushButton *m_save_btn = nullptr;
	QPushButton *m_save_as_btn = nullptr;

	static int s_query_id;
};

} // namespace phonometrica

#endif // PHONOMETRICA_VOICE_QUALITY_QUERY_EDITOR_HPP
