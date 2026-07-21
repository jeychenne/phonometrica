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
#include <phon/gui/file_dialog.hpp>
#include <QMessageBox>
#include <phon/gui/info_panel.hpp>
#include <phon/gui/csv_dialog.hpp>
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
	m_multi_page_index = m_stack->addWidget(createMultiFilePage());
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

	// ── Dynamic file info area ───────────────────
	// Cleared and rebuilt each time a file is selected, matching the wx version's
	// "bold heading + value below" pattern.

	m_info_layout = new QVBoxLayout;
	m_info_layout->setSpacing(2);
	scroll_layout->addLayout(m_info_layout);

	// Sound binding row (for annotations, hidden otherwise).
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
	auto prop_label = new QLabel(tr("Properties:"));
	QFont font = prop_label->font();
	font.setBold(true);
	prop_label->setFont(font);
	scroll_layout->addWidget(prop_label);

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
	auto desc_label = new QLabel(tr("Description:"));
	desc_label->setFont(font);
	scroll_layout->addWidget(desc_label);
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

QWidget *InfoPanel::createMultiFilePage()
{
	auto *page = new QWidget;
	auto *layout = new QVBoxLayout(page);
	layout->setContentsMargins(8, 8, 8, 8);
	layout->setSpacing(6);

	QFont bold;
	bold.setBold(true);

	// ── Header ───────────────────────────────────
	m_multi_header = new QLabel;
	m_multi_header->setFont(bold);
	m_multi_header->setWordWrap(true);
	layout->addWidget(m_multi_header);

	// ── Properties table ─────────────────────────
	auto *prop_label = new QLabel(tr("Properties:"));
	prop_label->setFont(bold);
	layout->addWidget(prop_label);

	m_multi_prop_table = new QTableWidget(0, 4, page);
	m_multi_prop_table->setHorizontalHeaderLabels({tr("Type"), tr("Category"), tr("Value"), tr("Coverage")});
	m_multi_prop_table->horizontalHeader()->setStretchLastSection(true);
	m_multi_prop_table->setSelectionBehavior(QAbstractItemView::SelectRows);
	m_multi_prop_table->setSelectionMode(QAbstractItemView::SingleSelection);
	m_multi_prop_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
	m_multi_prop_table->setMinimumHeight(120);
	m_multi_prop_table->verticalHeader()->hide();
	layout->addWidget(m_multi_prop_table);

	auto *btn_row = new QHBoxLayout;
	m_multi_add_prop_btn = new QPushButton(QIcon(":/icons/circle-plus.svg"), QString());
	m_multi_add_prop_btn->setFixedWidth(32);
	m_multi_add_prop_btn->setToolTip(tr("Add property to all selected files"));
	m_multi_remove_prop_btn = new QPushButton(QIcon(":/icons/circle-minus.svg"), QString());
	m_multi_remove_prop_btn->setFixedWidth(32);
	m_multi_remove_prop_btn->setToolTip(tr("Remove property from all selected files"));
	m_multi_remove_prop_btn->setEnabled(false);
	btn_row->addWidget(m_multi_add_prop_btn);
	btn_row->addWidget(m_multi_remove_prop_btn);
	btn_row->addStretch();
	layout->addLayout(btn_row);
	connect(m_multi_add_prop_btn, &QPushButton::clicked, this, &InfoPanel::onAddProperty);
	connect(m_multi_remove_prop_btn, &QPushButton::clicked, this, &InfoPanel::onRemoveProperty);
	connect(m_multi_prop_table, &QTableWidget::itemSelectionChanged, this, [this]() {
		m_multi_remove_prop_btn->setEnabled(m_multi_prop_table->currentRow() >= 0);
	});

	// ── Property editor ──────────────────────────
	auto *editor_group = new QGroupBox(tr("Edit property"), page);
	auto *editor_layout = new QFormLayout(editor_group);

	m_multi_type_combo = new QComboBox;
	m_multi_type_combo->addItems({tr("Text"), tr("Number"), tr("Boolean")});
	m_multi_type_combo->setCurrentIndex(-1);
	m_multi_type_combo->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
	editor_layout->addRow(tr("Type:"), m_multi_type_combo);
	connect(m_multi_type_combo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &InfoPanel::onTypeChanged);

	m_multi_category_combo = new QComboBox;
	m_multi_category_combo->setEditable(true);
	m_multi_category_combo->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
	editor_layout->addRow(tr("Category:"), m_multi_category_combo);
	connect(m_multi_category_combo, &QComboBox::currentTextChanged, this, &InfoPanel::onCategoryChanged);

	m_multi_value_combo = new QComboBox;
	m_multi_value_combo->setEditable(true);
	m_multi_value_combo->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
	editor_layout->setFieldGrowthPolicy(QFormLayout::ExpandingFieldsGrow);
	editor_layout->addRow(tr("Value:"), m_multi_value_combo);

	auto *val_btn_row = new QHBoxLayout;
	m_multi_validate_btn = new QPushButton(tr("Validate"));
	m_multi_clear_btn = new QPushButton(tr("Clear"));
	val_btn_row->addWidget(m_multi_validate_btn);
	val_btn_row->addWidget(m_multi_clear_btn);
	val_btn_row->addStretch();
	editor_layout->addRow(val_btn_row);
	connect(m_multi_validate_btn, &QPushButton::clicked, this, &InfoPanel::onValidateProperty);
	connect(m_multi_clear_btn, &QPushButton::clicked, this, &InfoPanel::onClearPropertyEditor);

	layout->addWidget(editor_group);

	// ── Description ──────────────────────────────
	auto *desc_label = new QLabel(tr("Description:"));
	desc_label->setFont(bold);
	layout->addWidget(desc_label);

	m_overwrite_desc_check = new QCheckBox(tr("Overwrite all descriptions"));
	m_overwrite_desc_check->setVisible(false);
	layout->addWidget(m_overwrite_desc_check);
	connect(m_overwrite_desc_check, &QCheckBox::toggled, this, &InfoPanel::onOverwriteDescriptionToggled);

	m_multi_description_edit = new QTextEdit;
	m_multi_description_edit->setMaximumHeight(100);
	layout->addWidget(m_multi_description_edit);

	m_multi_save_desc_btn = new QPushButton(tr("Save description"));
	m_multi_save_desc_btn->setEnabled(false);
	layout->addWidget(m_multi_save_desc_btn);
	connect(m_multi_save_desc_btn, &QPushButton::clicked, this, &InfoPanel::onSaveDescription);
	connect(m_multi_description_edit, &QTextEdit::textChanged, [this]() {
		m_multi_save_desc_btn->setEnabled(true);
	});

	// ── Import/Export ────────────────────────────
	auto *meta_btn_row = new QHBoxLayout;
	m_multi_import_btn = new QPushButton(tr("Import metadata..."));
	m_multi_export_btn = new QPushButton(tr("Export metadata..."));
	meta_btn_row->addWidget(m_multi_import_btn);
	meta_btn_row->addWidget(m_multi_export_btn);
	layout->addLayout(meta_btn_row);
	connect(m_multi_import_btn, &QPushButton::clicked, this, &InfoPanel::onImportMetadata);
	connect(m_multi_export_btn, &QPushButton::clicked, this, &InfoPanel::onExportMetadata);

	layout->addStretch();

	return page;
}


