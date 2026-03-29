/***********************************************************************************************************************
 *                                                                                                                     *
 * Copyright (C) 2019-2026 Julien Eychenne                                                                             *
 *                                                                                                                     *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0. If a copy of the MPL was not   *
 * distributed with this file, You can obtain one at https://mozilla.org/MPL/2.0/.                                     *
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

	// Context
	QRadioButton *m_ctx_none = nullptr;
	QRadioButton *m_ctx_labels = nullptr;
	QRadioButton *m_ctx_kwic = nullptr;
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
