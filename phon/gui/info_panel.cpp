/***********************************************************************************************************************
 *                                                                                                                     *
 * Copyright (C) 2019-2026 Julien Eychenne                                                                             *
 *                                                                                                                     *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0. If a copy of the MPL was not   *
 * distributed with this file, You can obtain one at https://mozilla.org/MPL/2.0/.                                     *
 *                                                                                                                     *
 * Created: 25/03/2026                                                                                                 *
 *                                                                                                                     *
 * Purpose: see header.                                                                                                *
 *                                                                                                                     *
 ***********************************************************************************************************************/

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFormLayout>
#include <QGroupBox>
#include <QHeaderView>
#include <QFileDialog>
#include <QMessageBox>
#include <phon/gui/info_panel.hpp>
#include <phon/application/project.hpp>
#include <phon/application/annotation.hpp>
#include <phon/application/sound.hpp>

namespace phonometrica {

InfoPanel::InfoPanel(Project *project, QWidget *parent) :
	QWidget(parent), m_project(project)
{
	setupUi();
	showEmptyPage();
}


// ─────────────────────────────────────────────────
//  UI setup
// ─────────────────────────────────────────────────

void InfoPanel::setupUi()
{
	auto *layout = new QVBoxLayout(this);
	layout->setContentsMargins(0, 0, 0, 0);

	m_stack = new QStackedWidget(this);
	layout->addWidget(m_stack);

	m_empty_page_index = m_stack->addWidget(createEmptyPage());
	m_single_page_index = m_stack->addWidget(createSingleFilePage());
}

QWidget *InfoPanel::createEmptyPage()
{
	auto *page = new QWidget;
	auto *layout = new QVBoxLayout(page);
	layout->setAlignment(Qt::AlignCenter);
	auto *label = new QLabel(tr("Select a file to view its properties."));
	label->setAlignment(Qt::AlignCenter);
	label->setWordWrap(true);
	layout->addWidget(label);
	return page;
}

QWidget *InfoPanel::createSingleFilePage()
{
	auto *page = new QWidget;
	auto *scroll_layout = new QVBoxLayout(page);
	scroll_layout->setContentsMargins(8, 8, 8, 8);
	scroll_layout->setSpacing(6);

	// ── File info ────────────────────────────────

	m_file_name_label = new QLabel;
	m_file_name_label->setWordWrap(true);
	QFont bold_font = m_file_name_label->font();
	bold_font.setBold(true);
	m_file_name_label->setFont(bold_font);
	scroll_layout->addWidget(m_file_name_label);

	m_file_info_label = new QLabel;
	m_file_info_label->setWordWrap(true);
	scroll_layout->addWidget(m_file_info_label);

	// Sound binding (for annotations).
	auto *sound_row = new QHBoxLayout;
	m_sound_label = new QLabel;
	m_sound_label->setWordWrap(true);
	sound_row->addWidget(m_sound_label, 1);
	m_bind_button = new QPushButton(tr("Bind..."));
	m_bind_button->setToolTip(tr("Bind annotation to a sound file"));
	sound_row->addWidget(m_bind_button);
	scroll_layout->addLayout(sound_row);
	connect(m_bind_button, &QPushButton::clicked, this, &InfoPanel::onBindSound);

	// ── Properties table ─────────────────────────

	scroll_layout->addWidget(new QLabel(tr("Properties:")));

	m_prop_table = new QTableWidget(0, 3, page);
	m_prop_table->setHorizontalHeaderLabels({tr("Type"), tr("Category"), tr("Value")});
	m_prop_table->horizontalHeader()->setStretchLastSection(true);
	m_prop_table->setSelectionBehavior(QAbstractItemView::SelectRows);
	m_prop_table->setSelectionMode(QAbstractItemView::SingleSelection);
	m_prop_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
	m_prop_table->setMinimumHeight(120);
	m_prop_table->verticalHeader()->hide();
	scroll_layout->addWidget(m_prop_table);
	connect(m_prop_table, &QTableWidget::itemSelectionChanged, this, &InfoPanel::onPropertySelected);

	// +/- buttons.
	auto *btn_row = new QHBoxLayout;
	m_add_prop_btn = new QPushButton(QIcon(":/icons/circle-plus.svg"), QString());
	m_add_prop_btn->setFixedWidth(32);
	m_add_prop_btn->setToolTip(tr("Add property"));
	m_remove_prop_btn = new QPushButton(QIcon(":/icons/circle-minus.svg"), QString());
	m_remove_prop_btn->setFixedWidth(32);
	m_remove_prop_btn->setToolTip(tr("Remove property"));
	m_remove_prop_btn->setEnabled(false);
	btn_row->addWidget(m_add_prop_btn);
	btn_row->addWidget(m_remove_prop_btn);
	btn_row->addStretch();
	scroll_layout->addLayout(btn_row);
	connect(m_add_prop_btn, &QPushButton::clicked, this, &InfoPanel::onAddProperty);
	connect(m_remove_prop_btn, &QPushButton::clicked, this, &InfoPanel::onRemoveProperty);

	// ── Property editor ──────────────────────────

	auto *editor_group = new QGroupBox(tr("Edit property"), page);
	auto *editor_layout = new QFormLayout(editor_group);

	m_type_combo = new QComboBox;
	m_type_combo->addItems({tr("Text"), tr("Number"), tr("Boolean")});
	m_type_combo->setCurrentIndex(-1);
	m_type_combo->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
	editor_layout->addRow(tr("Type:"), m_type_combo);
	connect(m_type_combo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &InfoPanel::onTypeChanged);

	m_category_combo = new QComboBox;
	m_category_combo->setEditable(true);
	m_category_combo->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
	editor_layout->addRow(tr("Category:"), m_category_combo);
	connect(m_category_combo, &QComboBox::currentTextChanged, this, &InfoPanel::onCategoryChanged);

	m_value_combo = new QComboBox;
	m_value_combo->setEditable(true);
	m_value_combo->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
	editor_layout->setFieldGrowthPolicy(QFormLayout::ExpandingFieldsGrow);
	editor_layout->addRow(tr("Value:"), m_value_combo);

	auto *val_btn_row = new QHBoxLayout;
	m_validate_btn = new QPushButton(tr("Validate"));
	m_clear_btn = new QPushButton(tr("Clear"));
	val_btn_row->addWidget(m_validate_btn);
	val_btn_row->addWidget(m_clear_btn);
	val_btn_row->addStretch();
	editor_layout->addRow(val_btn_row);
	connect(m_validate_btn, &QPushButton::clicked, this, &InfoPanel::onValidateProperty);
	connect(m_clear_btn, &QPushButton::clicked, this, &InfoPanel::onClearPropertyEditor);

	scroll_layout->addWidget(editor_group);
	enablePropertyEditing(false);

	// ── Description ──────────────────────────────

	scroll_layout->addWidget(new QLabel(tr("Description:")));
	m_description_edit = new QTextEdit;
	m_description_edit->setMaximumHeight(100);
	scroll_layout->addWidget(m_description_edit);

	m_save_desc_btn = new QPushButton(tr("Save description"));
	m_save_desc_btn->setEnabled(false);
	scroll_layout->addWidget(m_save_desc_btn);
	connect(m_save_desc_btn, &QPushButton::clicked, this, &InfoPanel::onSaveDescription);
	connect(m_description_edit, &QTextEdit::textChanged, [this]() {
		m_save_desc_btn->setEnabled(true);
	});

	// ── Import/Export ────────────────────────────

	auto *meta_btn_row = new QHBoxLayout;
	m_import_btn = new QPushButton(tr("Import metadata..."));
	m_export_btn = new QPushButton(tr("Export metadata..."));
	meta_btn_row->addWidget(m_import_btn);
	meta_btn_row->addWidget(m_export_btn);
	scroll_layout->addLayout(meta_btn_row);
	connect(m_import_btn, &QPushButton::clicked, this, &InfoPanel::onImportMetadata);
	connect(m_export_btn, &QPushButton::clicked, this, &InfoPanel::onExportMetadata);

	scroll_layout->addStretch();

	return page;
}


// ─────────────────────────────────────────────────
//  Page switching
// ─────────────────────────────────────────────────

void InfoPanel::onSelectionChanged(Element *elem)
{
	auto *doc = dynamic_cast<Document *>(elem);

	if (doc)
		showSingleFilePage(doc);
	else
		showEmptyPage();
}

void InfoPanel::showEmptyPage()
{
	m_current_doc = nullptr;
	m_stack->setCurrentIndex(m_empty_page_index);
}

void InfoPanel::showSingleFilePage(Document *doc)
{
	m_current_doc = doc;
	m_has_unsaved_property = false;
	m_remove_prop_btn->setEnabled(false);
	enablePropertyEditing(false);

	// File name.
	m_file_name_label->setText(doc->label());

	// Type-specific info.
	QString info;
	bool show_sound_row = false;

	if (auto *snd = dynamic_cast<Sound *>(doc))
	{
		snd->open();
		info = tr("Sound: %1 channel(s), %2 Hz, %3 s")
			.arg(snd->nchannel())
			.arg(snd->sample_rate())
			.arg(snd->duration(), 0, 'f', 2);
	}
	else if (auto *annot = dynamic_cast<Annotation *>(doc))
	{
		show_sound_row = true;
		if (annot->has_sound())
		{
			m_sound_label->setText(tr("Sound: %1").arg(annot->sound()->label()));
		}
		else
		{
			m_sound_label->setText(tr("Sound: (none)"));
		}
		info = tr("Annotation: %1 layer(s)").arg(annot->size());
	}
	else
	{
		info = doc->class_name();
	}

	m_file_info_label->setText(info);
	m_sound_label->setVisible(show_sound_row);
	m_bind_button->setVisible(show_sound_row);

	// Properties.
	refreshProperties();

	// Description.
	m_description_edit->blockSignals(true);
	m_description_edit->setPlainText(doc->description());
	m_description_edit->blockSignals(false);
	m_save_desc_btn->setEnabled(false);

	m_stack->setCurrentIndex(m_single_page_index);
}


// ─────────────────────────────────────────────────
//  Properties
// ─────────────────────────────────────────────────

void InfoPanel::refreshProperties()
{
	m_prop_table->setRowCount(0);

	if (!m_current_doc)
		return;

	for (auto &prop : m_current_doc->properties())
	{
		int row = m_prop_table->rowCount();
		m_prop_table->insertRow(row);
		m_prop_table->setItem(row, 0, new QTableWidgetItem(prop.type_name()));
		m_prop_table->setItem(row, 1, new QTableWidgetItem(prop.category()));
		m_prop_table->setItem(row, 2, new QTableWidgetItem(prop.value()));
	}
}

void InfoPanel::onPropertySelected()
{
	if (m_has_unsaved_property)
	{
		auto reply = QMessageBox::question(this, tr("Discard unsaved property?"),
			tr("You have an unsaved property. Discard it?"));
		if (reply != QMessageBox::Yes)
			return;
		// Remove the last row (the unsaved one).
		m_prop_table->removeRow(m_prop_table->rowCount() - 1);
		m_has_unsaved_property = false;
	}
	m_remove_prop_btn->setEnabled(m_prop_table->currentRow() >= 0);
}

void InfoPanel::onAddProperty()
{
	enablePropertyEditing(true);

	int row = m_prop_table->rowCount();
	m_prop_table->insertRow(row);
	auto *type_item = new QTableWidgetItem(tr("undefined"));
	auto *cat_item = new QTableWidgetItem(tr("undefined"));
	auto *val_item = new QTableWidgetItem(tr("undefined"));
	QFont italic = type_item->font();
	italic.setItalic(true);
	type_item->setFont(italic);
	type_item->setForeground(Qt::red);
	cat_item->setFont(italic);
	cat_item->setForeground(Qt::red);
	val_item->setFont(italic);
	val_item->setForeground(Qt::red);
	m_prop_table->setItem(row, 0, type_item);
	m_prop_table->setItem(row, 1, cat_item);
	m_prop_table->setItem(row, 2, val_item);
	m_prop_table->selectRow(row);
	m_has_unsaved_property = true;
	m_remove_prop_btn->setEnabled(true);
}

void InfoPanel::onRemoveProperty()
{
	int row = m_prop_table->currentRow();
	if (row < 0)
		return;

	// If it's the unsaved row, just remove it.
	if (m_has_unsaved_property && row == m_prop_table->rowCount() - 1)
	{
		m_prop_table->removeRow(row);
		m_has_unsaved_property = false;
		enablePropertyEditing(false);
		m_remove_prop_btn->setEnabled(false);
		return;
	}

	// Remove from the document.
	auto *cat_item = m_prop_table->item(row, 1);
	if (cat_item && m_current_doc)
	{
		String category(cat_item->text().toUtf8().constData());
		m_current_doc->remove_property(category);
	}
	m_prop_table->removeRow(row);
	m_remove_prop_btn->setEnabled(false);
	enablePropertyEditing(false);
	m_has_unsaved_property = false;
}

void InfoPanel::enablePropertyEditing(bool enabled)
{
	m_type_combo->setEnabled(enabled);
	m_category_combo->setEnabled(enabled);
	m_value_combo->setEnabled(enabled);
	m_validate_btn->setEnabled(enabled);
	m_clear_btn->setEnabled(enabled);

	if (!enabled)
	{
		m_type_combo->setCurrentIndex(-1);
		m_category_combo->clear();
		m_value_combo->clear();
	}
}


// ─────────────────────────────────────────────────
//  Property editor
// ─────────────────────────────────────────────────

void InfoPanel::onTypeChanged(int index)
{
	m_category_combo->clear();
	m_value_combo->clear();

	if (index == 0) // Text
	{
		for (auto &cat : Property::get_categories_by_type(typeid(String)))
			m_category_combo->addItem(cat);
	}
	else if (index == 1) // Number
	{
		for (auto &cat : Property::get_categories_by_type(typeid(double)))
			m_category_combo->addItem(cat);
	}
	else if (index == 2) // Boolean
	{
		for (auto &cat : Property::get_categories_by_type(typeid(bool)))
			m_category_combo->addItem(cat);
		m_value_combo->addItem("true");
		m_value_combo->addItem("false");
	}
}

void InfoPanel::onCategoryChanged()
{
	updateValueCombo();
}

void InfoPanel::updateValueCombo()
{
	m_value_combo->clear();

	if (m_type_combo->currentIndex() == 0) // Text
	{
		String category(m_category_combo->currentText().toUtf8().constData());
		for (auto &val : Property::get_values(category))
			m_value_combo->addItem(val);
	}
	else if (m_type_combo->currentIndex() == 2) // Boolean
	{
		m_value_combo->addItem("true");
		m_value_combo->addItem("false");
	}
}

void InfoPanel::onValidateProperty()
{
	if (!m_current_doc)
		return;

	int type_index = m_type_combo->currentIndex();
	if (type_index < 0)
	{
		QMessageBox::warning(this, tr("Invalid property"), tr("You must first select a type."));
		return;
	}

	String type_str;
	if (type_index == 0) type_str = "Text";
	else if (type_index == 1) type_str = "Number";
	else type_str = "Boolean";

	String category(m_category_combo->currentText().toUtf8().constData());
	String value(m_value_combo->currentText().toUtf8().constData());

	if (category.empty())
	{
		QMessageBox::warning(this, tr("Invalid property"), tr("Category cannot be empty."));
		return;
	}

	try
	{
		auto prop = Property::parse(type_str, category, value);
		m_current_doc->add_property(prop);
	}
	catch (std::exception &e)
	{
		QMessageBox::warning(this, tr("Invalid property"), e.what());
		return;
	}

	m_has_unsaved_property = false;
	enablePropertyEditing(false);
	refreshProperties();
}

void InfoPanel::onClearPropertyEditor()
{
	m_category_combo->setCurrentText(QString());
	m_value_combo->setCurrentText(QString());
}


// ─────────────────────────────────────────────────
//  Description
// ─────────────────────────────────────────────────

void InfoPanel::onSaveDescription()
{
	if (!m_current_doc)
		return;

	String desc(m_description_edit->toPlainText().toUtf8().constData());
	m_current_doc->set_description(std::move(desc));
	m_save_desc_btn->setEnabled(false);
}


// ─────────────────────────────────────────────────
//  Bind sound
// ─────────────────────────────────────────────────

void InfoPanel::onBindSound()
{
	if (!m_current_doc)
		return;

	auto *annot = dynamic_cast<Annotation *>(m_current_doc);
	if (!annot)
		return;

	auto path = QFileDialog::getOpenFileName(this, tr("Bind annotation to sound file..."),
		QString(), tr("Sound files (*.wav *.aiff *.flac *.ogg *.mp3);;All files (*)"));

	if (path.isEmpty())
		return;

	try
	{
		String spath(path.toUtf8().constData());
		m_project->import_file(spath);
		auto doc = m_project->get(spath);
		auto sound = recast<Sound>(doc);

		if (sound)
		{
			annot->set_sound(sound);
			m_sound_label->setText(tr("Sound: %1").arg(sound->label()));
		}
		else
		{
			QMessageBox::warning(this, tr("Error"), tr("Could not find the sound file in the project."));
		}
	}
	catch (std::exception &e)
	{
		QMessageBox::warning(this, tr("Error"), e.what());
	}
}


// ─────────────────────────────────────────────────
//  Import/Export (stubs)
// ─────────────────────────────────────────────────

void InfoPanel::onImportMetadata()
{
	QMessageBox::information(this, tr("Not implemented"), tr("Import metadata from CSV is not yet implemented."));
}

void InfoPanel::onExportMetadata()
{
	QMessageBox::information(this, tr("Not implemented"), tr("Export metadata to CSV is not yet implemented."));
}

} // namespace phonometrica