// ─────────────────────────────────────────────────
//  Page switching
// ─────────────────────────────────────────────────

void InfoPanel::onSelectionChanged(QList<Document*> docs)
{
	m_selected_docs = docs;

	if (docs.size() == 1)
		showSingleFilePage(docs.first());
	else if (docs.size() > 1)
		showMultiFilePage();
	else
		showEmptyPage();
}

void InfoPanel::showEmptyPage()
{
	m_selected_docs.clear();
	m_stack->setCurrentIndex(m_empty_page_index);
}

void InfoPanel::showSingleFilePage(Document *doc)
{
	m_has_unsaved_property = false;
	m_remove_prop_btn->setEnabled(false);
	enablePropertyEditing(false);

	// Rebuild the dynamic info area.
	clearInfoArea();

	addHeading(tr("File name:"));
	addValue(doc->browser_label(), doc->path());

	bool show_sound_row = false;

	if (auto *snd = dynamic_cast<Sound *>(doc))
	{
		snd->open();
		addHeading(tr("Duration:"));
		addValue(tr("%1 seconds").arg(snd->duration(), 0, 'f', 4));
		addHeading(tr("Sample rate:"));
		addValue(tr("%1 Hz").arg(snd->sample_rate()));
		addHeading(tr("Number of channels:"));
		addValue(QString::number(snd->nchannel()));
	}
	else if (auto *annot = dynamic_cast<Annotation *>(doc))
	{
		show_sound_row = true;
		if (annot->has_sound())
			m_sound_label->setText(annot->sound()->browser_label());
		else
			m_sound_label->setText(tr("None"));
		addHeading(tr("Sound file:"));
	}

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

void InfoPanel::showMultiFilePage()
{
	m_has_unsaved_property = false;
	m_multi_remove_prop_btn->setEnabled(false);

	m_multi_header->setText(tr("%1 files selected").arg(m_selected_docs.size()));

	// Properties.
	refreshMultiProperties();

	// Reset the multi editor.
	m_multi_type_combo->setCurrentIndex(-1);
	m_multi_category_combo->clear();
	m_multi_value_combo->clear();

	// Description: check if all files have the same description.
	bool uniform = true;
	QString first_desc;
	if (!m_selected_docs.isEmpty())
	{
		first_desc = m_selected_docs.first()->description();
		for (int i = 1; i < m_selected_docs.size(); i++)
		{
			if (QString(m_selected_docs[i]->description()) != first_desc)
			{
				uniform = false;
				break;
			}
		}
	}

	m_multi_description_edit->blockSignals(true);
	m_overwrite_desc_check->blockSignals(true);

	if (uniform)
	{
		// All descriptions are the same (including all empty): enable editing directly.
		m_overwrite_desc_check->setVisible(false);
		m_overwrite_desc_check->setChecked(false);
		m_multi_description_edit->setEnabled(true);
		m_multi_description_edit->setPlainText(first_desc);
	}
	else
	{
		// Descriptions differ: disable editing, show the overwrite checkbox.
		m_overwrite_desc_check->setVisible(true);
		m_overwrite_desc_check->setChecked(false);
		m_multi_description_edit->setEnabled(false);
		m_multi_description_edit->setPlainText(QString());
	}

	m_overwrite_desc_check->blockSignals(false);
	m_multi_description_edit->blockSignals(false);
	m_multi_save_desc_btn->setEnabled(false);

	m_stack->setCurrentIndex(m_multi_page_index);
}


// ─────────────────────────────────────────────────
//  Dynamic info area helpers (single-file page)
// ─────────────────────────────────────────────────

void InfoPanel::clearInfoArea()
{
	QLayoutItem *item;
	while ((item = m_info_layout->takeAt(0)) != nullptr)
	{
		if (item->widget())
			delete item->widget();
		delete item;
	}
}

void InfoPanel::addHeading(const QString &text)
{
	auto *label = new QLabel(text);
	QFont font = label->font();
	font.setBold(true);
	label->setFont(font);
	m_info_layout->addSpacing(6);
	m_info_layout->addWidget(label);
}

void InfoPanel::addValue(const QString &text, const QString &tooltip)
{
	auto *label = new QLabel(text);
	label->setWordWrap(true);
	label->setContentsMargins(2, 0, 0, 0);
	if (!tooltip.isEmpty())
		label->setToolTip(tooltip);
	m_info_layout->addWidget(label);
}


// ─────────────────────────────────────────────────
//  Properties — single file
// ─────────────────────────────────────────────────

void InfoPanel::refreshProperties()
{
	m_prop_table->setRowCount(0);

	if (m_selected_docs.size() != 1)
		return;

	auto *doc = m_selected_docs.first();
	for (auto &prop : doc->properties())
	{
		int row = m_prop_table->rowCount();
		m_prop_table->insertRow(row);
		m_prop_table->setItem(row, 0, new QTableWidgetItem(prop.type_name()));
		m_prop_table->setItem(row, 1, new QTableWidgetItem(prop.category()));
		m_prop_table->setItem(row, 2, new QTableWidgetItem(prop.value()));
	}
}


// ─────────────────────────────────────────────────
//  Properties — multi file
// ─────────────────────────────────────────────────

void InfoPanel::refreshMultiProperties()
{
	m_multi_prop_table->setRowCount(0);

	if (m_selected_docs.isEmpty())
		return;

	int total = m_selected_docs.size();

	// Collect the union of all categories across all selected files.
	// For each category: track count, type name, and whether the value is uniform.
	struct CategoryInfo {
		QString type_name;
		QString value;
		int count = 0;
		bool uniform = true;
	};
	// Use an ordered map so rows appear in a stable order.
	std::map<QString, CategoryInfo> categories;

	for (auto *doc : m_selected_docs)
	{
		for (auto &prop : doc->properties())
		{
			QString cat = prop.category();
			auto it = categories.find(cat);
			if (it == categories.end())
			{
				CategoryInfo ci;
				ci.type_name = prop.type_name();
				ci.value = prop.value();
				ci.count = 1;
				categories[cat] = ci;
			}
			else
			{
				it->second.count++;
				if (it->second.value != QString(prop.value()))
					it->second.uniform = false;
			}
		}
	}

	for (auto &[cat, ci] : categories)
	{
		int row = m_multi_prop_table->rowCount();
		m_multi_prop_table->insertRow(row);
		m_multi_prop_table->setItem(row, 0, new QTableWidgetItem(ci.type_name));
		m_multi_prop_table->setItem(row, 1, new QTableWidgetItem(cat));

		QString val_text = ci.uniform ? ci.value : tr("(mixed)");
		auto *val_item = new QTableWidgetItem(val_text);
		if (!ci.uniform)
		{
			QFont italic = val_item->font();
			italic.setItalic(true);
			val_item->setFont(italic);
			val_item->setForeground(Qt::darkGray);
		}
		m_multi_prop_table->setItem(row, 2, val_item);

		m_multi_prop_table->setItem(row, 3,
			new QTableWidgetItem(QStringLiteral("%1/%2").arg(ci.count).arg(total)));
	}
}


// ─────────────────────────────────────────────────
//  Property selection and add/remove
// ─────────────────────────────────────────────────

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
	bool multi = (m_stack->currentIndex() == m_multi_page_index);

	if (multi)
	{
		// In multi mode, just enable the editor — no placeholder row needed.
		m_multi_type_combo->setEnabled(true);
		m_multi_category_combo->setEnabled(true);
		m_multi_value_combo->setEnabled(true);
		m_multi_validate_btn->setEnabled(true);
		m_multi_clear_btn->setEnabled(true);
		m_multi_type_combo->setFocus();
	}
	else
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
}

