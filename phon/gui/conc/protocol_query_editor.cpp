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

#include <QGroupBox>
#include <QScrollArea>
#include <QGridLayout>
#include <QLabel>
#include <QMessageBox>
#include <QFileDialog>
#include <QSplitter>
#include <QProgressDialog>
#include <QToolButton>
#include <phon/gui/conc/protocol_query_editor.hpp>
#include <phon/gui/help_browser.hpp>
#include <phon/application/project.hpp>
#include <phon/application/settings.hpp>

namespace phonometrica {

int ProtocolQueryEditor::s_query_id = 0;

ProtocolQueryEditor::ProtocolQueryEditor(AutoProtocol protocol, QWidget *parent) :
	QDialog(parent),
	m_protocol(std::move(protocol)),
	m_query(make_handle<Query>(nullptr, String()))
{
	auto name = m_protocol->name();
	setWindowTitle(QString::fromUtf8(name.data(), (int) name.size()));
	setMinimumSize(800, 600);
	setupUi();
}

void ProtocolQueryEditor::setupUi()
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

	// Left: search (protocol fields) + context
	auto *left_scroll = new QScrollArea;
	left_scroll->setWidgetResizable(true);
	auto *left_widget = new QWidget;
	auto *left_layout = new QVBoxLayout(left_widget);
	left_layout->setContentsMargins(0, 0, 0, 0);
	left_layout->addWidget(createSearchPanel());
	left_layout->addWidget(createContextPanel());
	left_layout->addStretch();
	left_scroll->setWidget(left_widget);
	splitter->addWidget(left_scroll);

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


// ---------------------------------------------------------
//  Search panel: protocol field widgets
// ---------------------------------------------------------

QWidget *ProtocolQueryEditor::createSearchPanel()
{
	auto protocol_name = m_protocol->name();
	auto title = QString::fromUtf8(protocol_name.data(), (int) protocol_name.size());
	auto *group = new QGroupBox(title);
	auto *grid = new QGridLayout(group);
	grid->setContentsMargins(6, 10, 6, 6);
	grid->setSpacing(4);

	auto &fields = m_protocol->fields();
	int cols_per_row = m_protocol->fields_per_row();
	int col = 0, row = 0;

	for (intptr_t i = 1; i <= fields.size(); i++)
	{
		auto *fw = new FieldWidget(fields[i], group);
		grid->addWidget(fw, row, col);
		m_fields.append(fw);

		if (++col >= cols_per_row) {
			col = 0;
			++row;
		}
	}

	return group;
}


// ---------------------------------------------------------
//  Context panel (same as QueryEditor)
// ---------------------------------------------------------

QWidget *ProtocolQueryEditor::createContextPanel()
{
	auto *group = new QGroupBox(tr("Context"));
	auto *layout = new QHBoxLayout(group);

	auto *ref_label = new QLabel(tr("Reference constraint:"));
	m_ref_constraint = new QSpinBox;
	m_ref_constraint->setRange(1, 1);
	m_ref_constraint->setToolTip(tr("Constraint for which context is extracted"));

	m_ctx_none = new QRadioButton(tr("No context"));
	m_ctx_labels = new QRadioButton(tr("Surrounding labels"));
	m_ctx_kwic = new QRadioButton(tr("Number of characters"));
	m_ctx_kwic->setChecked(true);

	m_ctx_length = new QSpinBox;
	m_ctx_length->setRange(1, 1000);
	m_ctx_length->setValue(Settings::get_int("concordance", "context_length"));
	m_ctx_length->setToolTip(tr("Number of characters in left/right context"));

	layout->addWidget(ref_label);
	layout->addWidget(m_ref_constraint);
	layout->addSpacing(10);
	layout->addWidget(m_ctx_none);
	layout->addWidget(m_ctx_labels);
	layout->addWidget(m_ctx_kwic);
	layout->addWidget(m_ctx_length);
	layout->addStretch();

	connect(m_ctx_none, &QRadioButton::toggled, this, [this](bool on) {
		if (on) { m_ctx_length->setEnabled(false); m_ref_constraint->setEnabled(false); }
	});
	connect(m_ctx_labels, &QRadioButton::toggled, this, [this](bool on) {
		if (on) { m_ctx_length->setEnabled(false); m_ref_constraint->setEnabled(true); }
	});
	connect(m_ctx_kwic, &QRadioButton::toggled, this, [this](bool on) {
		if (on) { m_ctx_length->setEnabled(true); m_ref_constraint->setEnabled(true); }
	});

	return group;
}


// ---------------------------------------------------------
//  File selector (same as QueryEditor)
// ---------------------------------------------------------

QWidget *ProtocolQueryEditor::createFileSelector()
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
	m_desc_op_combo->setFixedWidth(120);
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
		item->setData(Qt::UserRole, QString::fromUtf8(annot->path().data(), (int) annot->path().size()));
		item->setToolTip(item->data(Qt::UserRole).toString());
		m_file_list->addItem(item);
	}

	layout->addWidget(m_file_list);

	return group;
}


// ---------------------------------------------------------
//  Metadata panel (same as QueryEditor)
// ---------------------------------------------------------

