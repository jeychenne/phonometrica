/***********************************************************************************************************************
 *                                                                                                                     *
 * Copyright (C) 2019-2026 Julien Eychenne                                                                             *
 *                                                                                                                     *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0. If a copy of the MPL was not   *
 * distributed with this file, You can obtain one at https://mozilla.org/MPL/2.0/.                                     *
 *                                                                                                                     *
 * Created: 29/03/2026                                                                                                 *
 *                                                                                                                     *
 * Purpose: Dialog for intensity queries.                                                                              *
 *                                                                                                                     *
 ***********************************************************************************************************************/

#ifndef PHONOMETRICA_INTENSITY_QUERY_EDITOR_HPP
#define PHONOMETRICA_INTENSITY_QUERY_EDITOR_HPP

#include <QDialog>
#include <QLineEdit>
#include <QComboBox>
#include <QRadioButton>
#include <QSpinBox>
#include <QCheckBox>
#include <QListWidget>
#include <QPushButton>
#include <QVBoxLayout>
#include <phon/application/conc/intensity_query.hpp>
#include <phon/gui/conc/constraint_widget.hpp>
#include <phon/gui/conc/property_widget.hpp>

namespace phonometrica {

class IntensityQueryEditor : public QDialog
{
	Q_OBJECT

public:

	explicit IntensityQueryEditor(QWidget *parent = nullptr);
	explicit IntensityQueryEditor(Handle<IntensityQuery> query, QWidget *parent = nullptr);

	Handle<IntensityQuery> query() const { return m_query; }
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
	QWidget *createIntensitySettingsPanel();
	QWidget *createContextPanel();
	QWidget *createMetadataPanel();
	QWidget *createFileSelector();
	QWidget *createButtonPanel();
	void parseQuery();
	bool validateQuery();
	void loadQuery();

	Handle<IntensityQuery> m_query;
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

	QRadioButton *m_midpoint_radio = nullptr;
	QRadioButton *m_npoint_radio = nullptr;
	QLineEdit *m_npoint_edit = nullptr;

	QRadioButton *m_wide_radio = nullptr;
	QRadioButton *m_long_radio = nullptr;
	QCheckBox *m_average_check = nullptr;

	QListWidget *m_file_list = nullptr;
	Array<PropertyWidget*> m_properties;

	QPushButton *m_save_btn = nullptr;
	QPushButton *m_save_as_btn = nullptr;

	static int s_query_id;
};

} // namespace phonometrica

#endif // PHONOMETRICA_INTENSITY_QUERY_EDITOR_HPP
