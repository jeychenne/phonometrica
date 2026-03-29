/***********************************************************************************************************************
 *                                                                                                                     *
 * Copyright (C) 2019-2026 Julien Eychenne                                                                             *
 *                                                                                                                     *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0. If a copy of the MPL was not   *
 * distributed with this file, You can obtain one at https://mozilla.org/MPL/2.0/.                                     *
 *                                                                                                                     *
 * Created: 27/03/2026                                                                                                 *
 *                                                                                                                     *
 * Purpose: Widget representing a single search constraint in the query editor. Each constraint allows the user to     *
 *          specify a layer (by index or pattern), a search operator (equals/contains/matches), a target text, and     *
 *          optionally a relation to the next constraint (for complex queries).                                        *
 *                                                                                                                     *
 ***********************************************************************************************************************/

#ifndef PHONOMETRICA_CONSTRAINT_WIDGET_HPP
#define PHONOMETRICA_CONSTRAINT_WIDGET_HPP

#include <QWidget>
#include <QLineEdit>
#include <QComboBox>
#include <QCheckBox>
#include <QLabel>
#include <phon/application/conc/constraint.hpp>

namespace phonometrica {

class ConstraintWidget final : public QWidget
{
	Q_OBJECT

public:

	explicit ConstraintWidget(int index, QWidget *parent = nullptr);

	// Build a Constraint from the current UI state.
	Constraint parseConstraint() const;

	// Load values from an existing constraint (for re-editing a saved query).
	void loadConstraint(const Constraint &c);

	// Show/hide the relation combo at the left edge.
	void setRelationVisible(bool visible);

	// Move keyboard focus to the search field.
	void focusSearch();

	// Index label (e.g. "#1").
	int index() const { return m_index; }

signals:

	// Emitted when any field in the constraint changes.
	void modified();

	// Emitted when Enter is pressed in the search field.
	void searchRequested();

private:

	void setupUi();

	int m_index;

	QLabel *m_index_label;
	QLineEdit *m_layer_edit;      // layer index or pattern
	QComboBox *m_operator_combo;  // equals, contains, matches
	QLineEdit *m_search_edit;     // target text
	QCheckBox *m_case_checkbox;
	QComboBox *m_relation_combo;  // relation to next constraint
};

} // namespace phonometrica

#endif // PHONOMETRICA_CONSTRAINT_WIDGET_HPP
