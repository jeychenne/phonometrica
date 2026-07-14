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

#include <QGroupBox>
#include <QScrollArea>
#include <QGridLayout>
#include <QLabel>
#include <QDialogButtonBox>
#include <QMessageBox>
#include <phon/gui/file_dialog.hpp>
#include <QSplitter>
#include <QProgressDialog>
#include <QToolButton>
#include <phon/gui/conc/query_editor.hpp>
#include <phon/gui/help_browser.hpp>
#include <phon/application/project.hpp>
#include <phon/application/settings.hpp>

namespace phonometrica {

int QueryEditor::s_query_id = 0;

QueryEditor::QueryEditor(QWidget *parent) :
	QueryEditor(make_handle<Query>(nullptr, String()), parent)
{
}

QueryEditor::QueryEditor(Handle<Query> query, QWidget *parent) :
	QDialog(parent), m_query(std::move(query))
{
	setWindowTitle(tr("Find in annotations..."));
	setMinimumSize(800, 600);
	setupUi();

	if (!m_query->empty()) {
		loadQuery();
	}

	// Default cursor to the search field, not the query name.
	if (!m_constraints.empty())
		m_constraints.first()->focusSearch();
}

void QueryEditor::setupUi()
{
	auto *main_layout = new QVBoxLayout(this);

	// Name
	auto *header = new QHBoxLayout;
	header->addWidget(new QLabel(tr("Query name:")));
	m_name_edit = new QLineEdit;
	m_name_edit->setPlaceholderText(tr("Query %1").arg(++s_query_id));
	header->addWidget(m_name_edit, 1);

	auto *help_btn = new QToolButton;
	help_btn->setIcon(QIcon(":/icons/circle-help.svg"));
	help_btn->setToolTip(tr("Help"));
	help_btn->setAutoRaise(true);
	connect(help_btn, &QToolButton::clicked, this, [this]() {
		HelpBrowser::showPage(QStringLiteral("concordance"), this);
	});
	header->addWidget(help_btn);

	main_layout->addLayout(header);

	// Central area: splitter with search panel on the left, file selector + properties on the right
	auto *splitter = new QSplitter(Qt::Horizontal);

	// Left: search + context
	auto *left_widget = new QWidget;
	auto *left_layout = new QVBoxLayout(left_widget);
	left_layout->setContentsMargins(0, 0, 0, 0);
	left_layout->addWidget(createSearchPanel());
	left_layout->addWidget(createContextPanel());
	left_layout->addStretch();
	splitter->addWidget(left_widget);

	// Right: file selector + description filter + properties (scrollable)
	auto *right_scroll = new QScrollArea;
	right_scroll->setWidgetResizable(true);
	auto *right_widget = new QWidget;
	auto *right_layout = new QVBoxLayout(right_widget);
	right_layout->setContentsMargins(0, 0, 0, 0);
	right_layout->addWidget(createFileSelector());
	right_layout->addWidget(createMetadataPanel());
	right_layout->addStretch();
	right_scroll->setWidget(right_widget);
	splitter->addWidget(right_scroll);

	splitter->setStretchFactor(0, 3);
	splitter->setStretchFactor(1, 2);
	main_layout->addWidget(splitter, 1);

	// Buttons
	main_layout->addWidget(createButtonPanel());
}

QWidget *QueryEditor::createSearchPanel()
{
	auto *group = new QGroupBox(tr("Search constraints"));
	auto *outer = new QVBoxLayout(group);

	m_constraint_layout = new QVBoxLayout;
	m_constraint_layout->setSpacing(2);

	// First constraint (always present, no relation)
	auto *first = new ConstraintWidget(1, group);
	m_constraints.append(first);
	m_constraint_layout->addWidget(first);
	connect(first, &ConstraintWidget::searchRequested, this, &QueryEditor::onExecute);
	connect(first, &ConstraintWidget::modified, this, [this]() {
		if (m_save_btn) m_save_btn->setEnabled(true);
		if (m_save_as_btn) m_save_as_btn->setEnabled(true);
	});

	outer->addLayout(m_constraint_layout);

	// Add/remove buttons
	auto *btn_layout = new QHBoxLayout;
	btn_layout->addStretch();
	m_add_btn = new QPushButton(QIcon(":/icons/circle-plus.svg"), QString());
	m_add_btn->setFixedSize(28, 28);
	m_add_btn->setToolTip(tr("Add constraint"));
	m_remove_btn = new QPushButton(QIcon(":/icons/circle-minus.svg"), QString());
	m_remove_btn->setFixedSize(28, 28);
	m_remove_btn->setToolTip(tr("Remove last constraint"));
	m_remove_btn->setEnabled(false);
	btn_layout->addWidget(m_add_btn);
	btn_layout->addWidget(m_remove_btn);
	outer->addLayout(btn_layout);

	// Reference constraint (used for context extraction and acoustic measurements)
	auto *ref_layout = new QHBoxLayout;
	ref_layout->addWidget(new QLabel(tr("Reference constraint:")));
	m_ref_constraint = new QSpinBox;
	m_ref_constraint->setRange(1, 1);
	m_ref_constraint->setEnabled(false);
	m_ref_constraint->setToolTip(tr("Constraint used for context extraction and acoustic measurements"));
	ref_layout->addWidget(m_ref_constraint);
	ref_layout->addStretch();

	m_duration_check = new QCheckBox(tr("Add target duration(s)"));
	m_duration_check->setToolTip(tr("Add a column with the duration of each target's event"));
	m_duration_s = new QRadioButton(tr("s"));
	m_duration_ms = new QRadioButton(tr("ms"));
	m_duration_s->setChecked(true);
	m_duration_s->setEnabled(false);
	m_duration_ms->setEnabled(false);
	ref_layout->addWidget(m_duration_check);
	ref_layout->addWidget(m_duration_s);
	ref_layout->addWidget(m_duration_ms);
	connect(m_duration_check, &QCheckBox::toggled, this, [this](bool on) {
		m_duration_s->setEnabled(on);
		m_duration_ms->setEnabled(on);
	});

	outer->addLayout(ref_layout);

	connect(m_add_btn, &QPushButton::clicked, this, &QueryEditor::onAddConstraint);
	connect(m_remove_btn, &QPushButton::clicked, this, &QueryEditor::onRemoveConstraint);

	return group;
}

QWidget *QueryEditor::createContextPanel()
{
	auto *group = new QGroupBox(tr("Context"));
	auto *layout = new QHBoxLayout(group);

	m_ctx_none = new QRadioButton(tr("No context"));
	m_ctx_labels = new QRadioButton(tr("Surrounding labels"));
	m_ctx_event = new QRadioButton(tr("Within event"));
	m_ctx_event->setToolTip(tr("Text to the left and right of the match inside the matched event only"));
	m_ctx_kwic = new QRadioButton(tr("Number of characters"));

	// Apply default context from preferences
	try {
		auto ctx = Settings::get_string("concordance", "default_context");
		if (ctx == "none")
			m_ctx_none->setChecked(true);
		else if (ctx == "labels")
			m_ctx_labels->setChecked(true);
		else if (ctx == "event")
			m_ctx_event->setChecked(true);
		else
			m_ctx_kwic->setChecked(true);
	} catch (...) {
		m_ctx_kwic->setChecked(true);
	}

	m_ctx_length = new QSpinBox;
	m_ctx_length->setRange(1, 1000);
	m_ctx_length->setValue(Settings::get_int("concordance", "context_length"));
	m_ctx_length->setToolTip(tr("Number of characters in left/right context"));

	layout->addWidget(m_ctx_none);
	layout->addWidget(m_ctx_labels);
	layout->addWidget(m_ctx_event);
	layout->addWidget(m_ctx_kwic);
	layout->addWidget(m_ctx_length);
	layout->addStretch();

	connect(m_ctx_none, &QRadioButton::toggled, this, [this](bool on) {
		if (on) { m_ctx_length->setEnabled(false); }
	});
	connect(m_ctx_labels, &QRadioButton::toggled, this, [this](bool on) {
		if (on) { m_ctx_length->setEnabled(false); }
	});
	connect(m_ctx_kwic, &QRadioButton::toggled, this, [this](bool on) {
		if (on) { m_ctx_length->setEnabled(true); }
	});
	connect(m_ctx_event, &QRadioButton::toggled, this, [this](bool on) {
		if (on) { m_ctx_length->setEnabled(false); }
	});

	// Set initial enable state
	m_ctx_length->setEnabled(m_ctx_kwic->isChecked());

	return group;
}

QWidget *QueryEditor::createFileSelector()
{
	auto *group = new QGroupBox(tr("Annotations"));
	auto *layout = new QVBoxLayout(group);

	// Description filter row
	auto *desc_row = new QHBoxLayout;
	desc_row->addWidget(new QLabel(tr("Description")));
	m_desc_op_combo = new QComboBox;
	m_desc_op_combo->addItem(tr("is exactly"));
	m_desc_op_combo->addItem(tr("is not"));
	m_desc_op_combo->addItem(tr("contains"));
	m_desc_op_combo->addItem(tr("doesn't contain"));
	m_desc_op_combo->addItem(tr("matches"));
	m_desc_op_combo->addItem(tr("doesn't match"));
	m_desc_op_combo->setCurrentIndex(2);
	m_desc_op_combo->setFixedWidth(120); // same ballpark as constraint operator combo
	desc_row->addWidget(m_desc_op_combo);
	m_desc_edit = new QLineEdit;
	m_desc_edit->setPlaceholderText(tr("Filter by description..."));
	desc_row->addWidget(m_desc_edit, 1);
	layout->addLayout(desc_row);

	// File list with checkboxes
	m_file_list = new QListWidget;
	m_file_list->setMaximumHeight(180);
	m_file_list->setToolTip(tr("Check specific files, or leave all unchecked to search all files"));

	auto annotations = Project::get()->get_annotations();
	for (auto &annot : annotations)
	{
		auto qlabel = QString::fromUtf8(annot->label().data(), (int) annot->label().size());
		auto *item = new QListWidgetItem(qlabel);
		item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
		item->setCheckState(Qt::Unchecked);
		// Store path as user data so we can look it up later.
		item->setData(Qt::UserRole, QString::fromUtf8(annot->path().data(), (int) annot->path().size()));
		item->setToolTip(item->data(Qt::UserRole).toString());
		m_file_list->addItem(item);
	}

	layout->addWidget(m_file_list);

	return group;
}

QWidget *QueryEditor::createMetadataPanel()
{
	auto *group = new QGroupBox(tr("File properties"));
	auto categories = Property::get_categories();

	if (categories.empty())
	{
		auto *layout = new QVBoxLayout(group);
		auto *label = new QLabel(tr("The current project doesn't have any properties."));
		label->setStyleSheet("color: palette(disabled-text); font-style: italic;");
		layout->addWidget(label);
		return group;
	}

	auto *grid = new QGridLayout(group);
	int col = 0, row = 0;
	const int cols_per_row = 4;

	for (auto &category : categories)
	{
		const std::type_info *type;
		if (Property::is_boolean(category))
			type = &typeid(bool);
		else if (Property::is_numeric(category))
			type = &typeid(double);
		else
			type = &typeid(String);

		auto *pw = new PropertyWidget(category, *type, group);
		m_properties.append(pw);
		grid->addWidget(pw, row, col);
		connect(pw, &PropertyWidget::modified, this, [this]() {
			if (m_save_btn) m_save_btn->setEnabled(true);
			if (m_save_as_btn) m_save_as_btn->setEnabled(true);
		});

		if (++col >= cols_per_row) {
			col = 0;
			++row;
		}
	}

	return group;
}

QWidget *QueryEditor::createButtonPanel()
{
	auto *widget = new QWidget;
	auto *layout = new QHBoxLayout(widget);
	layout->setContentsMargins(0, 4, 0, 0);

	m_save_btn = new QPushButton(tr("Save"));
	m_save_as_btn = new QPushButton(tr("Save as..."));

	auto *cancel_btn = new QPushButton(tr("Cancel"));
	auto *ok_btn = new QPushButton(tr("Search"));
	ok_btn->setDefault(true);

	layout->addWidget(m_save_btn);
	layout->addWidget(m_save_as_btn);
	layout->addStretch();
	layout->addWidget(cancel_btn);
	layout->addWidget(ok_btn);

	connect(ok_btn, &QPushButton::clicked, this, &QueryEditor::onExecute);
	connect(cancel_btn, &QPushButton::clicked, this, &QDialog::reject);
	connect(m_save_btn, &QPushButton::clicked, this, &QueryEditor::onSave);
	connect(m_save_as_btn, &QPushButton::clicked, this, &QueryEditor::onSaveAs);

	return widget;
}

void QueryEditor::parseQuery()
{
	m_query->clear();

	// Name
	auto name = m_name_edit->text().trimmed();
	if (name.isEmpty()) {
		name = m_name_edit->placeholderText();
	}
	m_query->set_label(String(name.toUtf8().constData()), false);

	// Description metaconstraint
	if (!m_desc_edit->text().trimmed().isEmpty())
	{
		auto op = static_cast<DescMetaConstraint::Operator>(m_desc_op_combo->currentIndex());
		auto value = String(m_desc_edit->text().toUtf8().constData());
		m_query->add_metaconstraint(std::make_shared<DescMetaConstraint>(op, std::move(value)), false);
	}

	// Property metaconstraints
	for (auto *pw : m_properties)
	{
		auto mc = pw->buildMetaConstraint();
		if (mc) {
			m_query->add_metaconstraint(std::move(mc), false);
		}
	}

	// File selection
	Array<Handle<Annotation>> annotations;
	for (int i = 0; i < m_file_list->count(); i++)
	{
		if (m_file_list->item(i)->checkState() == Qt::Checked)
		{
			auto path_str = m_file_list->item(i)->data(Qt::UserRole).toString();
			String path(path_str.toUtf8().constData());
			auto vfile = Project::get()->get(path);
			auto annot = recast<Annotation>(vfile);
			if (annot) {
				annotations.append(std::move(annot));
			}
		}
	}
	m_query->set_selection(std::move(annotations));

	// Reference constraint (always set, independent of context type)
	m_query->set_reference_constraint(m_ref_constraint->value());

	// Duration
	m_query->set_include_duration(m_duration_check->isChecked());
	m_query->set_duration_in_ms(m_duration_ms->isChecked());

	// Context
	if (m_ctx_none->isChecked())
	{
		m_query->set_context(Query::Context::None);
	}
	else if (m_ctx_labels->isChecked())
	{
		m_query->set_context(Query::Context::Labels);
	}
	else if (m_ctx_kwic->isChecked())
	{
		m_query->set_context(Query::Context::KWIC);
		m_query->set_context_length(m_ctx_length->value());
		Settings::set_value("concordance", "context_length", intptr_t(m_ctx_length->value()));
	}
	else if (m_ctx_event->isChecked())
	{
		m_query->set_context(Query::Context::WithinEvent);
	}

	// Constraints
	for (auto *cw : m_constraints) {
		m_query->add_constraint(cw->parseConstraint(), false);
	}
}

bool QueryEditor::validateQuery()
{
	for (auto *cw : m_constraints)
	{
		auto c = cw->parseConstraint();
		if (c.target.empty())
		{
			QMessageBox::warning(this, tr("Invalid query"),
				tr("Cannot run query with an empty search field."));
			return false;
		}
	}
	return true;
}

void QueryEditor::onExecute()
{
	if (!validateQuery()) return;

	parseQuery();

	// Progress dialog
	auto *progress = new QProgressDialog(tr("Searching..."), tr("Cancel"), 0, 100, this);
	progress->setWindowModality(Qt::WindowModal);
	progress->setMinimumDuration(500);

	auto conn = m_query->query_progress.connect([progress](int current, int total) {
		if (total > 0) {
			progress->setMaximum(total);
			progress->setValue(current);
		}
		if (progress->wasCanceled()) {
			// The query checks m_cancel_requested on each annotation.
		}
	});

	// Connect cancellation
	connect(progress, &QProgressDialog::canceled, this, [this]() {
		m_query->request_cancel();
	});

	try
	{
		m_concordance = m_query->execute();
		conn.disconnect();
		progress->close();

		if (m_query->modified()) {
			Project::updated();
		}

		accept();
	}
	catch (std::exception &e)
	{
		conn.disconnect();
		progress->close();
		QMessageBox::critical(this, tr("Query error"), QString::fromUtf8(e.what()));
	}
}

void QueryEditor::onSave()
{
	if (m_query->path().empty()) {
		onSaveAs();
		return;
	}
	parseQuery();
	m_query->save();
}

void QueryEditor::onSaveAs()
{
	// Pre-fill the dialog with the user's query name (or its placeholder,
	// e.g. "Query 3") — same fallback chain used by parseQuery() to set the
	// label.  An empty result collapses to "untitled" via defaultSaveName.
	auto name = m_name_edit->text().trimmed();
	if (name.isEmpty()) name = m_name_edit->placeholderText();
	auto suggested = defaultSaveName(String(name.toUtf8().constData()),
	                                 QStringLiteral(".phon-query"));

	auto path = getSaveFileName(this, tr("Save query..."),
		tr("Phonometrica query (*.phon-query)"), suggested);
	if (path.isEmpty()) return;

	bool is_new = m_query->path().empty();

	parseQuery();
	m_query->set_path(String(path.toUtf8().constData()), true);
	m_query->save();

	// Register the query with the project so it appears in the file manager.
	if (is_new) {
		Project::get()->add_query(m_query);
		Project::updated();
	}
}

void QueryEditor::onAddConstraint()
{
	int idx = (int) m_constraints.size() + 1;
	auto *cw = new ConstraintWidget(idx, this);
	cw->setRelationVisible(true);
	m_constraint_layout->addWidget(cw);
	m_constraints.append(cw);
	m_remove_btn->setEnabled(true);
	m_ref_constraint->setRange(1, idx);
	m_ref_constraint->setEnabled(true);

	// Show alignment spacer on the first constraint now that there are siblings.
	m_constraints.first()->setRelationPlaceholder(true);

	connect(cw, &ConstraintWidget::searchRequested, this, &QueryEditor::onExecute);
	connect(cw, &ConstraintWidget::modified, this, [this]() {
		if (m_save_btn) m_save_btn->setEnabled(true);
		if (m_save_as_btn) m_save_as_btn->setEnabled(true);
	});
}

void QueryEditor::onRemoveConstraint()
{
	if (m_constraints.size() <= 1) return;

	auto *cw = m_constraints.take_last();
	m_constraint_layout->removeWidget(cw);
	delete cw;

	m_remove_btn->setEnabled(m_constraints.size() > 1);
	m_ref_constraint->setRange(1, (int) m_constraints.size());
	m_ref_constraint->setEnabled(m_constraints.size() > 1);

	// Hide alignment spacer when back to a single constraint.
	if (m_constraints.size() == 1) {
		m_constraints.first()->setRelationPlaceholder(false);
	}
}

void QueryEditor::loadQuery()
{
	// Name
	auto label = m_query->label();
	if (!label.empty()) {
		m_name_edit->setText(QString::fromUtf8(label.data(), (int) label.size()));
	}

	// Metaconstraints
	for (auto &mc : m_query->metaconstraints())
	{
		if (auto *desc = dynamic_cast<DescMetaConstraint*>(mc.get()))
		{
			m_desc_op_combo->setCurrentIndex(static_cast<int>(desc->op));
			m_desc_edit->setText(QString::fromUtf8(desc->value.data(), (int) desc->value.size()));
		}
		else if (auto *text_mc = dynamic_cast<TextMetaConstraint*>(mc.get()))
		{
			for (auto *pw : m_properties)
			{
				if (pw->category() == text_mc->category && pw->type() == typeid(String)) {
					pw->loadTextValues(text_mc->values);
					break;
				}
			}
		}
		else if (auto *num_mc = dynamic_cast<NumericMetaConstraint*>(mc.get()))
		{
			for (auto *pw : m_properties)
			{
				if (pw->category() == num_mc->category && pw->type() == typeid(double)) {
					pw->loadNumericValue(num_mc->op, num_mc->value);
					break;
				}
			}
		}
		else if (auto *bool_mc = dynamic_cast<BooleanMetaConstraint*>(mc.get()))
		{
			for (auto *pw : m_properties)
			{
				if (pw->category() == bool_mc->category && pw->type() == typeid(bool)) {
					pw->loadBoolean(bool_mc->value);
					break;
				}
			}
		}
	}

	// File selection
	for (auto &file : m_query->selection())
	{
		for (int i = 0; i < m_file_list->count(); i++)
		{
			auto item_path = m_file_list->item(i)->data(Qt::UserRole).toString();
			if (String(item_path.toUtf8().constData()) == file->path())
			{
				m_file_list->item(i)->setCheckState(Qt::Checked);
				break;
			}
		}
	}

	// Constraints (add extras beyond the first)
	intptr_t count = m_query->constraint_count();
	for (intptr_t i = 2; i <= count; i++) {
		onAddConstraint();
	}
	for (intptr_t i = 0; i < count; i++) {
		m_constraints[i]->loadConstraint(m_query->get_constraint(i));
	}

	// Reference constraint (always restore, independent of context type)
	m_ref_constraint->setValue(m_query->reference_constraint());

	// Duration
	m_duration_check->setChecked(m_query->include_duration());
	if (m_query->duration_in_ms()) m_duration_ms->setChecked(true);
	else m_duration_s->setChecked(true);

	// Context
	switch (m_query->context())
	{
		case Query::Context::Labels:
			m_ctx_labels->setChecked(true);
			break;
		case Query::Context::KWIC:
			m_ctx_kwic->setChecked(true);
			m_ctx_length->setValue(m_query->context_length());
			break;
		case Query::Context::WithinEvent:
			m_ctx_event->setChecked(true);
			break;
		default:
			m_ctx_none->setChecked(true);
			break;
	}
}

} // namespace phonometrica