QWidget *ProtocolQueryEditor::createMetadataPanel()
{
	auto *group = new QGroupBox(tr("File properties"));
	auto categories = Property::get_categories();

	if (categories.empty())
	{
		auto *layout = new QVBoxLayout(group);
		auto *label = new QLabel(tr("The current project doesn't have any properties."));
		label->setStyleSheet("color: gray; font-style: italic;");
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


// ---------------------------------------------------------
//  Buttons (same as QueryEditor)
// ---------------------------------------------------------

QWidget *ProtocolQueryEditor::createButtonPanel()
{
	auto *widget = new QWidget;
	auto *layout = new QHBoxLayout(widget);
	layout->setContentsMargins(0, 4, 0, 0);

	m_save_btn = new QPushButton(tr("Save"));
	m_save_btn->setEnabled(false);
	m_save_as_btn = new QPushButton(tr("Save as..."));
	m_save_as_btn->setEnabled(false);

	auto *cancel_btn = new QPushButton(tr("Cancel"));
	auto *ok_btn = new QPushButton(tr("Search"));
	ok_btn->setDefault(true);

	layout->addWidget(m_save_btn);
	layout->addWidget(m_save_as_btn);
	layout->addStretch();
	layout->addWidget(cancel_btn);
	layout->addWidget(ok_btn);

	connect(ok_btn, &QPushButton::clicked, this, &ProtocolQueryEditor::onExecute);
	connect(cancel_btn, &QPushButton::clicked, this, &QDialog::reject);
	connect(m_save_btn, &QPushButton::clicked, this, &ProtocolQueryEditor::onSave);
	connect(m_save_as_btn, &QPushButton::clicked, this, &ProtocolQueryEditor::onSaveAs);

	return widget;
}


// ---------------------------------------------------------
//  Pattern assembly
// ---------------------------------------------------------

String ProtocolQueryEditor::buildPattern() const
{
	auto separator = m_protocol->field_separator();
	auto qsep = QString::fromUtf8(separator.data(), (int) separator.size());

	QStringList parts;
	for (auto *fw : m_fields) {
		parts << fw->pattern();
	}

	auto result = parts.join(qsep);
	return String(result.toUtf8().constData(), result.toUtf8().size());
}


// ---------------------------------------------------------
//  Query construction
// ---------------------------------------------------------

void ProtocolQueryEditor::parseQuery()
{
	m_query->clear();

	// Name
	auto name = m_name_edit->text().trimmed();
	if (name.isEmpty())
		name = m_name_edit->placeholderText();
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
		if (mc)
			m_query->add_metaconstraint(std::move(mc), false);
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
			if (annot)
				annotations.append(std::move(annot));
		}
	}
	m_query->set_selection(std::move(annotations));

	// Context
	if (m_ctx_none->isChecked())
	{
		m_query->set_context(Query::Context::None);
	}
	else if (m_ctx_labels->isChecked())
	{
		m_query->set_context(Query::Context::Labels);
		m_query->set_reference_constraint(m_ref_constraint->value());
	}
	else if (m_ctx_kwic->isChecked())
	{
		m_query->set_context(Query::Context::KWIC);
		m_query->set_context_length(m_ctx_length->value());
		m_query->set_reference_constraint(m_ref_constraint->value());
		Settings::set_value("concordance", "context_length", intptr_t(m_ctx_length->value()));
	}

	// Build one constraint from the protocol fields.
	Constraint constraint;
	constraint.op = Constraint::Operator::Matches;
	constraint.case_sensitive = m_protocol->case_sensitive();
	constraint.layer_index = m_protocol->layer_index();

	auto lp = m_protocol->layer_pattern();
	if (!lp.empty())
		constraint.layer_pattern = lp;

	constraint.target = buildPattern();
	m_query->add_constraint(std::move(constraint), false);
}


// ---------------------------------------------------------
//  Execution
// ---------------------------------------------------------

void ProtocolQueryEditor::onExecute()
{
	parseQuery();

	auto *progress = new QProgressDialog(tr("Searching..."), tr("Cancel"), 0, 100, this);
	progress->setWindowModality(Qt::WindowModal);
	progress->setMinimumDuration(500);

	auto conn = m_query->query_progress.connect([progress](int current, int total) {
		if (total > 0) {
			progress->setMaximum(total);
			progress->setValue(current);
		}
	});

	connect(progress, &QProgressDialog::canceled, this, [this]() {
		m_query->request_cancel();
	});

	try
	{
		m_concordance = m_query->execute();
		conn.disconnect();
		progress->close();

		if (m_query->modified())
			Project::updated();

		accept();
	}
	catch (std::exception &e)
	{
		conn.disconnect();
		progress->close();
		QMessageBox::critical(this, tr("Query error"), QString::fromUtf8(e.what()));
	}
}

void ProtocolQueryEditor::onSave()
{
	if (m_query->path().empty()) {
		onSaveAs();
		return;
	}
	parseQuery();
	m_query->save();
	m_save_btn->setEnabled(false);
}

void ProtocolQueryEditor::onSaveAs()
{
	auto path = QFileDialog::getSaveFileName(this, tr("Save query..."),
		QString(), tr("Phonometrica query (*.phon-query)"));
	if (path.isEmpty()) return;

	bool is_new = m_query->path().empty();

	parseQuery();
	m_query->set_path(String(path.toUtf8().constData()), true);
	m_query->save();

	if (is_new) {
		Project::get()->add_query(m_query);
		Project::updated();
	}

	m_save_btn->setEnabled(false);
}

} // namespace phonometrica
