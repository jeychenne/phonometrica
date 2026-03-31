/***********************************************************************************************************************
 *                                                                                                                     *
 * Copyright (C) 2019-2026 Julien Eychenne                                                                             *
 *                                                                                                                     *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0. If a copy of the MPL was not   *
 * distributed with this file, You can obtain one at https://mozilla.org/MPL/2.0/.                                     *
 *                                                                                                                     *
 * Created: 31/03/2026                                                                                                 *
 *                                                                                                                     *
 * Purpose: see header.                                                                                                *
 *                                                                                                                     *
 ***********************************************************************************************************************/

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QMenu>
#include <QAction>
#include <QMessageBox>
#include <QFileDialog>
#include <phon/gui/dataset_view.hpp>
#include <phon/gui/help_browser.hpp>
#include <phon/application/project.hpp>

namespace phonometrica {

DatasetView::DatasetView(Handle<Dataset> ds, QWidget *parent) :
	View(parent), m_ds(std::move(ds))
{
	m_ds->open(); // Ensure data is loaded from disk if not already.
	setupUi();
}

void DatasetView::setupUi()
{
	auto *layout = new QVBoxLayout(this);
	layout->setContentsMargins(6, 6, 6, 6);

	// ── Toolbar ────────────────────────────────────────
	m_toolbar = new QToolBar;
	m_toolbar->setIconSize(QSize(20, 20));

	// -- File operations --
	auto *save_action = m_toolbar->addAction(QIcon(":/icons/save.svg"), tr("Save dataset"));
	save_action->setToolTip(tr("Save dataset (Ctrl+S)"));

	auto *csv_action = m_toolbar->addAction(QIcon(":/icons/file-spreadsheet.svg"), tr("Export to CSV"));
	csv_action->setToolTip(tr("Export dataset to CSV..."));

	m_toolbar->addSeparator();

	// -- Editing --
	auto *del_row_action = m_toolbar->addAction(QIcon(":/icons/trash-2.svg"), tr("Delete row(s)"));
	del_row_action->setToolTip(tr("Delete selected row(s)"));

	auto *del_col_action = m_toolbar->addAction(QIcon(":/icons/circle-minus.svg"), tr("Delete column(s)"));
	del_col_action->setToolTip(tr("Delete selected column(s)"));

	m_toolbar->addSeparator();

	auto *analyze_action = m_toolbar->addAction(QIcon(":/icons/statistics.svg"), tr("Analyze"));
	analyze_action->setToolTip(tr("Open analysis view for this dataset"));

	// ── Right-aligned help button ─────────────────────
	auto *spacer = new QWidget(this);
	spacer->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
	m_toolbar->addWidget(spacer);

	auto *help_action = m_toolbar->addAction(QIcon(":/icons/circle-help.svg"),
		tr("Help"));
	connect(help_action, &QAction::triggered, this, [this]() {
		HelpBrowser::showPage(helpAnchor(), this);
	});

	layout->addWidget(m_toolbar);

	// ── Count label ────────────────────────────────────
	auto *info_row = new QHBoxLayout;
	m_count_label = new QLabel;
	m_count_label->setStyleSheet("font-weight: bold;");
	info_row->addWidget(m_count_label);
	info_row->addStretch();

	layout->addLayout(info_row);

	// ── Table ──────────────────────────────────────────
	m_model = new DatasetModel(m_ds, this);
	m_table = new QTableView;
	m_table->setModel(m_model);
	m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
	m_table->setSelectionMode(QAbstractItemView::ExtendedSelection);
	m_table->setEditTriggers(QAbstractItemView::DoubleClicked | QAbstractItemView::EditKeyPressed);
	m_table->setAlternatingRowColors(true);
	m_table->verticalHeader()->setDefaultSectionSize(28);
	m_table->horizontalHeader()->setStretchLastSection(false);
	m_table->setContextMenuPolicy(Qt::CustomContextMenu);
	m_table->setSortingEnabled(false);

	m_table->resizeColumnsToContents();

	layout->addWidget(m_table, 1);

	updateCountLabel();

	// ── Connections ────────────────────────────────────
	connect(save_action, &QAction::triggered, this, &DatasetView::save);
	connect(csv_action, &QAction::triggered, this, &DatasetView::onExportCsv);
	connect(del_row_action, &QAction::triggered, this, &DatasetView::onDeleteRows);
	connect(del_col_action, &QAction::triggered, this, &DatasetView::onDeleteColumns);
	connect(analyze_action, &QAction::triggered, this, [this]() {
		emit requestAnalysis(m_ds);
	});
	connect(m_table, &QTableView::customContextMenuRequested, this, &DatasetView::onContextMenu);
}

// ── View interface ──────────────────────────────────────

QString DatasetView::label() const
{
	auto lbl = m_ds->label();
	auto qlabel = tabLabel(QString::fromUtf8(lbl.data(), (int) lbl.size()));
	if (m_ds->modified())
		qlabel += QStringLiteral(" *");
	return qlabel;
}

String DatasetView::path() const
{
	return m_ds->path();
}

bool DatasetView::isModified() const
{
	return m_ds->modified();
}

bool DatasetView::save()
{
	bool is_new = m_ds->path().empty();

	if (is_new)
	{
		auto current_label = QString::fromUtf8(m_ds->label().data(), (int) m_ds->label().size());
		auto suggested = current_label + QStringLiteral(".csv");

		auto path = QFileDialog::getSaveFileName(this, tr("Save dataset..."),
			suggested, tr("CSV files (*.csv)"));
		if (path.isEmpty()) return false;

		m_ds->set_path(String(path.toUtf8().constData()), true);
	}

	m_ds->set_content_modified(true);
	m_ds->save();

	if (is_new)
	{
		auto *project = Project::get();
		project->data()->append(m_ds, true);
		project->register_file(m_ds->path(), m_ds);
		Project::updated();
	}

	emit titleChanged(label());
	return true;
}

void DatasetView::discardChanges()
{
	m_ds->discard_changes();
}

// ── Toolbar actions ─────────────────────────────────────

void DatasetView::onDeleteRows()
{
	auto rows = selectedRows();
	if (rows.isEmpty())
	{
		QMessageBox::information(this, tr("Information"),
			tr("Select one or more rows to delete."));
		return;
	}

	// Remove from bottom to top to keep indices valid.
	for (int i = rows.size() - 1; i >= 0; i--)
		m_model->removeRow(rows[i]);

	m_ds->set_content_modified(true);
	updateCountLabel();
	emit titleChanged(label());
}

void DatasetView::onDeleteColumns()
{
	auto cols = selectedColumns();
	if (cols.isEmpty())
	{
		QMessageBox::information(this, tr("Information"),
			tr("Select one or more columns to delete."));
		return;
	}

	if (cols.size() >= m_model->columnCount())
	{
		QMessageBox::warning(this, tr("Warning"),
			tr("Cannot delete all columns."));
		return;
	}

	// Remove from right to left to keep indices valid.
	for (int i = cols.size() - 1; i >= 0; i--)
		m_model->removeColumn(cols[i]);

	m_ds->set_content_modified(true);
	updateCountLabel();
	m_table->resizeColumnsToContents();
	emit titleChanged(label());
}

void DatasetView::onExportCsv()
{
	auto path = QFileDialog::getSaveFileName(this, tr("Export to CSV..."),
		QString(), tr("CSV files (*.csv *.txt)"));
	if (path.isEmpty()) return;

	try
	{
		m_ds->to_csv(String(path.toUtf8().constData()), ",");
		QMessageBox::information(this, tr("Export"), tr("Dataset exported successfully."));
	}
	catch (std::exception &e)
	{
		QMessageBox::critical(this, tr("Export error"), QString::fromUtf8(e.what()));
	}
}

// ── Context menu ────────────────────────────────────────

void DatasetView::onContextMenu(const QPoint &pos)
{
	auto index = m_table->indexAt(pos);
	if (!index.isValid()) return;

	QMenu menu(this);

	menu.addAction(QIcon(":/icons/trash-2.svg"), tr("Delete row(s)"), this, &DatasetView::onDeleteRows);
	menu.addAction(QIcon(":/icons/circle-minus.svg"), tr("Delete column(s)"), this, &DatasetView::onDeleteColumns);
	menu.addSeparator();
	menu.addAction(QIcon(":/icons/pencil-line.svg"), tr("Edit cell"), this, [this, index]() {
		m_table->edit(index);
	});

	menu.exec(m_table->viewport()->mapToGlobal(pos));
}

// ── Helpers ─────────────────────────────────────────────

void DatasetView::updateCountLabel()
{
	auto nrows = m_ds->row_count();
	auto ncols = m_ds->column_count();
	m_count_label->setText(tr("%1 row(s), %2 column(s)").arg((int) nrows).arg((int) ncols));
}

int DatasetView::selectedRow() const
{
	auto sel = m_table->selectionModel()->selectedRows();
	if (sel.size() != 1) return -1;
	return sel.first().row();
}

int DatasetView::selectedColumn() const
{
	auto sel = m_table->selectionModel()->selectedColumns();
	if (sel.size() != 1) return -1;
	return sel.first().column();
}

QList<int> DatasetView::selectedRows() const
{
	QList<int> rows;
	for (auto &idx : m_table->selectionModel()->selectedRows())
		rows.append(idx.row());
	std::sort(rows.begin(), rows.end());
	return rows;
}

QList<int> DatasetView::selectedColumns() const
{
	// Gather unique column indices from the current selection.
	// This works whether the user selected full columns (via header click)
	// or individual cells.
	QSet<int> cols;
	for (auto &idx : m_table->selectionModel()->selectedIndexes())
		cols.insert(idx.column());

	QList<int> result(cols.begin(), cols.end());
	std::sort(result.begin(), result.end());
	return result;
}

} // namespace phonometrica
