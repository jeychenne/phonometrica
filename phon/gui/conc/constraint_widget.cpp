/***********************************************************************************************************************
 *                                                                                                                     *
 * Copyright (C) 2019-2026 Julien Eychenne                                                                             *
 *                                                                                                                     *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0. If a copy of the MPL was not   *
 * distributed with this file, You can obtain one at https://mozilla.org/MPL/2.0/.                                     *
 *                                                                                                                     *
 * Created: 27/03/2026                                                                                                 *
 *                                                                                                                     *
 * Purpose: see header.                                                                                                *
 *                                                                                                                     *
 ***********************************************************************************************************************/

#include <QHBoxLayout>
#include <phon/gui/conc/constraint_widget.hpp>

namespace phonometrica {

ConstraintWidget::ConstraintWidget(int index, QWidget *parent) :
	QWidget(parent), m_index(index)
{
	setupUi();
}

void ConstraintWidget::setupUi()
{
	auto *layout = new QHBoxLayout(this);
	layout->setContentsMargins(0, 2, 0, 2);

	// Relation combo (hidden for the first constraint).
	m_relation_combo = new QComboBox;
	m_relation_combo->addItem(tr("dominance"), static_cast<int>(Constraint::Relation::Dominance));
	m_relation_combo->addItem(tr("strict dominance"), static_cast<int>(Constraint::Relation::StrictDominance));
	m_relation_combo->addItem(tr("alignment"), static_cast<int>(Constraint::Relation::Alignment));
	m_relation_combo->addItem(tr("left alignment"), static_cast<int>(Constraint::Relation::LeftAlignment));
	m_relation_combo->addItem(tr("right alignment"), static_cast<int>(Constraint::Relation::RightAlignment));
	m_relation_combo->addItem(tr("precedence"), static_cast<int>(Constraint::Relation::Precedence));
	m_relation_combo->addItem(tr("subsequence"), static_cast<int>(Constraint::Relation::Subsequence));
	m_relation_combo->setToolTip(tr("Relation to the previous constraint"));
	m_relation_combo->setVisible(false);
	m_relation_combo->setFixedWidth(130);

	// Index label.
	m_index_label = new QLabel(QStringLiteral("#%1").arg(m_index));
	m_index_label->setFixedWidth(24);

	// Layer field.
	m_layer_edit = new QLineEdit;
	m_layer_edit->setPlaceholderText(tr("Layer"));
	m_layer_edit->setToolTip(tr("Layer index (0 = all) or name pattern"));
	m_layer_edit->setFixedWidth(80);

	// Operator combo.
	m_operator_combo = new QComboBox;
	m_operator_combo->addItem(tr("equals"), static_cast<int>(Constraint::Operator::Equals));
	m_operator_combo->addItem(tr("contains"), static_cast<int>(Constraint::Operator::Contains));
	m_operator_combo->addItem(tr("matches"), static_cast<int>(Constraint::Operator::Matches));
	m_operator_combo->setCurrentIndex(1); // "contains" by default
	m_operator_combo->setToolTip(tr("Search operator"));

	// Search field.
	m_search_edit = new QLineEdit;
	m_search_edit->setPlaceholderText(tr("Search text or pattern..."));
	m_search_edit->setClearButtonEnabled(true);

	// Case-sensitive checkbox.
	m_case_checkbox = new QCheckBox(tr("Aa"));
	m_case_checkbox->setToolTip(tr("Case sensitive"));

	layout->addWidget(m_relation_combo);
	layout->addWidget(m_index_label);
	layout->addWidget(m_layer_edit);
	layout->addWidget(m_operator_combo);
	layout->addWidget(m_search_edit, 1); // stretch
	layout->addWidget(m_case_checkbox);

	// Forward modification signals.
	connect(m_layer_edit, &QLineEdit::textChanged, this, &ConstraintWidget::modified);
	connect(m_operator_combo, qOverload<int>(&QComboBox::currentIndexChanged), this, &ConstraintWidget::modified);
	connect(m_search_edit, &QLineEdit::textChanged, this, &ConstraintWidget::modified);
	connect(m_case_checkbox, &QCheckBox::toggled, this, &ConstraintWidget::modified);
	connect(m_relation_combo, qOverload<int>(&QComboBox::currentIndexChanged), this, &ConstraintWidget::modified);

	// Enter in search field triggers execution.
	connect(m_search_edit, &QLineEdit::returnPressed, this, &ConstraintWidget::searchRequested);
}

Constraint ConstraintWidget::parseConstraint() const
{
	Constraint c;

	// Operator
	c.op = static_cast<Constraint::Operator>(m_operator_combo->currentData().toInt());

	// Case
	c.case_sensitive = m_case_checkbox->isChecked();

	// Layer
	auto layer_text = m_layer_edit->text().trimmed();
	if (layer_text.isEmpty())
	{
		c.layer_index = 0; // all layers
	}
	else
	{
		bool ok;
		int idx = layer_text.toInt(&ok);
		if (ok)
		{
			c.layer_index = idx;
		}
		else
		{
			c.layer_index = -1; // use pattern
			c.layer_pattern = String(layer_text.toUtf8().constData());
		}
	}

	// Target
	c.target = String(m_search_edit->text().toUtf8().constData());

	// Relation (only meaningful for constraints after the first)
	if (m_relation_combo->isVisible())
	{
		c.relation = static_cast<Constraint::Relation>(m_relation_combo->currentData().toInt());
	}
	else
	{
		c.relation = Constraint::Relation::None;
	}

	return c;
}

void ConstraintWidget::loadConstraint(const Constraint &c)
{
	// Operator
	for (int i = 0; i < m_operator_combo->count(); i++)
	{
		if (m_operator_combo->itemData(i).toInt() == static_cast<int>(c.op))
		{
			m_operator_combo->setCurrentIndex(i);
			break;
		}
	}

	// Case
	m_case_checkbox->setChecked(c.case_sensitive);

	// Layer
	if (c.layer_index == 0)
	{
		m_layer_edit->clear();
	}
	else if (c.layer_index > 0)
	{
		m_layer_edit->setText(QString::number(c.layer_index));
	}
	else
	{
		m_layer_edit->setText(QString::fromUtf8(c.layer_pattern.data(), (int) c.layer_pattern.size()));
	}

	// Target
	m_search_edit->setText(QString::fromUtf8(c.target.data(), (int) c.target.size()));

	// Relation
	if (c.relation != Constraint::Relation::None)
	{
		for (int i = 0; i < m_relation_combo->count(); i++)
		{
			if (m_relation_combo->itemData(i).toInt() == static_cast<int>(c.relation))
			{
				m_relation_combo->setCurrentIndex(i);
				break;
			}
		}
	}
}

void ConstraintWidget::setRelationVisible(bool visible)
{
	m_relation_combo->setVisible(visible);
}

} // namespace phonometrica
