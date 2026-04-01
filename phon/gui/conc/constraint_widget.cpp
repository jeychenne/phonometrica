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

	// Invisible spacer: same width as the relation combo, shown only on the first
	// constraint when there are 2+ constraints, so that fields align across rows.
	m_spacer = new QWidget;
	m_spacer->setFixedWidth(130);
	m_spacer->setVisible(false);

	// Relation combo (hidden for the first constraint).
	m_relation_combo = new QComboBox;
	m_relation_combo->addItem(tr("dominates"), static_cast<int>(Constraint::Relation::Dominance));
	m_relation_combo->addItem(tr("strictly dominates"), static_cast<int>(Constraint::Relation::StrictDominance));
	m_relation_combo->addItem(tr("is aligned with"), static_cast<int>(Constraint::Relation::Alignment));
	m_relation_combo->addItem(tr("is left aligned with"), static_cast<int>(Constraint::Relation::LeftAlignment));
	m_relation_combo->addItem(tr("is right aligned with"), static_cast<int>(Constraint::Relation::RightAlignment));
	m_relation_combo->addItem(tr("precedes"), static_cast<int>(Constraint::Relation::Precedence));
	m_relation_combo->addItem(tr("follows"), static_cast<int>(Constraint::Relation::Subsequence));
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

	layout->addWidget(m_spacer);
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

void ConstraintWidget::setRelationPlaceholder(bool visible)
{
	m_spacer->setVisible(visible);
}

void ConstraintWidget::focusSearch()
{
	m_search_edit->setFocus();
}

} // namespace phonometrica
