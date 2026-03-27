/***********************************************************************************************************************
 *                                                                                                                     *
 * Copyright (C) 2019-2026 Julien Eychenne                                                                             *
 *                                                                                                                     *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0. If a copy of the MPL was not   *
 * distributed with this file, You can obtain one at https://mozilla.org/MPL/2.0/.                                     *
 *                                                                                                                     *
 * Created: 27/03/2026                                                                                                 *
 *                                                                                                                     *
 * Purpose: Widget for filtering files by a metadata property in the query editor. Supports text properties (shown as  *
 *          a checkable list of values), numeric properties (operator + value entry), and boolean properties.           *
 *                                                                                                                     *
 ***********************************************************************************************************************/

#ifndef PHONOMETRICA_PROPERTY_WIDGET_HPP
#define PHONOMETRICA_PROPERTY_WIDGET_HPP

#include <QGroupBox>
#include <QComboBox>
#include <QLineEdit>
#include <QListWidget>
#include <phon/application/property.hpp>
#include <phon/application/conc/metaconstraint.hpp>

namespace phonometrica {

class PropertyWidget final : public QGroupBox
{
	Q_OBJECT

public:

	PropertyWidget(const String &category, const std::type_info &type, QWidget *parent = nullptr);

	const String &category() const { return m_category; }

	const std::type_info &type() const { return m_type; }

	// Returns true if the user has made a selection (i.e. this filter is active).
	bool hasSelection() const;

	// Build a metaconstraint from the current UI state. Returns null if no selection.
	AutoMetaConstraint buildMetaConstraint() const;

	// Load values from an existing metaconstraint.
	void loadTextValues(const Array<String> &values);
	void loadNumericValue(NumericMetaConstraint::Operator op, std::pair<double,double> value);
	void loadBoolean(bool value);

signals:

	void modified();

private:

	void setupTextUi();
	void setupNumericUi();
	void setupBooleanUi();

	String m_category;
	const std::type_info &m_type;

	// Text property
	QListWidget *m_checklist = nullptr;

	// Numeric property
	QComboBox *m_numeric_op = nullptr;
	QLineEdit *m_numeric_value1 = nullptr;
	QLineEdit *m_numeric_value2 = nullptr;

	// Boolean property
	QComboBox *m_bool_combo = nullptr;
};

} // namespace phonometrica

#endif // PHONOMETRICA_PROPERTY_WIDGET_HPP
