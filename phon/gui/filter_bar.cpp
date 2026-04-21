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
 * Created: 02/04/2026                                                                                                 *
 *                                                                                                                     *
 * Purpose: see header.                                                                                                *
 *                                                                                                                     *
 ***********************************************************************************************************************/

#include <QHBoxLayout>
#include <QLabel>
#include <QButtonGroup>
#include <QFrame>
#include <phon/gui/filter_bar.hpp>

namespace phonometrica {

FilterBar::FilterBar(DataFilterProxyModel *proxy, QWidget *parent) :
	QWidget(parent), m_proxy(proxy)
{
	auto *layout = new QVBoxLayout(this);
	layout->setContentsMargins(4, 4, 4, 4);
	layout->setSpacing(3);

	m_strips_layout = new QVBoxLayout;
	m_strips_layout->setSpacing(3);
	layout->addLayout(m_strips_layout);

	// ── Bottom action row: Add rule | AND/OR | Add boolean column ──
	auto *action_row = new QHBoxLayout;
	action_row->setSpacing(8);

	m_add_button = new QPushButton(QIcon(QStringLiteral(":/icons/circle-plus.svg")),
	                               tr("Add filter rule"));
	m_add_button->setFlat(true);
	m_add_button->setCursor(Qt::PointingHandCursor);
	action_row->addWidget(m_add_button);

	// AND/OR radio buttons controlling how rules combine.
	auto *logic_group = new QButtonGroup(this);
	m_and_radio = new QRadioButton(tr("AND"));
	m_or_radio  = new QRadioButton(tr("OR"));
	m_and_radio->setToolTip(tr("A row passes if every rule matches"));
	m_or_radio->setToolTip(tr("A row passes if any rule matches"));
	m_and_radio->setChecked(true);  // default
	logic_group->addButton(m_and_radio);
	logic_group->addButton(m_or_radio);
	action_row->addWidget(m_and_radio);
	action_row->addWidget(m_or_radio);

	// Visual separator between logic radios and the boolean-column button.
	auto *sep = new QFrame;
	sep->setFrameShape(QFrame::VLine);
	sep->setFrameShadow(QFrame::Sunken);
	action_row->addWidget(sep);

	// "Add boolean column" button: generates a 0/1 column reflecting filter matches.
	m_bool_column_button = new QPushButton(QIcon(QStringLiteral(":/icons/circle-plus.svg")),
	                                       tr("Add boolean column"));
	m_bool_column_button->setFlat(true);
	m_bool_column_button->setCursor(Qt::PointingHandCursor);
	m_bool_column_button->setToolTip(tr("Add a 0/1 column marking rows that match the current filter"));
	action_row->addWidget(m_bool_column_button);

	action_row->addStretch();
	layout->addLayout(action_row);

	// ── Connections ────────────────────────────────────
	connect(m_add_button, &QPushButton::clicked, this, &FilterBar::appendStrip);

	connect(m_and_radio, &QRadioButton::toggled, this, &FilterBar::onLogicRadioToggled);
	connect(m_or_radio,  &QRadioButton::toggled, this, &FilterBar::onLogicRadioToggled);

	connect(m_bool_column_button, &QPushButton::clicked,
	        this, &FilterBar::addBooleanColumnRequested);
}

void FilterBar::setColumns(const QStringList &headers, ColumnTypeCallback isNumeric,
                           ColumnLevelsCallback getLevels)
{
	m_headers = headers;
	m_isNumeric = std::move(isNumeric);
	m_getLevels = std::move(getLevels);

	// Update existing strips' column combos.
	for (auto &strip : m_strips) {
		int current = strip.column_combo->currentIndex();
		strip.column_combo->blockSignals(true);
		strip.column_combo->clear();
		strip.column_combo->addItems(m_headers);
		if (current >= 0 && current < m_headers.size())
			strip.column_combo->setCurrentIndex(current);
		strip.column_combo->blockSignals(false);
	}
}

void FilterBar::rebuild()
{
	// Remove all strip widgets.
	while (!m_strips.isEmpty()) {
		auto &s = m_strips.last();
		m_strips_layout->removeWidget(s.container);
		delete s.container;
		m_strips.removeLast();
	}

	// Recreate from proxy rules.
	for (int i = 0; i < m_proxy->ruleCount(); i++) {
		auto strip = createStrip(i);
		auto &rule = m_proxy->rules()[i];

		strip.column_combo->setCurrentIndex(rule.column);

		bool numeric = m_isNumeric ? m_isNumeric(rule.column) : false;
		strip.is_numeric = numeric;
		populateOperators(strip.op_combo, numeric);
		strip.op_combo->setCurrentIndex(opToIndex(rule.op, numeric));

		if (rule.op == FilterOp::InSet) {
			strip.value_edit->hide();
			strip.set_combo->show();
			if (m_getLevels && rule.column >= 0) {
				strip.set_column = rule.column;
				strip.set_combo->setItems(m_getLevels(rule.column));
				if (!rule.set_values.isEmpty()) {
					strip.set_combo->setCheckedItems(rule.set_values);
				}
				else {
					strip.set_combo->checkAll(true);
				}
			}
		}
		else {
			strip.value_edit->setText(rule.value);
			strip.value_edit->show();
			strip.set_combo->hide();
		}

		m_strips_layout->addWidget(strip.container);
		m_strips.append(strip);
	}

	// Connect signals for all rebuilt strips.
	for (int i = 0; i < m_strips.size(); i++) {
		auto &s = m_strips[i];
		connect(s.column_combo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this, i](int) {
			onStripChanged(i);
		});
		connect(s.op_combo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this, i](int) {
			onStripChanged(i);
		});
		connect(s.value_edit, &QLineEdit::textChanged, this, [this, i](const QString &) {
			onStripChanged(i);
		});
		connect(s.set_combo, &CheckableComboBox::checkedItemsChanged, this, [this, i](const QStringList &) {
			onStripChanged(i);
		});
		connect(s.remove_button, &QToolButton::clicked, this, [this, i]() {
			removeStrip(i);
		});
	}

	// Sync radios to the proxy's logic (without emitting logicChanged).
	setLogic(m_proxy->logic());
}

