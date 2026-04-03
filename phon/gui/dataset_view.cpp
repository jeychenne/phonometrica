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

#include <cmath>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QMenu>
#include <QAction>
#include <QMessageBox>
#include <QInputDialog>
#include <QFileDialog>
#include <phon/gui/dataset_view.hpp>
#include <phon/gui/recode_dialog.hpp>
#include <phon/gui/transform_dialog.hpp>
#include <phon/gui/help_browser.hpp>
#include <phon/application/project.hpp>
#include <phon/analysis/column_metrics.hpp>
#include <phon/analysis/formula_engine.hpp>

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
	auto *del_row_action = m_toolbar->addAction(QIcon(":/icons/grid-2x2-x.svg"), tr("Delete row(s)"));
	del_row_action->setToolTip(tr("Delete selected row(s)"));

	auto *del_col_action = m_toolbar->addAction(QIcon(":/icons/circle-minus.svg"), tr("Delete column(s)"));
	del_col_action->setToolTip(tr("Delete selected column(s)"));

	m_toolbar->addSeparator();

	// -- Filter / Subset --
	m_filter_action = m_toolbar->addAction(QIcon(":/icons/list-filter.svg"), tr("Filter"));
	m_filter_action->setToolTip(tr("Show/hide the filter bar"));
	m_filter_action->setCheckable(true);

	m_clear_filter_action = m_toolbar->addAction(QIcon(":/icons/filter-x.svg"), tr("Clear filters"));
	m_clear_filter_action->setToolTip(tr("Remove all filter rules"));
	m_clear_filter_action->setEnabled(false);

	m_subset_action = m_toolbar->addAction(QIcon(":/icons/scissors.svg"), tr("Subset"));
	m_subset_action->setToolTip(tr("Create a new dataset from the visible (filtered) rows"));
	m_subset_action->setEnabled(false);

	auto *metric_action = m_toolbar->addAction(QIcon(":/icons/sigma.svg"), tr("Metric column"));
	metric_action->setToolTip(tr("Compute a distance metric (z-score, etc.) for outlier detection"));

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

	// ── Filter bar (hidden by default) ────────────────
	m_model = new DatasetModel(m_ds, this);
	m_proxy = new DataFilterProxyModel(this);
	m_proxy->setSourceModel(m_model);

	m_filter_bar = new FilterBar(m_proxy, this);
	m_filter_bar->hide();
	setupFilterBar();
	layout->addWidget(m_filter_bar);

	// Restore saved filter rules from the document.
	if (!m_ds->filter_rules().empty())
	{
		auto &saved = m_ds->filter_rules();
		for (intptr_t r = 1; r <= saved.size(); r++)
		{
			auto &rd = saved[r];
			FilterRule rule;
			// Resolve column name to 0-based index.
			intptr_t col = m_ds->find_column(rd.column);
			rule.column = (col > 0) ? static_cast<int>(col - 1) : 0;
			rule.op = string_to_filter_op(rd.op.data());
			if (rule.op == FilterOp::InSet) {
				for (intptr_t k = 1; k <= rd.set_values.size(); k++)
					rule.set_values << QString::fromUtf8(rd.set_values[k].data(), (int)rd.set_values[k].size());
			} else {
				rule.value = QString::fromUtf8(rd.value.data(), (int)rd.value.size());
			}
			m_proxy->addRule(rule);
		}
		m_proxy->setFilterEnabled(m_ds->filter_enabled());
		m_filter_bar->rebuild();
		m_filter_bar->show();
		m_filter_action->setChecked(true);
	}

	// ── Count label ────────────────────────────────────
	auto *info_row = new QHBoxLayout;
	m_count_label = new QLabel;
	m_count_label->setStyleSheet("font-weight: bold;");
	info_row->addWidget(m_count_label);
	info_row->addStretch();

	layout->addLayout(info_row);

	// ── Table ──────────────────────────────────────────
	m_table = new QTableView;
	m_table->setModel(m_proxy);
	m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
	m_table->setSelectionMode(QAbstractItemView::ExtendedSelection);
	m_table->setEditTriggers(QAbstractItemView::DoubleClicked | QAbstractItemView::EditKeyPressed);
	m_table->setAlternatingRowColors(true);
	m_table->verticalHeader()->setDefaultSectionSize(28);
	m_table->horizontalHeader()->setStretchLastSection(false);
	m_table->setContextMenuPolicy(Qt::CustomContextMenu);
	m_table->setSortingEnabled(false);

	// Sorting is handled via the header context menu (right-click),
	// not by clicking — this avoids interfering with other header interactions.
	auto *hdr = m_table->horizontalHeader();
	hdr->setSectionsClickable(false);
	hdr->setSortIndicatorShown(false);
	hdr->setContextMenuPolicy(Qt::CustomContextMenu);
	hdr->setToolTip(tr("Right-click for column options (sort, rename, recode, transform)"));

	m_table->resizeColumnsToContents();

	layout->addWidget(m_table, 1);

	updateCountLabel();

	// ── Connections ────────────────────────────────────
	connect(save_action, &QAction::triggered, this, &DatasetView::save);
	connect(csv_action, &QAction::triggered, this, &DatasetView::onExportCsv);
	connect(del_row_action, &QAction::triggered, this, &DatasetView::onDeleteRows);
	connect(del_col_action, &QAction::triggered, this, &DatasetView::onDeleteColumns);
	connect(m_filter_action, &QAction::toggled, this, &DatasetView::onToggleFilter);
	connect(m_clear_filter_action, &QAction::triggered, this, &DatasetView::onClearFilters);
	connect(m_subset_action, &QAction::triggered, this, &DatasetView::onCreateSubset);
	connect(metric_action, &QAction::triggered, this, &DatasetView::onAddMetricColumn);
	connect(analyze_action, &QAction::triggered, this, [this]() {
		emit requestAnalysis(m_ds);
	});
	connect(union_action, &QAction::triggered, this, &DatasetView::onUnion);
	connect(intersect_action, &QAction::triggered, this, &DatasetView::onIntersection);
	connect(compl_action, &QAction::triggered, this, &DatasetView::onComplement);
	connect(merge_action, &QAction::triggered, this, &DatasetView::onMerge);
	connect(m_table, &QTableView::customContextMenuRequested, this, &DatasetView::onContextMenu);

	connect(hdr, &QHeaderView::customContextMenuRequested, this, [this](const QPoint &pos) {
		auto *header = m_table->horizontalHeader();
		int section = header->logicalIndexAt(pos);
		if (section < 0) return;

		auto col_name = m_proxy->headerData(section, Qt::Horizontal).toString();
		intptr_t col_1based = section + 1;
		QMenu menu(this);

		// ── Sort ───────────────────────────────────────
		menu.addAction(tr("Sort \"%1\" ascending").arg(col_name), this, [this, section, header]() {
			m_proxy->sort(section, Qt::AscendingOrder);
			header->setSortIndicator(section, Qt::AscendingOrder);
			header->setSortIndicatorShown(true);
		});
		menu.addAction(tr("Sort \"%1\" descending").arg(col_name), this, [this, section, header]() {
			m_proxy->sort(section, Qt::DescendingOrder);
			header->setSortIndicator(section, Qt::DescendingOrder);
			header->setSortIndicatorShown(true);
		});
		menu.addSeparator();
		menu.addAction(tr("Remove sort"), this, [this, header]() {
			m_proxy->sort(-1, Qt::AscendingOrder);
			header->setSortIndicatorShown(false);
		});

		// ── Rename ─────────────────────────────────────
		menu.addSeparator();
		menu.addAction(QIcon(":/icons/tag.svg"), tr("Rename column..."), this, [this, section]() {
			onRenameColumn(section);
		});

		if (m_ds->column_count() > 1)
		{
			menu.addAction(QIcon(":/icons/circle-minus.svg"), tr("Delete column \"%1\"").arg(col_name), this, [this, section]() {
				auto name = m_proxy->headerData(section, Qt::Horizontal).toString();
				auto answer = QMessageBox::question(this, tr("Delete column"),
					tr("Delete column \"%1\"?").arg(name),
					QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
				if (answer != QMessageBox::Yes) return;

				m_model->removeColumn(section);
				m_ds->set_content_modified(true);
				Document::file_modified();
				updateCountLabel();
				m_table->resizeColumnsToContents();
				setupFilterBar();
				emit titleChanged(label());
			});
		}

		// ── Duplicate / Move ───────────────────────────
		menu.addSeparator();
		menu.addAction(tr("Duplicate column..."), this, [this, section]() {
			onDuplicateColumn(section);
		});
		menu.addAction(tr("Move column..."), this, [this, section]() {
			onMoveColumn(section);
		});

		// ── Recode (text columns only) ─────────────────
		if (m_ds->is_text(col_1based))
		{
			menu.addSeparator();
			menu.addAction(tr("Recode values..."), this, [this, section]() {
				onRecodeColumn(section);
			});
		}

		// ── Transform (numeric columns only) ───────────
		if (m_ds->is_numeric(col_1based))
		{
			menu.addSeparator();
			menu.addAction(tr("Transform..."), this, [this, section]() {
				onTransformColumn(section);
			});
		}

		menu.exec(header->mapToGlobal(pos));
	});

	connect(m_proxy, &DataFilterProxyModel::filterChanged, this, [this]() {
		updateCountLabel();
		bool has_rules = m_proxy->ruleCount() > 0;
		m_clear_filter_action->setEnabled(has_rules);
		bool is_filtered = has_rules && m_proxy->visibleRowCount() < m_model->rowCount();
		m_subset_action->setEnabled(is_filtered);

		// Sync filter rules to the document for project serialization.
		Array<FilterRuleData> saved;
		for (auto &r : m_proxy->rules()) {
			FilterRuleData rd;
			if (r.column >= 0 && r.column < m_model->columnCount()) {
				auto h = m_ds->get_header(r.column + 1);
				rd.column = h;
			}
			rd.op = filter_op_to_string(r.op);
			if (r.op == FilterOp::InSet) {
				for (auto &v : r.set_values)
					rd.set_values.append(String(v.toUtf8().constData()));
			} else {
				rd.value = String(r.value.toUtf8().constData());
			}
			saved.append(std::move(rd));
		}
		m_ds->set_filter_rules(std::move(saved), m_proxy->isFilterEnabled());
	});

	// Update the tab label (adds/removes the modification star) when a cell is edited.
	connect(m_model, &QAbstractItemModel::dataChanged, this, [this]() {
		emit titleChanged(label());
	});
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
	if (m_ds->has_path()) {
		m_ds->reload();
	} else {
		// In-memory document: remove from project tree since there's nothing to revert to.
		m_ds->detach();
		Project::updated();
	}
}

