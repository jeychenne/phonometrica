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
#include <QInputDialog>
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

	m_toolbar->addSeparator();

	// -- Set operations --
	auto *union_action = m_toolbar->addAction(QIcon(":/icons/set-union.svg"), tr("Union"));
	union_action->setToolTip(tr("Unite with another dataset (A \u222a B)"));

	auto *intersect_action = m_toolbar->addAction(QIcon(":/icons/set-intersection.svg"), tr("Intersect"));
	intersect_action->setToolTip(tr("Intersect with another dataset (A \u2229 B)"));

	auto *compl_action = m_toolbar->addAction(QIcon(":/icons/set-complement.svg"), tr("Complement"));
	compl_action->setToolTip(tr("Get complement (A \u2216 B)"));

	auto *merge_action = m_toolbar->addAction(QIcon(":/icons/layers.svg"), tr("Merge"));
	merge_action->setToolTip(tr("Horizontal merge: add columns from another table"));

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
	connect(union_action, &QAction::triggered, this, &DatasetView::onUnion);
	connect(intersect_action, &QAction::triggered, this, &DatasetView::onIntersection);
	connect(compl_action, &QAction::triggered, this, &DatasetView::onComplement);
	connect(merge_action, &QAction::triggered, this, &DatasetView::onMerge);
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

// ── Set operations ─────────────────────────────────────────

Handle<Dataset> DatasetView::pickDataset(const QString &title)
{
	auto datasets = Project::get()->get_datasets();
	QStringList names;

	for (auto &ds : datasets)
	{
		if (ds.get() != m_ds.get())
			names << QString::fromUtf8(ds->label().data(), (int) ds->label().size());
	}

	if (names.isEmpty())
	{
		QMessageBox::information(this, title, tr("No other datasets available."));
		return {};
	}

	bool ok;
	auto choice = QInputDialog::getItem(this, title, tr("Select dataset:"), names, 0, false, &ok);
	if (!ok) return {};

	for (auto &ds : datasets)
	{
		auto lbl = QString::fromUtf8(ds->label().data(), (int) ds->label().size());
		if (lbl == choice && ds.get() != m_ds.get())
		{
			ds->open();
			return ds;
		}
	}

	return {};
}

void DatasetView::onUnion()
{
	auto other = pickDataset(tr("Unite datasets"));
	if (!other) return;

	bool ok;
	auto name = QInputDialog::getText(this, tr("Union"), tr("Name for the result:"),
		QLineEdit::Normal, tr("Union"), &ok);
	if (!ok || name.isEmpty()) return;

	try
	{
		auto result = m_ds->unite(*other, String(name.toUtf8().constData()));
		Project::updated();
		emit datasetCreated(result);
	}
	catch (std::exception &e)
	{
		QMessageBox::critical(this, tr("Error"), QString::fromUtf8(e.what()));
	}
}

void DatasetView::onIntersection()
{
	auto other = pickDataset(tr("Intersect datasets"));
	if (!other) return;

	bool ok;
	auto name = QInputDialog::getText(this, tr("Intersect"), tr("Name for the result:"),
		QLineEdit::Normal, tr("Intersection"), &ok);
	if (!ok || name.isEmpty()) return;

	try
	{
		auto result = m_ds->intersect(*other, String(name.toUtf8().constData()));
		Project::updated();
		emit datasetCreated(result);
	}
	catch (std::exception &e)
	{
		QMessageBox::critical(this, tr("Error"), QString::fromUtf8(e.what()));
	}
}

void DatasetView::onComplement()
{
	auto other = pickDataset(tr("Complement"));
	if (!other) return;

	bool ok;
	auto name = QInputDialog::getText(this, tr("Complement"), tr("Name for the result:"),
		QLineEdit::Normal, tr("Complement"), &ok);
	if (!ok || name.isEmpty()) return;

	try
	{
		auto result = m_ds->complement(*other, String(name.toUtf8().constData()));
		Project::updated();
		emit datasetCreated(result);
	}
	catch (std::exception &e)
	{
		QMessageBox::critical(this, tr("Error"), QString::fromUtf8(e.what()));
	}
}

// ── Merge ──────────────────────────────────────────────────

Handle<DataTable> DatasetView::pickDataTable(const QString &title)
{
	auto concordances = Project::get()->get_concordances();
	auto datasets = Project::get()->get_datasets();
	QStringList names;

	for (auto &d : datasets)
	{
		if (d.get() != m_ds.get())
			names << QString::fromUtf8(d->label().data(), (int) d->label().size());
	}
	for (auto &c : concordances)
	{
		names << QString::fromUtf8(c->label().data(), (int) c->label().size());
	}

	if (names.isEmpty())
	{
		QMessageBox::information(this, title, tr("No other data tables available."));
		return {};
	}

	bool ok;
	auto choice = QInputDialog::getItem(this, title, tr("Select data table:"), names, 0, false, &ok);
	if (!ok) return {};

	for (auto &d : datasets)
	{
		auto lbl = QString::fromUtf8(d->label().data(), (int) d->label().size());
		if (lbl == choice && d.get() != m_ds.get())
		{
			d->open();
			return d;
		}
	}
	for (auto &c : concordances)
	{
		auto lbl = QString::fromUtf8(c->label().data(), (int) c->label().size());
		if (lbl == choice)
		{
			c->open();
			return c;
		}
	}

	return {};
}