FilterBar::RuleStrip FilterBar::createStrip(int ruleIndex)
{
	Q_UNUSED(ruleIndex)

	RuleStrip strip;
	strip.container = new QWidget;
	auto *row = new QHBoxLayout(strip.container);
	row->setContentsMargins(0, 0, 0, 0);
	row->setSpacing(4);

	// Column selector.
	strip.column_combo = new QComboBox;
	strip.column_combo->addItems(m_headers);
	strip.column_combo->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
	strip.column_combo->setMinimumWidth(100);
	row->addWidget(strip.column_combo);

	// Operator selector.
	strip.op_combo = new QComboBox;
	strip.op_combo->setMinimumWidth(80);
	row->addWidget(strip.op_combo);

	// Value field (text input for most operators).
	strip.value_edit = new QLineEdit;
	strip.value_edit->setPlaceholderText(tr("value"));
	strip.value_edit->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
	strip.value_edit->setMinimumWidth(80);
	row->addWidget(strip.value_edit);

	// Set combo (for InSet operator — hidden by default, multi-select).
	strip.set_combo = new CheckableComboBox;
	strip.set_combo->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
	strip.set_combo->setMinimumWidth(80);
	strip.set_combo->hide();
	row->addWidget(strip.set_combo);

	// Remove button.
	strip.remove_button = new QToolButton;
	strip.remove_button->setIcon(QIcon(QStringLiteral(":/icons/circle-x.svg")));
	strip.remove_button->setToolTip(tr("Remove this filter rule"));
	strip.remove_button->setAutoRaise(true);
	row->addWidget(strip.remove_button);

	// Populate initial operators based on first column's type.
	bool numeric = m_isNumeric && !m_headers.isEmpty() && m_isNumeric(0);
	strip.is_numeric = numeric;
	populateOperators(strip.op_combo, numeric);

	return strip;
}