void InfoPanel::onRemoveProperty()
{
	bool multi = (m_stack->currentIndex() == m_multi_page_index);

	if (multi)
	{
		int row = m_multi_prop_table->currentRow();
		if (row < 0) return;

		auto *cat_item = m_multi_prop_table->item(row, 1);
		if (!cat_item) return;
		String category(cat_item->text().toUtf8().constData());

		// Remove from all selected files.
		for (auto *doc : m_selected_docs)
			doc->remove_property(category);

		refreshMultiProperties();
		m_multi_remove_prop_btn->setEnabled(false);
	}
	else
	{
		int row = m_prop_table->currentRow();
		if (row < 0) return;

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
		if (cat_item && m_selected_docs.size() == 1)
		{
			String category(cat_item->text().toUtf8().constData());
			m_selected_docs.first()->remove_property(category);
		}
		m_prop_table->removeRow(row);
		m_remove_prop_btn->setEnabled(false);
		enablePropertyEditing(false);
		m_has_unsaved_property = false;
	}
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
//  Property editor — type/category/value combos
// ─────────────────────────────────────────────────

void InfoPanel::onTypeChanged(int index)
{
	bool multi = (m_stack->currentIndex() == m_multi_page_index);
	auto *cat_combo = multi ? m_multi_category_combo : m_category_combo;
	auto *val_combo = multi ? m_multi_value_combo : m_value_combo;

	cat_combo->clear();
	val_combo->clear();

	if (index == 0) // Text
	{
		for (auto &cat : Property::get_categories_by_type(typeid(String)))
			cat_combo->addItem(cat);
	}
	else if (index == 1) // Number
	{
		for (auto &cat : Property::get_categories_by_type(typeid(double)))
			cat_combo->addItem(cat);
	}
	else if (index == 2) // Boolean
	{
		for (auto &cat : Property::get_categories_by_type(typeid(bool)))
			cat_combo->addItem(cat);
		val_combo->addItem("true");
		val_combo->addItem("false");
	}
}

void InfoPanel::onCategoryChanged()
{
	updateValueCombo();
}

void InfoPanel::updateValueCombo()
{
	bool multi = (m_stack->currentIndex() == m_multi_page_index);
	auto *type_combo = multi ? m_multi_type_combo : m_type_combo;
	auto *cat_combo = multi ? m_multi_category_combo : m_category_combo;
	auto *val_combo = multi ? m_multi_value_combo : m_value_combo;

	val_combo->clear();

	if (type_combo->currentIndex() == 0) // Text
	{
		String category(cat_combo->currentText().toUtf8().constData());
		for (auto &val : Property::get_values(category))
			val_combo->addItem(val);
	}
	else if (type_combo->currentIndex() == 2) // Boolean
	{
		val_combo->addItem("true");
		val_combo->addItem("false");
	}
}

void InfoPanel::onValidateProperty()
{
	bool multi = (m_stack->currentIndex() == m_multi_page_index);
	auto *type_combo = multi ? m_multi_type_combo : m_type_combo;
	auto *cat_combo = multi ? m_multi_category_combo : m_category_combo;
	auto *val_combo = multi ? m_multi_value_combo : m_value_combo;

	int type_index = type_combo->currentIndex();
	if (type_index < 0)
	{
		QMessageBox::warning(this, tr("Invalid property"), tr("You must first select a type."));
		return;
	}

	String type_str;
	if (type_index == 0) type_str = "Text";
	else if (type_index == 1) type_str = "Number";
	else type_str = "Boolean";

	String category(cat_combo->currentText().toUtf8().constData());
	String value(val_combo->currentText().toUtf8().constData());

	if (category.empty())
	{
		QMessageBox::warning(this, tr("Invalid property"), tr("Category cannot be empty."));
		return;
	}

	try
	{
		auto prop = Property::parse(type_str, category, value);

		if (multi)
		{
			// Apply to all selected files.
			for (auto *doc : m_selected_docs)
				doc->add_property(prop);
			refreshMultiProperties();
		}
		else
		{
			if (m_selected_docs.size() == 1)
				m_selected_docs.first()->add_property(prop);
			m_has_unsaved_property = false;
			enablePropertyEditing(false);
			refreshProperties();
		}
	}
	catch (std::exception &e)
	{
		QMessageBox::warning(this, tr("Invalid property"), e.what());
	}
}

void InfoPanel::onClearPropertyEditor()
{
	bool multi = (m_stack->currentIndex() == m_multi_page_index);
	auto *cat_combo = multi ? m_multi_category_combo : m_category_combo;
	auto *val_combo = multi ? m_multi_value_combo : m_value_combo;

	cat_combo->setCurrentText(QString());
	val_combo->setCurrentText(QString());
}


// ─────────────────────────────────────────────────
//  Description
// ─────────────────────────────────────────────────

void InfoPanel::onSaveDescription()
{
	bool multi = (m_stack->currentIndex() == m_multi_page_index);

	if (multi)
	{
		String desc(m_multi_description_edit->toPlainText().toUtf8().constData());
		for (auto *doc : m_selected_docs)
			doc->set_description(desc);
		m_multi_save_desc_btn->setEnabled(false);
	}
	else
	{
		if (m_selected_docs.size() != 1)
			return;
		String desc(m_description_edit->toPlainText().toUtf8().constData());
		m_selected_docs.first()->set_description(std::move(desc));
		m_save_desc_btn->setEnabled(false);
	}
}

void InfoPanel::onOverwriteDescriptionToggled(bool checked)
{
	m_multi_description_edit->setEnabled(checked);
	if (checked)
	{
		m_multi_description_edit->blockSignals(true);
		m_multi_description_edit->clear();
		m_multi_description_edit->blockSignals(false);
		m_multi_description_edit->setFocus();
	}
}


// ─────────────────────────────────────────────────
//  Bind sound (single-file page only)
// ─────────────────────────────────────────────────

void InfoPanel::onBindSound()
{
	if (m_selected_docs.size() != 1)
		return;

	auto *annot = dynamic_cast<Annotation *>(m_selected_docs.first());
	if (!annot)
		return;

	auto path = getOpenFileName(this, tr("Bind annotation to sound file..."), tr("Sound files (*.wav *.aiff *.flac *.ogg *.mp3);;All files (*)"));

	if (path.isEmpty())
		return;

	try
	{
		String spath(path.toUtf8().constData());
		m_project->import_file(spath);
		auto doc = m_project->get(spath);
		auto sound = handle_cast<Sound>(doc);

		if (sound)
		{
			annot->set_sound(sound);
			m_sound_label->setText(sound->browser_label());
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
//  Import/Export
// ─────────────────────────────────────────────────

void InfoPanel::onImportMetadata()
{
	CsvDialog dlg(tr("Import metadata..."), true, this);

	if (dlg.exec() == QDialog::Accepted)
	{
		auto path = dlg.filePath();
		auto sep = dlg.separator();

		if (path.empty())
		{
			QMessageBox::warning(this, tr("Import metadata"), tr("No file selected."));
			return;
		}

		try
		{
			m_project->import_metadata(path, sep);

			// Refresh whichever page is active.
			if (m_selected_docs.size() == 1)
				showSingleFilePage(m_selected_docs.first());
			else if (m_selected_docs.size() > 1)
				showMultiFilePage();
		}
		catch (std::exception &e)
		{
			QMessageBox::critical(this, tr("Import error"), e.what());
		}
	}
}

void InfoPanel::onExportMetadata()
{
	CsvDialog dlg(tr("Export metadata..."), false, this);

	if (dlg.exec() == QDialog::Accepted)
	{
		auto path = dlg.filePath();

		if (path.empty())
		{
			QMessageBox::warning(this, tr("Export metadata"), tr("No file selected."));
			return;
		}

		try
		{
			m_project->export_metadata(path);
		}
		catch (std::exception &e)
		{
			QMessageBox::critical(this, tr("Export error"), e.what());
		}
	}
}

} // namespace phonometrica