// ── Toolbar actions ─────────────────────────────────────

void DatasetView::onDeleteRows()
{
	auto proxy_rows = selectedRows();
	if (proxy_rows.isEmpty())
	{
		QMessageBox::information(this, tr("Information"),
			tr("Select one or more rows to delete."));
		return;
	}

	// Map proxy rows to source rows.
	QList<int> source_rows;
	for (int pr : proxy_rows) {
		auto source_idx = m_proxy->mapToSource(m_proxy->index(pr, 0));
		source_rows.append(source_idx.row());
	}
	std::sort(source_rows.begin(), source_rows.end());

	// Remove from bottom to top to keep indices valid.
	for (int i = source_rows.size() - 1; i >= 0; i--)
		m_model->removeRow(source_rows[i]);

	m_ds->set_content_modified(true);
	Document::file_modified();
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
	Document::file_modified();
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

// ── Filter / Subset ────────────────────────────────────────

void DatasetView::setupFilterBar()
{
	QStringList headers;
	for (intptr_t j = 1; j <= m_ds->column_count(); j++) {
		auto h = m_ds->get_header(j);
		headers << QString::fromUtf8(h.data(), (int) h.size());
	}

	m_filter_bar->setColumns(headers,
		// isNumeric callback
		[this](int col) -> bool {
			return m_ds->is_numeric(col + 1); // 0-based → 1-based
		},
		// getLevels callback
		[this](int col) -> QStringList {
			QStringList levels;
			auto arr = m_ds->get_levels(col + 1);
			for (intptr_t i = 1; i <= arr.size(); i++) {
				levels << QString::fromUtf8(arr[i].data(), (int) arr[i].size());
			}
			return levels;
		}
	);
}

void DatasetView::onToggleFilter()
{
	bool show = m_filter_action->isChecked();
	m_filter_bar->setVisible(show);
	m_proxy->setFilterEnabled(show);

	// Auto-add a first rule so the user can start filtering immediately.
	if (show && m_proxy->ruleCount() == 0) {
		m_filter_bar->appendStrip();
	}
}

void DatasetView::onClearFilters()
{
	m_proxy->clearRules();
	m_filter_bar->rebuild();
}

void DatasetView::onCreateSubset()
{
	auto visible = m_proxy->visibleSourceRows();
	if (visible.isEmpty() || visible.size() == m_model->rowCount()) return;

	bool ok;
	auto name = QInputDialog::getText(this, tr("Create subset"),
		tr("Name for the subset:"),
		QLineEdit::Normal, tr("Filtered"), &ok);
	if (!ok || name.isEmpty()) return;

	try
	{
		std::vector<int> rows(visible.begin(), visible.end());
		auto result = m_ds->subset(rows, String(name.toUtf8().constData()));
		Project::updated();
		emit datasetCreated(result);
	}
	catch (std::exception &e)
	{
		QMessageBox::critical(this, tr("Error"), QString::fromUtf8(e.what()));
	}
}

void DatasetView::onAddMetricColumn()
{
	// Collect numeric and text columns.
	QStringList num_names, text_names;
	QVector<int> num_indices, text_indices;

	for (intptr_t j = 1; j <= m_ds->column_count(); j++) {
		auto h = m_ds->get_header(j);
		auto qh = QString::fromUtf8(h.data(), (int) h.size());
		if (m_ds->is_numeric(j)) {
			num_names << qh;
			num_indices << (int) j;
		}
		else if (m_ds->is_text(j)) {
			text_names << qh;
			text_indices << (int) j;
		}
	}

	if (num_names.isEmpty()) {
		QMessageBox::information(this, tr("Information"),
			tr("No numeric columns available."));
		return;
	}

	OutlierDialog dlg(num_names, num_indices, text_names, text_indices, this);
	if (dlg.exec() != QDialog::Accepted) return;

	auto metric = dlg.selectedMetric();
	auto group_cols = dlg.groupByColumns();
	auto col_name = dlg.columnName();

	if (col_name.isEmpty()) return;

	try
	{
		// Build composite group labels from selected group-by columns.
		std::vector<std::string> groups;
		if (!group_cols.isEmpty()) {
			auto nrows = m_ds->row_count();
			groups.resize(nrows);
			for (intptr_t i = 0; i < nrows; i++) {
				std::string key;
				for (int gc : group_cols) {
					auto text_span = m_ds->text_column(gc);
					if (!key.empty()) key += '|';
					key.append(text_span[i].data(), text_span[i].size());
				}
				groups[i] = std::move(key);
			}
		}

		std::vector<double> result;

		if (stats::is_multivariate(metric))
		{
			// Multi-column metric.
			auto cols = dlg.selectedColumns();
			if (cols.size() < 2) {
				QMessageBox::information(this, tr("Information"),
					tr("Please select at least two columns for this metric."));
				return;
			}
			std::vector<std::vector<double>> columns;
			for (int c : cols) {
				auto span = m_ds->numeric_column(c);
				columns.emplace_back(span.begin(), span.end());
			}
			result = stats::compute_multivariate_metric(columns, groups, metric);
		}
		else
		{
			// Single-column metric.
			int col = dlg.selectedColumn();
			auto span = m_ds->numeric_column(col);
			std::vector<double> values(span.begin(), span.end());
			result = stats::compute_column_metric(values, groups, metric);
		}

		// Add the column.
		m_ds->add_numeric_column(String(col_name.toUtf8().constData()), result);
		m_model->refreshAll();
		m_table->resizeColumnsToContents();
		setupFilterBar(); // refresh column list in filter bar
		updateCountLabel();
		emit titleChanged(label());

		// Auto-add filter rule if requested.
		if (dlg.addFilter())
		{
			double threshold = dlg.filterThreshold();
			int new_col_idx = m_ds->column_count() - 1; // 0-based for the proxy

			// For absolute/distance metrics, use ≤ directly.
			// For signed metrics (ZScore, ModifiedZScore), filter on absolute value: add two rules.
			bool is_positive = (metric == stats::ColumnMetric::AbsZScore ||
			                    metric == stats::ColumnMetric::AbsModifiedZScore ||
			                    metric == stats::ColumnMetric::Percentile ||
			                    metric == stats::ColumnMetric::EuclideanDistance ||
			                    metric == stats::ColumnMetric::MahalanobisDistance);

			if (is_positive)
			{
				FilterRule rule;
				rule.column = new_col_idx;
				rule.op = FilterOp::Le;
				rule.value = QString::number(threshold, 'f', 2);
				m_proxy->addRule(rule);
			}
			else
			{
				// For signed z-scores: keep values where -threshold ≤ value ≤ threshold.
				FilterRule rule_ge;
				rule_ge.column = new_col_idx;
				rule_ge.op = FilterOp::Ge;
				rule_ge.value = QString::number(-threshold, 'f', 2);
				m_proxy->addRule(rule_ge);

				FilterRule rule_le;
				rule_le.column = new_col_idx;
				rule_le.op = FilterOp::Le;
				rule_le.value = QString::number(threshold, 'f', 2);
				m_proxy->addRule(rule_le);
			}

			// Show the filter bar and update UI.
			m_filter_action->setChecked(true);
			m_filter_bar->rebuild();
		}
	}
	catch (std::exception &e)
	{
		QMessageBox::critical(this, tr("Error"), QString::fromUtf8(e.what()));
	}
}

// ── Column rename / recode / transform ─────────────────

void DatasetView::onRenameColumn(int section)
{
	auto current = m_model->headerData(section, Qt::Horizontal, Qt::DisplayRole).toString();

	bool ok;
	auto new_name = QInputDialog::getText(this,
		tr("Rename column"),
		tr("New name for column \"%1\":").arg(current),
		QLineEdit::Normal, current, &ok);

	if (!ok || new_name.trimmed().isEmpty()) return;

	m_model->setHeaderData(section, Qt::Horizontal, new_name.trimmed(), Qt::EditRole);
	setupFilterBar(); // refresh column names in filter bar
	emit titleChanged(label());
}

void DatasetView::onRecodeColumn(int section)
{
	intptr_t col = section + 1; // 1-based
	auto col_name_str = m_ds->get_header(col);
	auto col_name = QString::fromUtf8(col_name_str.data(), (int) col_name_str.size());

	// Collect unique levels.
	auto levels_arr = m_ds->get_levels(col);
	QStringList levels;
	for (intptr_t i = 1; i <= levels_arr.size(); i++) {
		levels << QString::fromUtf8(levels_arr[i].data(), (int) levels_arr[i].size());
	}

	RecodeDialog dlg(col_name, levels, this);
	if (dlg.exec() != QDialog::Accepted) return;

	auto new_col_name = dlg.newColumnName();
	if (new_col_name.isEmpty()) return;

	auto mapping = dlg.mapping();

	try
	{
		// Build the new column by mapping each row's value.
		auto text_span = m_ds->text_column(col);
		std::vector<String> new_values(m_ds->row_count());

		for (intptr_t i = 0; i < m_ds->row_count(); i++)
		{
			auto original = QString::fromUtf8(text_span[i].data(), (int) text_span[i].size());
			auto it = mapping.find(original);
			if (it != mapping.end()) {
				new_values[i] = String(it.value().toUtf8().constData());
			}
			else {
				new_values[i] = text_span[i]; // Keep original if not in mapping.
			}
		}

		m_ds->add_text_column(String(new_col_name.toUtf8().constData()), new_values);
		m_model->refreshAll();
		m_table->resizeColumnsToContents();
		setupFilterBar();
		updateCountLabel();
		emit titleChanged(label());
	}
	catch (std::exception &e)
	{
		QMessageBox::critical(this, tr("Error"), QString::fromUtf8(e.what()));
	}
}

void DatasetView::onTransformColumn(int section)
{
	intptr_t col = section + 1; // 1-based
	auto col_name_str = m_ds->get_header(col);
	auto col_name = QString::fromUtf8(col_name_str.data(), (int) col_name_str.size());

	// Collect sample values for the preview (first 8 rows).
	auto span = m_ds->numeric_column(col);
	QVector<double> samples;
	intptr_t sample_count = std::min(m_ds->row_count(), (intptr_t)8);
	for (intptr_t i = 0; i < sample_count; i++)
		samples.append(span[i]);

	TransformDialog dlg(col_name, samples, this);
	if (dlg.exec() != QDialog::Accepted) return;

	auto new_col_name = dlg.newColumnName();
	if (new_col_name.isEmpty()) return;

	try
	{
		FormulaEngine engine;
		engine.parse(dlg.formula().toStdString());

		std::vector<double> result(m_ds->row_count());
		int nan_count = 0;

		for (intptr_t i = 0; i < m_ds->row_count(); i++)
		{
			double val = span[i];
			result[i] = engine.evaluate(val);
			if (std::isnan(result[i]) && !std::isnan(val))
				nan_count++;
		}

		m_ds->add_numeric_column(String(new_col_name.toUtf8().constData()), result);
		m_model->refreshAll();
		m_table->resizeColumnsToContents();
		setupFilterBar();
		updateCountLabel();
		emit titleChanged(label());

		if (nan_count > 0)
		{
			QMessageBox::information(this, tr("Transform"),
				tr("%1 value(s) produced NaN (non-positive input, division by zero, etc.).")
					.arg(nan_count));
		}
	}
	catch (std::exception &e)
	{
		QMessageBox::critical(this, tr("Error"), QString::fromUtf8(e.what()));
	}
}

// ── Column position helper ─────────────────────────────

static QStringList buildPositionList(const Handle<Dataset> &ds, int exclude = -1)
{
	QStringList items;
	for (intptr_t j = 1; j <= ds->column_count(); j++)
	{
		if ((int) j == exclude) continue;
		auto h = ds->get_header(j);
		auto qh = QString::fromUtf8(h.data(), (int) h.size());
		items << QStringLiteral("%1 \u2014 Before \"%2\"").arg(j).arg(qh);
	}
	int end_pos = (int) ds->column_count() + 1;
	items << QStringLiteral("%1 \u2014 At the end").arg(end_pos);
	return items;
}

void DatasetView::onDuplicateColumn(int section)
{
	intptr_t col = section + 1; // 1-based
	auto col_name_str = m_ds->get_header(col);
	auto col_name = QString::fromUtf8(col_name_str.data(), (int) col_name_str.size());

	auto items = buildPositionList(m_ds);
	// Default: right after the source column.
	int default_idx = (int) col; // 0-based index in the list → position col+1

	bool ok;
	auto choice = QInputDialog::getItem(this,
		tr("Duplicate column"),
		tr("Insert copy of \"%1\" at:").arg(col_name),
		items, default_idx, false, &ok);
	if (!ok) return;

	// Parse the position number from the chosen item.
	int dest = choice.left(choice.indexOf(QChar(0x2014)) - 1).trimmed().toInt();

	try
	{
		m_ds->duplicate_column(col, dest);
		m_model->refreshAll();
		m_table->resizeColumnsToContents();
		setupFilterBar();
		updateCountLabel();
		Document::file_modified();
		emit titleChanged(label());
	}
	catch (std::exception &e)
	{
		QMessageBox::critical(this, tr("Error"), QString::fromUtf8(e.what()));
	}
}

void DatasetView::onMoveColumn(int section)
{
	intptr_t col = section + 1; // 1-based
	auto col_name_str = m_ds->get_header(col);
	auto col_name = QString::fromUtf8(col_name_str.data(), (int) col_name_str.size());

	if (m_ds->column_count() < 2) return;

	// Build position list from the perspective of the layout after
	// the source column is removed, so that "Before «X»" means what the user expects.
	QStringList items;
	int slot = 1;
	for (intptr_t j = 1; j <= m_ds->column_count(); j++)
	{
		if (j == col) continue; // skip the column being moved
		auto h = m_ds->get_header(j);
		auto qh = QString::fromUtf8(h.data(), (int) h.size());
		items << QStringLiteral("%1 \u2014 Before \"%2\"").arg(slot).arg(qh);
		slot++;
	}
	items << QStringLiteral("%1 \u2014 At the end").arg(slot);

	bool ok;
	auto choice = QInputDialog::getItem(this,
		tr("Move column"),
		tr("Move \"%1\" to:").arg(col_name),
		items, 0, false, &ok);
	if (!ok) return;

	// Parse the slot number — this is the 1-based insert position in the
	// array after removal of the source column.
	int dest = choice.left(choice.indexOf(QChar(0x2014)) - 1).trimmed().toInt();

	try
	{
		// move_column removes src, then inserts at dest in the post-removal array.
		m_ds->move_column(col, dest);
		m_model->refreshAll();
		m_table->resizeColumnsToContents();
		setupFilterBar();
		updateCountLabel();
		Document::file_modified();
		emit titleChanged(label());
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

	menu.addAction(QIcon(":/icons/list-x.svg"), tr("Delete row(s)"), this, &DatasetView::onDeleteRows);
	menu.addAction(QIcon(":/icons/grid-2x2-x.svg"), tr("Delete column(s)"), this, &DatasetView::onDeleteColumns);
	menu.addSeparator();
	menu.addAction(QIcon(":/icons/pencil-line.svg"), tr("Edit cell"), this, [this, index]() {
		m_table->edit(index);
	});

	menu.exec(m_table->viewport()->mapToGlobal(pos));
}

// ── Helpers ─────────────────────────────────────────────

void DatasetView::updateCountLabel()
{
	auto total = m_ds->row_count();
	auto ncols = m_ds->column_count();
	auto visible = m_proxy->visibleRowCount();

	if (visible < (int) total) {
		m_count_label->setText(tr("Showing %1 of %2 row(s), %3 column(s)")
			.arg(visible).arg((int) total).arg((int) ncols));
	}
	else {
		m_count_label->setText(tr("%1 row(s), %2 column(s)").arg((int) total).arg((int) ncols));
	}
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
