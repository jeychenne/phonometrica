/***********************************************************************************************************************
 *                                                                                                                     *
 * Copyright (C) 2019-2026 Julien Eychenne                                                                             *
 *                                                                                                                     *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0. If a copy of the MPL was not   *
 * distributed with this file, You can obtain one at https://mozilla.org/MPL/2.0/.                                     *
 *                                                                                                                     *
 * Created: 28/03/2026                                                                                                 *
 *                                                                                                                     *
 * Purpose: see header.                                                                                                *
 *                                                                                                                     *
 ***********************************************************************************************************************/

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <phon/gui/conc/field_widget.hpp>

namespace phonometrica {

FieldWidget::FieldWidget(const SearchField &field, QWidget *parent) :
	QWidget(parent)
{
	m_match_all = QString::fromUtf8(field.match_all.data(), (int) field.match_all.size());
	m_layer_pattern = QString::fromUtf8(field.layer_pattern.data(), (int) field.layer_pattern.size());

	auto *outer = new QVBoxLayout(this);
	outer->setContentsMargins(0, 0, 0, 0);

	auto name = QString::fromUtf8(field.name.data(), (int) field.name.size());
	auto *group = new QGroupBox(name);
	auto *layout = new QVBoxLayout(group);

	// "Select all" header
	auto *header_layout = new QHBoxLayout;
	header_layout->setContentsMargins(0, 0, 0, 0);
	auto *all_box = new QCheckBox;
	header_layout->addWidget(all_box);
	header_layout->addSpacing(6);
	auto *all_label = new QLabel(QStringLiteral("<b>All values</b>"));
	header_layout->addWidget(all_label);
	header_layout->addStretch();
	layout->addLayout(header_layout);
	layout->addSpacing(4);

	connect(all_box, &QCheckBox::toggled, this, &FieldWidget::onToggleAll);

	// One checkbox per value
	for (auto &v : field.values)
	{
		ValueEntry entry;
		entry.match = QString::fromUtf8(v.match.data(), (int) v.match.size());
		entry.layer_name = QString::fromUtf8(v.layer_name.data(), (int) v.layer_name.size());

		auto text = QString::fromUtf8(v.text.data(), (int) v.text.size());
		entry.checkbox = new QCheckBox(text);
		layout->addWidget(entry.checkbox);

		m_values.append(entry);
	}

	layout->addStretch();
	outer->addWidget(group);
}

void FieldWidget::onToggleAll(bool checked)
{
	for (auto &v : m_values) {
		v.checkbox->setChecked(checked);
	}
}

bool FieldWidget::allChecked() const
{
	// If there's only one value (which may contain sub-choices), treat it as "not all"
	// so that we use the value's own match pattern rather than match_all.
	if (m_values.size() == 1)
		return false;

	for (auto &v : m_values) {
		if (!v.checkbox->isChecked())
			return false;
	}
	return true;
}

bool FieldWidget::noneChecked() const
{
	for (auto &v : m_values) {
		if (v.checkbox->isChecked())
			return false;
	}
	return true;
}

QString FieldWidget::pattern() const
{
	if (allChecked() || noneChecked())
		return QStringLiteral("(%1)").arg(m_match_all);

	QStringList parts;
	for (auto &v : m_values) {
		if (v.checkbox->isChecked())
			parts << v.match;
	}

	if (parts.size() == 1)
		return parts.first();

	return QStringLiteral("(%1)").arg(parts.join(QStringLiteral("|")));
}

QString FieldWidget::layerNamePattern() const
{
	if (allChecked() || noneChecked())
		return m_layer_pattern;

	QStringList names;
	for (auto &v : m_values) {
		if (v.checkbox->isChecked() && !v.layer_name.isEmpty())
			names << v.layer_name;
	}

	return names.join(QStringLiteral("|"));
}

} // namespace phonometrica