void FilterBar::appendStrip()
{
	int idx = m_strips.size();

	auto strip = createStrip(idx);
	m_strips_layout->addWidget(strip.container);
	m_strips.append(strip);

	// Add a default rule to the proxy (first column, Eq, empty value).
	FilterRule rule;
	rule.column = 0;
	bool numeric = m_isNumeric && !m_headers.isEmpty() && m_isNumeric(0);
	rule.op = numeric ? FilterOp::Eq : FilterOp::Contains;
	m_proxy->addRule(rule);

	// Connect signals (use the captured index).
	connect(strip.column_combo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this, idx](int) {
		onStripChanged(idx);
	});
	connect(strip.op_combo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this, idx](int) {
		onStripChanged(idx);
	});
	connect(strip.value_edit, &QLineEdit::textChanged, this, [this, idx](const QString &) {
		onStripChanged(idx);
	});
	connect(strip.set_combo, &CheckableComboBox::checkedItemsChanged, this, [this, idx](const QStringList &) {
		onStripChanged(idx);
	});
	connect(strip.remove_button, &QToolButton::clicked, this, [this, idx]() {
		removeStrip(idx);
	});

	emit ruleCountChanged(m_strips.size());
}

void FilterBar::removeStrip(int index)
{
	if (index < 0 || index >= m_strips.size()) return;

	// Remove widget.
	auto &s = m_strips[index];
	m_strips_layout->removeWidget(s.container);
	delete s.container;
	m_strips.removeAt(index);

	// Remove rule from proxy.
	m_proxy->removeRule(index);

	// Rebuild all strips to fix captured indices.
	// (simpler and safer than re-wiring lambdas)
	auto rules = m_proxy->rules();  // copy
	m_proxy->clearRules();

	while (!m_strips.isEmpty()) {
		auto &strip = m_strips.last();
		m_strips_layout->removeWidget(strip.container);
		delete strip.container;
		m_strips.removeLast();
	}

	// Restore rules and rebuild strips.
	for (auto &r : rules) {
		m_proxy->addRule(r);
	}
	rebuild();

	// Reconnect all strips.
	for (int i = 0; i < m_strips.size(); i++) {
		auto &strip = m_strips[i];
		connect(strip.column_combo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this, i](int) {
			onStripChanged(i);
		});
		connect(strip.op_combo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this, i](int) {
			onStripChanged(i);
		});
		connect(strip.value_edit, &QLineEdit::textChanged, this, [this, i](const QString &) {
			onStripChanged(i);
		});
		connect(strip.set_combo, &CheckableComboBox::checkedItemsChanged, this, [this, i](const QStringList &) {
			onStripChanged(i);
		});
		connect(strip.remove_button, &QToolButton::clicked, this, [this, i]() {
			removeStrip(i);
		});
	}

	emit ruleCountChanged(m_strips.size());
}

void FilterBar::onStripChanged(int index)
{
	if (index < 0 || index >= m_strips.size()) return;

	auto &strip = m_strips[index];
	int col = strip.column_combo->currentIndex();

	// Detect column type and repopulate operators if the type changed.
	bool numeric = m_isNumeric && col >= 0 && m_isNumeric(col);

	if (numeric != strip.is_numeric) {
		strip.is_numeric = numeric;
		strip.op_combo->blockSignals(true);
		populateOperators(strip.op_combo, numeric);
		strip.op_combo->setCurrentIndex(0);
		strip.op_combo->blockSignals(false);
	}

	FilterOp op = opFromIndex(strip.op_combo->currentIndex(), strip.is_numeric);

	// Show/hide value edit vs set combo.
	if (op == FilterOp::InSet) {
		strip.value_edit->hide();
		strip.set_combo->show();
		// Only repopulate levels when the column has changed.
		if (m_getLevels && col >= 0 && col != strip.set_column) {
			strip.set_column = col;
			strip.set_combo->blockSignals(true);
			strip.set_combo->setItems(m_getLevels(col));
			// Start empty: nothing checked = no filtering (show all).
			// User picks the levels they want to keep.
			strip.set_combo->blockSignals(false);
		}
	}
	else {
		strip.set_combo->hide();
		strip.value_edit->show();
	}

	// Build rule.
	FilterRule rule;
	rule.column = col;
	rule.op = op;

	if (op == FilterOp::InSet) {
		rule.set_values = strip.set_combo->checkedItems();
		rule.value = rule.set_values.join(QStringLiteral(", "));
	}
	else {
		rule.value = strip.value_edit->text();
	}

	m_proxy->setRule(index, rule);
}

