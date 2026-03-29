/***********************************************************************************************************************
 *                                                                                                                     *
 * Copyright (C) 2019-2026 Julien Eychenne                                                                             *
 *                                                                                                                     *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0. If a copy of the MPL was not   *
 * distributed with this file, You can obtain one at https://mozilla.org/MPL/2.0/.                                     *
 *                                                                                                                     *
 * Created: 28/03/2026                                                                                                 *
 *                                                                                                                     *
 * Purpose: Widget representing one field in a coding protocol. Each field is a QGroupBox containing a "select all"    *
 *          checkbox and individual value checkboxes. The widget assembles a regex pattern from checked values.         *
 *                                                                                                                     *
 ***********************************************************************************************************************/

#ifndef PHONOMETRICA_FIELD_WIDGET_HPP
#define PHONOMETRICA_FIELD_WIDGET_HPP

#include <QWidget>
#include <QCheckBox>
#include <QGroupBox>
#include <phon/application/protocol.hpp>

namespace phonometrica {

class FieldWidget : public QWidget
{
	Q_OBJECT

public:

	explicit FieldWidget(const SearchField &field, QWidget *parent = nullptr);

	// Returns the regex pattern for this field based on checkbox state.
	// If all or none are checked, returns the field's match_all pattern.
	// Otherwise, joins checked values' match patterns with "|" in a group.
	QString pattern() const;

	// Returns the layer name pattern assembled from checked values (for layer_field support).
	QString layerNamePattern() const;

private slots:

	void onToggleAll(bool checked);

private:

	bool allChecked() const;
	bool noneChecked() const;

	struct ValueEntry {
		QCheckBox *checkbox;
		QString match;       // regex pattern for this value
		QString layer_name;  // layer name for layer_field values
	};

	QString m_match_all;        // field-level match-all pattern
	QString m_layer_pattern;    // field-level layer pattern (for layer_field)
	QList<ValueEntry> m_values;
};

} // namespace phonometrica

#endif // PHONOMETRICA_FIELD_WIDGET_HPP
