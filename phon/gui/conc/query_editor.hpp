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
 * Purpose: Dialog for building, editing, saving, and executing text queries on annotations. The editor provides       *
 *          search constraints, metadata filters, file selection, and context options.                                 *
 *                                                                                                                     *
 ***********************************************************************************************************************/

#ifndef PHONOMETRICA_QUERY_EDITOR_HPP
#define PHONOMETRICA_QUERY_EDITOR_HPP

#include <QDialog>
#include <QLineEdit>
#include <QComboBox>
#include <QRadioButton>
#include <QCheckBox>
#include <QSpinBox>
#include <QListWidget>
#include <QPushButton>
#include <QVBoxLayout>
#include <phon/application/conc/query.hpp>
#include <phon/gui/conc/constraint_widget.hpp>
#include <phon/gui/conc/property_widget.hpp>

namespace phonometrica {

class QueryEditor : public QDialog
{
	Q_OBJECT

public:

	// Create a new query from scratch.
	explicit QueryEditor(QWidget *parent = nullptr);

	// Edit an existing query.
	explicit QueryEditor(Handle<Query> query, QWidget *parent = nullptr);

	// Returns the query object (populated after accept()).
	Handle<Query> query() const { return m_query; }

	// Returns the concordance resulting from execution (null if not executed).
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
	QWidget *createContextPanel();
	QWidget *createMetadataPanel();
	QWidget *createFileSelector();
	QWidget *createButtonPanel();

	// Build the query from the current UI state.
	void parseQuery();

	// Validate that the query has at least one non-empty search field.
	bool validateQuery();

	// Load values from m_query into the UI (for re-editing).
	void loadQuery();

	Handle<Query> m_query;
	Handle<Concordance> m_concordance;

	// Header
	QLineEdit *m_name_edit = nullptr;

	// Description filter
	QComboBox *m_desc_op_combo = nullptr;
	QLineEdit *m_desc_edit = nullptr;

	// Constraints
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

	// Duration checkbox
	QCheckBox *m_duration_check = nullptr;
	QRadioButton *m_duration_s = nullptr;
	QRadioButton *m_duration_ms = nullptr;

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

#endif // PHONOMETRICA_QUERY_EDITOR_HPP