void FilterBar::populateOperators(QComboBox *combo, bool numeric)
{
	combo->clear();

	if (numeric) {
		combo->addItem(QStringLiteral("="));
		combo->addItem(QStringLiteral("\u2260"));    // ≠
		combo->addItem(QStringLiteral("<"));
		combo->addItem(QStringLiteral("\u2264"));    // ≤
		combo->addItem(QStringLiteral(">"));
		combo->addItem(QStringLiteral("\u2265"));    // ≥
	}
	else {
		combo->addItem(tr("equals"));
		combo->addItem(tr("does not equal"));
		combo->addItem(tr("contains"));
		combo->addItem(tr("does not contain"));
		combo->addItem(tr("matches"));
		combo->addItem(tr("is"));
	}
}

FilterOp FilterBar::opFromIndex(int index, bool numeric) const
{
	if (numeric) {
		switch (index) {
		case 0: return FilterOp::Eq;
		case 1: return FilterOp::Ne;
		case 2: return FilterOp::Lt;
		case 3: return FilterOp::Le;
		case 4: return FilterOp::Gt;
		case 5: return FilterOp::Ge;
		default: return FilterOp::Eq;
		}
	}
	else {
		switch (index) {
		case 0: return FilterOp::Eq;
		case 1: return FilterOp::Ne;
		case 2: return FilterOp::Contains;
		case 3: return FilterOp::NotContains;
		case 4: return FilterOp::Regex;
		case 5: return FilterOp::InSet;
		default: return FilterOp::Contains;
		}
	}
}

int FilterBar::opToIndex(FilterOp op, bool numeric) const
{
	if (numeric) {
		switch (op) {
		case FilterOp::Eq: return 0;
		case FilterOp::Ne: return 1;
		case FilterOp::Lt: return 2;
		case FilterOp::Le: return 3;
		case FilterOp::Gt: return 4;
		case FilterOp::Ge: return 5;
		default: return 0;
		}
	}
	else {
		switch (op) {
		case FilterOp::Eq: return 0;
		case FilterOp::Ne: return 1;
		case FilterOp::Contains: return 2;
		case FilterOp::NotContains: return 3;
		case FilterOp::Regex: return 4;
		case FilterOp::InSet: return 5;
		default: return 2;
		}
	}
}

// ─── Logic radio buttons ─────────────────────────────────────────────

void FilterBar::setLogic(FilterLogic logic)
{
	// Set the radios without emitting logicChanged (this is for view-
	// driven restoration after loading the document).
	m_and_radio->blockSignals(true);
	m_or_radio->blockSignals(true);
	if (logic == FilterLogic::Or) {
		m_or_radio->setChecked(true);
	}
	else {
		m_and_radio->setChecked(true);
	}
	m_and_radio->blockSignals(false);
	m_or_radio->blockSignals(false);
}

FilterLogic FilterBar::logic() const
{
	return m_or_radio->isChecked() ? FilterLogic::Or : FilterLogic::And;
}

void FilterBar::onLogicRadioToggled()
{
	// QButtonGroup fires toggled() for both the newly-unchecked and the
	// newly-checked radio; only act on the one that ended up checked to
	// avoid double emission.
	auto *sender_radio = qobject_cast<QRadioButton*>(sender());
	if (!sender_radio || !sender_radio->isChecked()) return;

	emit logicChanged(logic());
}

} // namespace phonometrica