void DatasetView::onMerge()
{
	auto other = pickDataTable(tr("Merge tables"));
	if (!other) return;

	// Check row count compatibility.
	if (m_ds->row_count() != other->row_count())
	{
		QMessageBox::critical(this, tr("Error"),
			tr("Cannot merge: tables have different numbers of rows (%1 vs %2).")
				.arg((int) m_ds->row_count()).arg((int) other->row_count()));
		return;
	}

	// ── If the other table is a Concordance, the result must be a Concordance. ──
	// We flip the perspective: the concordance is the base and this dataset's columns are merged into it.
	auto *other_conc = dynamic_cast<Concordance *>(other.get());
	if (other_conc)
	{
		auto a_cols = other_conc->column_count(); // A = concordance (base)
		auto b_cols = m_ds->column_count();       // B = this dataset
		auto rows = other_conc->row_count();

		std::map<String, intptr_t> a_headers;
		for (intptr_t j = 1; j <= a_cols; j++) {
			a_headers[other_conc->get_header(j)] = j;
		}

		Array<std::pair<String, intptr_t>> columns_to_add;

		for (intptr_t j = 1; j <= b_cols; j++)
		{
			auto header = m_ds->get_header(j);
			auto it = a_headers.find(header);

			if (it == a_headers.end())
			{
				columns_to_add.append(std::make_pair(header, j));
			}
			else
			{
				bool same = true;
				for (intptr_t i = 1; i <= rows; i++)
				{
					if (other_conc->get_cell(i, it->second) != m_ds->get_cell(i, j)) {
						same = false;
						break;
					}
				}

				if (!same)
				{
					auto qheader = QString::fromUtf8(header.data(), (int) header.size());
					auto answer = QMessageBox::question(this, tr("Column conflict"),
						tr("Column \"%1\" has different values.\nAdd the dataset's values as \"%1 (B)\"?")
							.arg(qheader),
						QMessageBox::Yes | QMessageBox::No, QMessageBox::No);

					if (answer == QMessageBox::Yes)
					{
						auto suffixed = header;
						suffixed.append(" (B)");
						columns_to_add.append(std::make_pair(suffixed, j));
					}
				}
			}
		}

		if (columns_to_add.empty())
		{
			QMessageBox::information(this, tr("Merge"),
				tr("No new columns to add."));
			return;
		}

		bool ok;
		auto name = QInputDialog::getText(this, tr("Merge"), tr("Name for the result:"),
			QLineEdit::Normal, tr("Merged"), &ok);
		if (!ok || name.isEmpty()) return;

		try
		{
			// The concordance merges with this dataset (dataset columns are B).
			auto result = other_conc->merge(*m_ds, String(name.toUtf8().constData()), columns_to_add);
			Project::updated();
			emit concordanceCreated(result);
		}
		catch (std::exception &e)
		{
			QMessageBox::critical(this, tr("Error"), QString::fromUtf8(e.what()));
		}
		return;
	}

	// ── Other table is a Dataset → result is a Dataset. ──
	auto a_cols = m_ds->column_count();
	auto b_cols = other->column_count();
	auto rows = m_ds->row_count();

	std::map<String, intptr_t> a_headers;
	for (intptr_t j = 1; j <= a_cols; j++) {
		a_headers[m_ds->get_header(j)] = j;
	}

	Array<std::pair<String, intptr_t>> columns_to_add;
	Array<std::pair<intptr_t, intptr_t>> columns_to_overwrite;

	for (intptr_t j = 1; j <= b_cols; j++)
	{
		auto header = other->get_header(j);
		auto it = a_headers.find(header);

		if (it == a_headers.end())
		{
			columns_to_add.append(std::make_pair(header, j));
		}
		else
		{
			bool same = true;
			for (intptr_t i = 1; i <= rows; i++)
			{
				if (m_ds->get_cell(i, it->second) != other->get_cell(i, j)) {
					same = false;
					break;
				}
			}

			if (!same)
			{
				auto qheader = QString::fromUtf8(header.data(), (int) header.size());
				auto answer = QMessageBox::question(this, tr("Column conflict"),
					tr("Column \"%1\" has different values.\nOverwrite with values from B?")
						.arg(qheader),
					QMessageBox::Yes | QMessageBox::No, QMessageBox::No);

				if (answer == QMessageBox::Yes)
				{
					columns_to_overwrite.append(std::make_pair(it->second, j));
				}
			}
		}
	}

	if (columns_to_add.empty() && columns_to_overwrite.empty())
	{
		QMessageBox::information(this, tr("Merge"),
			tr("No new columns to add and no columns to overwrite."));
		return;
	}

	bool ok;
	auto name = QInputDialog::getText(this, tr("Merge"), tr("Name for the result:"),
		QLineEdit::Normal, tr("Merged"), &ok);
	if (!ok || name.isEmpty()) return;

	try
	{
		auto result = m_ds->merge(*other, String(name.toUtf8().constData()), columns_to_add, columns_to_overwrite);
		Project::updated();
		emit datasetCreated(result);
	}
	catch (std::exception &e)
	{
		QMessageBox::critical(this, tr("Error"), QString::fromUtf8(e.what()));
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
