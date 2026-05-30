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
 * Purpose: Dialog for building and executing protocol-based queries on annotations. The protocol defines a set of     *
 *          search fields with checkboxes; the user's checkbox selections are translated into a regex pattern that is   *
 *          passed as a single search constraint.                                                                      *
 *                                                                                                                     *
 ***********************************************************************************************************************/

#ifndef PHONOMETRICA_PROTOCOL_QUERY_EDITOR_HPP
#define PHONOMETRICA_PROTOCOL_QUERY_EDITOR_HPP

#include <QDialog>
#include <QLineEdit>
#include <QComboBox>
#include <QRadioButton>
#include <QSpinBox>
#include <QCheckBox>
#include <QListWidget>
#include <QPushButton>
#include <phon/application/conc/query.hpp>
#include <phon/application/protocol.hpp>
#include <phon/gui/conc/field_widget.hpp>
#include <phon/gui/conc/property_widget.hpp>

namespace phonometrica {

class ProtocolQueryEditor : public QDialog
{
	Q_OBJECT

public:

	explicit ProtocolQueryEditor(AutoProtocol protocol, QWidget *parent = nullptr);

	Handle<Query> query() const { return m_query; }

	Handle<Concordance> concordance() const { return m_concordance; }

private slots:

	void onExecute();
	void onSave();
	void onSaveAs();

private:

	void setupUi();

	QWidget *createSearchPanel();
	QWidget *createContextPanel();
	QWidget *createFileSelector();
	QWidget *createMetadataPanel();
	QWidget *createButtonPanel();

	// Build the query from the current UI state.
	void parseQuery();

	// Assemble the regex pattern from field widgets.
	String buildPattern() const;

	AutoProtocol m_protocol;
	Handle<Query> m_query;
	Handle<Concordance> m_concordance;

	// Header
	QLineEdit *m_name_edit = nullptr;

	// Description filter
	QComboBox *m_desc_op_combo = nullptr;
	QLineEdit *m_desc_edit = nullptr;

	// Protocol fields
	QList<FieldWidget*> m_fields;

	// When checked, after the query executes the protocol is applied to the target
	// column of the resulting concordance, appending one aux column per protocol field
	// with raw codes translated to human-readable labels. Defaults to checked; state is
	// persisted in Settings under ("concordance", "split_protocol_fields").
	QCheckBox *m_split_fields = nullptr;

	// Context
	QRadioButton *m_ctx_none = nullptr;
	QRadioButton *m_ctx_labels = nullptr;
	QRadioButton *m_ctx_kwic = nullptr;
	QRadioButton *m_ctx_event = nullptr;
	QSpinBox *m_ctx_length = nullptr;
	QSpinBox *m_ref_constraint = nullptr;

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

#endif // PHONOMETRICA_PROTOCOL_QUERY_EDITOR_HPP
