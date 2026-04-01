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

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <phon/gui/conc/property_widget.hpp>

namespace phonometrica {

PropertyWidget::PropertyWidget(const String &category, const std::type_info &type, QWidget *parent) :
	QGroupBox(QString::fromUtf8(category.data(), (int) category.size()), parent),
	m_category(category), m_type(type)
{
	if (type == typeid(String))
		setupTextUi();
	else if (type == typeid(double))
		setupNumericUi();
	else if (type == typeid(bool))
		setupBooleanUi();
}

void PropertyWidget::setupTextUi()
{
	auto *layout = new QVBoxLayout(this);
	m_checklist = new QListWidget;
	m_checklist->setMaximumHeight(120);

	// Populate with known values for this category.
	auto values = Property::get_values(m_category);
	for (auto &v : values)
	{
		auto *item = new QListWidgetItem(QString::fromUtf8(v.data(), (int) v.size()));
		item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
		item->setCheckState(Qt::Unchecked);
		m_checklist->addItem(item);
	}

	layout->addWidget(m_checklist);

	connect(m_checklist, &QListWidget::itemChanged, this, &PropertyWidget::modified);
}

void PropertyWidget::setupNumericUi()
{
	auto *layout = new QVBoxLayout(this);
	auto *row = new QHBoxLayout;

	m_numeric_op = new QComboBox;
	m_numeric_op->addItem(tr("(none)"), static_cast<int>(NumericMetaConstraint::Operator::None));
	m_numeric_op->addItem(QStringLiteral("="), static_cast<int>(NumericMetaConstraint::Operator::Equal));
	m_numeric_op->addItem(QStringLiteral("\u2260"), static_cast<int>(NumericMetaConstraint::Operator::NotEqual));
	m_numeric_op->addItem(QStringLiteral("<"), static_cast<int>(NumericMetaConstraint::Operator::Less));
	m_numeric_op->addItem(QStringLiteral("\u2264"), static_cast<int>(NumericMetaConstraint::Operator::LessEqual));
	m_numeric_op->addItem(QStringLiteral(">"), static_cast<int>(NumericMetaConstraint::Operator::Greater));
	m_numeric_op->addItem(QStringLiteral("\u2265"), static_cast<int>(NumericMetaConstraint::Operator::GreaterEqual));
	m_numeric_op->addItem(tr("[a, b]"), static_cast<int>(NumericMetaConstraint::Operator::InclusiveRange));
	m_numeric_op->addItem(tr("(a, b)"), static_cast<int>(NumericMetaConstraint::Operator::ExclusiveRange));

	m_numeric_value1 = new QLineEdit;
	m_numeric_value1->setPlaceholderText(tr("Value"));
	m_numeric_value1->setFixedWidth(80);

	m_numeric_value2 = new QLineEdit;
	m_numeric_value2->setPlaceholderText(tr("Max"));
	m_numeric_value2->setFixedWidth(80);
	m_numeric_value2->setVisible(false);

	row->addWidget(m_numeric_op);
	row->addWidget(m_numeric_value1);
	row->addWidget(m_numeric_value2);
	row->addStretch();
	layout->addLayout(row);

	connect(m_numeric_op, qOverload<int>(&QComboBox::currentIndexChanged), this, [this](int) {
		auto op = static_cast<NumericMetaConstraint::Operator>(m_numeric_op->currentData().toInt());
		bool is_range = (op == NumericMetaConstraint::Operator::InclusiveRange ||
		                 op == NumericMetaConstraint::Operator::ExclusiveRange);
		m_numeric_value2->setVisible(is_range);
		emit modified();
	});
	connect(m_numeric_value1, &QLineEdit::textChanged, this, &PropertyWidget::modified);
	connect(m_numeric_value2, &QLineEdit::textChanged, this, &PropertyWidget::modified);
}

void PropertyWidget::setupBooleanUi()
{
	auto *layout = new QVBoxLayout(this);
	m_bool_combo = new QComboBox;
	m_bool_combo->addItem(tr("(any)"));
	m_bool_combo->addItem(tr("true"));
	m_bool_combo->addItem(tr("false"));
	layout->addWidget(m_bool_combo);

	connect(m_bool_combo, qOverload<int>(&QComboBox::currentIndexChanged), this, &PropertyWidget::modified);
}

bool PropertyWidget::hasSelection() const
{
	if (m_type == typeid(String))
	{
		if (!m_checklist) return false;
		for (int i = 0; i < m_checklist->count(); i++)
		{
			if (m_checklist->item(i)->checkState() == Qt::Checked)
				return true;
		}
		return false;
	}
	else if (m_type == typeid(double))
	{
		if (!m_numeric_op) return false;
		return m_numeric_op->currentData().toInt() != static_cast<int>(NumericMetaConstraint::Operator::None);
	}
	else if (m_type == typeid(bool))
	{
		if (!m_bool_combo) return false;
		return m_bool_combo->currentIndex() > 0;
	}
	return false;
}

AutoMetaConstraint PropertyWidget::buildMetaConstraint() const
{
	if (!hasSelection()) return nullptr;

	if (m_type == typeid(String))
	{
		Array<String> values;
		for (int i = 0; i < m_checklist->count(); i++)
		{
			if (m_checklist->item(i)->checkState() == Qt::Checked)
			{
				auto text = m_checklist->item(i)->text();
				values.append(String(text.toUtf8().constData()));
			}
		}
		return std::make_shared<TextMetaConstraint>(m_category, std::move(values));
	}
	else if (m_type == typeid(double))
	{
		auto op = static_cast<NumericMetaConstraint::Operator>(m_numeric_op->currentData().toInt());
		std::pair<double,double> value{0, 0};
		bool ok;
		value.first = m_numeric_value1->text().toDouble(&ok);
		if (!ok) return nullptr;

		if (op == NumericMetaConstraint::Operator::InclusiveRange ||
		    op == NumericMetaConstraint::Operator::ExclusiveRange)
		{
			value.second = m_numeric_value2->text().toDouble(&ok);
			if (!ok) return nullptr;
		}
		return std::make_shared<NumericMetaConstraint>(m_category, op, value);
	}
	else if (m_type == typeid(bool))
	{
		bool val = (m_bool_combo->currentIndex() == 1);
		return std::make_shared<BooleanMetaConstraint>(m_category, val);
	}

	return nullptr;
}

void PropertyWidget::loadTextValues(const Array<String> &values)
{
	if (!m_checklist) return;
	for (int i = 0; i < m_checklist->count(); i++)
	{
		auto text = m_checklist->item(i)->text();
		String s(text.toUtf8().constData());
		bool found = false;
		for (auto &v : values)
		{
			if (v == s) { found = true; break; }
		}
		m_checklist->item(i)->setCheckState(found ? Qt::Checked : Qt::Unchecked);
	}
}

void PropertyWidget::loadNumericValue(NumericMetaConstraint::Operator op, std::pair<double, double> value)
{
	if (!m_numeric_op) return;
	for (int i = 0; i < m_numeric_op->count(); i++)
	{
		if (m_numeric_op->itemData(i).toInt() == static_cast<int>(op))
		{
			m_numeric_op->setCurrentIndex(i);
			break;
		}
	}
	m_numeric_value1->setText(QString::number(value.first));
	if (op == NumericMetaConstraint::Operator::InclusiveRange ||
	    op == NumericMetaConstraint::Operator::ExclusiveRange)
	{
		m_numeric_value2->setText(QString::number(value.second));
	}
}

void PropertyWidget::loadBoolean(bool value)
{
	if (!m_bool_combo) return;
	m_bool_combo->setCurrentIndex(value ? 1 : 2);
}

} // namespace phonometrica
