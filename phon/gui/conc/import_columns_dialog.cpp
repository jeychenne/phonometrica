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
 * Created: 24/11/2026                                                                                                 *
 *                                                                                                                     *
 * Purpose: see import_columns_dialog.hpp.                                                                             *
 *                                                                                                                     *
 ***********************************************************************************************************************/

#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QMessageBox>
#include <QPushButton>
#include <QVBoxLayout>
#include <phon/file.hpp>
#include <phon/gui/file_dialog.hpp>
#include <phon/gui/conc/import_columns_dialog.hpp>

namespace phonometrica {

static constexpr int PREVIEW_ROWS = 5;

// Data of the "Other" separator item: the separator is then read from the line edit.
static const QString OTHER_SEPARATOR = QStringLiteral("other");

static QString toQString(const String &s)
{
	return QString::fromUtf8(s.data(), (int) s.size());
}


ImportColumnsDialog::ImportColumnsDialog(Handle<Concordance> conc, ColumnImportMode mode, QWidget *parent) :
	QDialog(parent), m_conc(std::move(conc)), m_mode(mode)
{
	QString intro;
	switch (mode)
	{
	case ColumnImportMode::ById:
		setWindowTitle(tr("Import columns by ID"));
		intro = tr("Each row of the concordance receives the values of the line of the file that has the same ID. "
		           "IDs must be unique in the concordance.");
		break;
	case ColumnImportMode::ByLine:
		setWindowTitle(tr("Import columns by line"));
		intro = tr("The lines of the file are added in order: the first data line goes to the first match, "
		           "the second to the second match, and so on. The file must have exactly %1 data lines.")
		            .arg((qlonglong) m_conc->match_count());
		break;
	case ColumnImportMode::Lookup:
		setWindowTitle(tr("Import columns by key lookup"));
		intro = tr("Each row of the concordance looks up its key (for instance a word) in the file and receives "
		           "the values of the matching line. A key can occur on many rows of the concordance but "
		           "should occur only once in the file.");
		break;
	}

	setMinimumWidth(600);
	auto *layout = new QVBoxLayout(this);

	auto *intro_label = new QLabel(intro);
	intro_label->setWordWrap(true);
	layout->addWidget(intro_label);

	// ── File ───────────────────────────────────────────
	auto *file_box = new QGroupBox(tr("File"));
	auto *file_layout = new QVBoxLayout(file_box);

	auto *path_row = new QHBoxLayout;
	m_path_edit = new QLineEdit;
	m_path_edit->setPlaceholderText(tr("CSV file path..."));
	auto *browse_btn = new QPushButton(tr("Browse..."));
	path_row->addWidget(m_path_edit, 1);
	path_row->addWidget(browse_btn);
	file_layout->addLayout(path_row);

	auto *sep_row = new QHBoxLayout;
	sep_row->addWidget(new QLabel(tr("Separator:")));
	m_sep_combo = new QComboBox;
	m_sep_combo->addItem(tr("Tab"), QStringLiteral("\t"));
	m_sep_combo->addItem(tr("Comma"), QStringLiteral(","));
	m_sep_combo->addItem(tr("Semicolon"), QStringLiteral(";"));
	m_sep_combo->addItem(tr("Vertical bar"), QStringLiteral("|"));
	m_sep_combo->addItem(tr("Other:"), OTHER_SEPARATOR);
	sep_row->addWidget(m_sep_combo);
	m_sep_edit = new QLineEdit;
	m_sep_edit->setMaximumWidth(60);
	m_sep_edit->setEnabled(false);
	m_sep_edit->setToolTip(tr("Type the separator (\\t stands for a tab)"));
	sep_row->addWidget(m_sep_edit);
	sep_row->addStretch(1);
	m_header_check = new QCheckBox(tr("First line contains column names"));
	m_header_check->setChecked(true);
	sep_row->addWidget(m_header_check);
	file_layout->addLayout(sep_row);

	m_preview = new QTableWidget;
	m_preview->setEditTriggers(QAbstractItemView::NoEditTriggers);
	m_preview->setSelectionMode(QAbstractItemView::NoSelection);
	m_preview->verticalHeader()->hide();
	// Fixed height: the preview is empty when the dialog opens, and letting it grow once a file is loaded would
	// squeeze the rest of the dialog instead of resizing it.
	m_preview->setFixedHeight(150);
	m_preview->setToolTip(tr("First lines of the file"));
	file_layout->addWidget(m_preview);

	layout->addWidget(file_box);

	// ── Matching (ID and lookup) ───────────────────────
	if (mode != ColumnImportMode::ByLine)
	{
		auto *match_box = new QGroupBox(mode == ColumnImportMode::ById ? tr("Match rows by ID") : tr("Key"));
		auto *form = new QFormLayout(match_box);

		m_conc_key_combo = new QComboBox;
		for (intptr_t j = 0; j < m_conc->column_count(); j++)
		{
			if (m_conc->is_measurement_column(j)) continue;
			m_conc_key_combo->addItem(toQString(m_conc->get_header(j)), (int) j);
		}
		// Lookups are usually done on the match itself (e.g. the word): select the first target column.
		for (int k = 0; k < m_conc_key_combo->count(); k++)
		{
			if (m_conc->is_target(m_conc_key_combo->itemData(k).toInt())) {
				m_conc_key_combo->setCurrentIndex(k);
				break;
			}
		}

		m_csv_key_combo = new QComboBox;
		m_ignore_case_check = new QCheckBox(tr("Ignore case"));

		form->addRow(tr("Concordance column:"), m_conc_key_combo);
		form->addRow(tr("File column:"), m_csv_key_combo);
		form->addRow(QString(), m_ignore_case_check);

		layout->addWidget(match_box);
	}

	// ── Columns to import ──────────────────────────────
	auto *col_box = new QGroupBox(tr("Columns to import"));
	auto *col_layout = new QVBoxLayout(col_box);
	m_column_list = new QListWidget;
	m_column_list->setEditTriggers(QAbstractItemView::DoubleClicked | QAbstractItemView::EditKeyPressed);
	m_column_list->setMinimumHeight(120);
	col_layout->addWidget(m_column_list, 1);

	auto *col_buttons = new QHBoxLayout;
	auto *hint = new QLabel(tr("Double-click a name to rename the new column."));
	hint->setEnabled(false);
	col_buttons->addWidget(hint, 1);
	auto *all_btn = new QPushButton(tr("Select all"));
	auto *none_btn = new QPushButton(tr("Select none"));
	col_buttons->addWidget(all_btn);
	col_buttons->addWidget(none_btn);
	col_layout->addSpacing(6);
	col_layout->addLayout(col_buttons);

	layout->addWidget(col_box, 1);

	// ── Missing values ─────────────────────────────────
	auto *missing_box = new QGroupBox(tr("Missing values"));
	auto *missing_row = new QHBoxLayout(missing_box);
	missing_row->addWidget(new QLabel(tr("When a value is missing:")));
	m_missing_combo = new QComboBox;
	m_missing_combo->addItem(tr("Leave the cell empty"), (int) MissingValuePolicy::Empty);
	m_missing_combo->addItem(tr("Insert nan"), (int) MissingValuePolicy::NaN);
	m_missing_combo->addItem(tr("Insert a custom value:"), (int) MissingValuePolicy::Custom);
	m_missing_combo->addItem(tr("Report an error"), (int) MissingValuePolicy::Error);
	m_missing_combo->setToolTip(tr("A value is missing when a row of the concordance has no matching line in "
	                               "the file, or when the file's cell is empty or NA.\nA column whose values are all "
	                               "numbers (or missing) is imported as a numeric column."));
	missing_row->addWidget(m_missing_combo);
	m_custom_edit = new QLineEdit;
	m_custom_edit->setEnabled(false);
	m_custom_edit->setMaximumWidth(120);
	missing_row->addWidget(m_custom_edit);
	missing_row->addStretch(1);
	layout->addWidget(missing_box);

	// ── Status and buttons ─────────────────────────────
	m_status_label = new QLabel;
	m_status_label->setWordWrap(true);
	m_status_label->setTextInteractionFlags(Qt::TextSelectableByMouse);
	layout->addWidget(m_status_label);

	m_buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
	m_buttons->button(QDialogButtonBox::Ok)->setText(tr("Import"));
	layout->addWidget(m_buttons);

	// ── Connections ────────────────────────────────────
	connect(browse_btn, &QPushButton::clicked, this, &ImportColumnsDialog::browse);
	connect(m_path_edit, &QLineEdit::editingFinished, this, [this]() {
		// editingFinished is also emitted whenever the line edit loses focus (e.g. when the user double-clicks a
		// column to rename it). Reloading then would rebuild the column list under the user's feet and reset their
		// choices, so only reload when the path has actually changed.
		auto path = m_path_edit->text().trimmed();
		if (!path.isEmpty() && path != m_loaded_path) loadFile(path);
	});
	connect(m_sep_combo, &QComboBox::currentIndexChanged, this, [this]() {
		bool other = (m_sep_combo->currentData().toString() == OTHER_SEPARATOR);
		m_sep_edit->setEnabled(other);
		if (other) m_sep_edit->setFocus();
		reparse();
	});
	connect(m_sep_edit, &QLineEdit::textChanged, this, &ImportColumnsDialog::reparse);
	connect(m_header_check, &QCheckBox::toggled, this, &ImportColumnsDialog::reparse);

	if (m_csv_key_combo)
	{
		connect(m_csv_key_combo, &QComboBox::currentIndexChanged, this, &ImportColumnsDialog::onCsvKeyChanged);
		connect(m_conc_key_combo, &QComboBox::currentIndexChanged, this, &ImportColumnsDialog::updateStatus);
		connect(m_ignore_case_check, &QCheckBox::toggled, this, &ImportColumnsDialog::updateStatus);
	}

	connect(m_column_list, &QListWidget::itemChanged, this, &ImportColumnsDialog::updateStatus);
	connect(all_btn, &QPushButton::clicked, this, [this]() { setAllChecked(true); });
	connect(none_btn, &QPushButton::clicked, this, [this]() { setAllChecked(false); });

	connect(m_missing_combo, &QComboBox::currentIndexChanged, this, [this]() {
		auto policy = MissingValuePolicy(m_missing_combo->currentData().toInt());
		m_custom_edit->setEnabled(policy == MissingValuePolicy::Custom);
		if (policy == MissingValuePolicy::Custom) m_custom_edit->setFocus();
		updateStatus();
	});
	connect(m_custom_edit, &QLineEdit::textChanged, this, &ImportColumnsDialog::updateStatus);

	connect(m_buttons, &QDialogButtonBox::accepted, this, &ImportColumnsDialog::accept);
	connect(m_buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

	updateStatus();
}

void ImportColumnsDialog::browse()
{
	auto path = getOpenFileName(this, tr("Select CSV file"), tr("CSV files (*.csv *.tsv *.txt);;All files (*)"));
	if (path.isEmpty()) return;
	m_path_edit->setText(path);
	loadFile(path);
}

void ImportColumnsDialog::loadFile(const QString &path)
{
	m_loaded_path = path;

	try
	{
		m_text = File::read_all(String(path));
		m_read_error.clear();
	}
	catch (std::exception &e)
	{
		m_text = String();
		m_read_error = QString::fromUtf8(e.what());
		m_csv_valid = false;
		populateFromCsv();
		updatePreview();
		updateStatus();
		return;
	}

	// Guess the separator from the first line. Signals are blocked so that the file is parsed only once.
	auto guess = toQString(guess_csv_separator(m_text.view()));
	int index = m_sep_combo->findData(guess);
	if (index >= 0)
	{
		QSignalBlocker blocker(m_sep_combo);
		m_sep_combo->setCurrentIndex(index);
		m_sep_edit->setEnabled(false);
	}

	reparse();
}

void ImportColumnsDialog::reparse()
{
	if (m_text.empty())
	{
		// Nothing loaded yet, or the file could not be read (m_read_error says why).
		m_csv_valid = false;
	}
	else
	{
		try
		{
			auto sep = String(separator());
			m_csv = parse_csv_table(m_text.view(), sep.view(), m_header_check->isChecked());
			m_csv_valid = true;
			m_read_error.clear();
		}
		catch (std::exception &e)
		{
			m_csv_valid = false;
			m_read_error = QString::fromUtf8(e.what());
		}
	}

	populateFromCsv();
	updatePreview();
	updateStatus();
}

QString ImportColumnsDialog::uniqueColumnName(const QString &name, const QStringList &taken) const
{
	auto is_free = [&](const QString &candidate) {
		return !taken.contains(candidate) && !m_conc->has_column_named(String(candidate));
	};
	if (is_free(name)) return name;

	auto candidate = tr("%1 (CSV)").arg(name);
	for (int k = 2; !is_free(candidate); k++) {
		candidate = tr("%1 (CSV %2)").arg(name).arg(k);
	}
	return candidate;
}

void ImportColumnsDialog::populateFromCsv()
{
	QSignalBlocker list_blocker(m_column_list);
	m_column_list->clear();

	int csv_key = -1;

	if (m_csv_key_combo)
	{
		QSignalBlocker key_blocker(m_csv_key_combo);
		QSignalBlocker conc_blocker(m_conc_key_combo);
		m_csv_key_combo->clear();

		if (m_csv_valid)
		{
			for (intptr_t j = 0; j < m_csv.column_count(); j++) {
				m_csv_key_combo->addItem(toQString(m_csv.headers[j]), (int) j);
			}

			// Use the first column of the file whose name is also the name of a column of the concordance.
			for (int j = 0; j < m_csv_key_combo->count() && csv_key < 0; j++)
			{
				int k = m_conc_key_combo->findText(m_csv_key_combo->itemText(j), Qt::MatchFixedString);
				if (k >= 0)
				{
					csv_key = j;
					m_conc_key_combo->setCurrentIndex(k);
				}
			}
			if (csv_key < 0 && m_csv_key_combo->count() > 0) csv_key = 0;
			m_csv_key_combo->setCurrentIndex(csv_key);
		}
	}
	m_previous_csv_key = csv_key;

	if (!m_csv_valid) return;

	QStringList taken;
	for (intptr_t j = 0; j < m_csv.column_count(); j++)
	{
		auto header = toQString(m_csv.headers[j]);
		auto name = uniqueColumnName(header, taken);
		taken << name;

		auto *item = new QListWidgetItem(name, m_column_list);
		item->setFlags(item->flags() | Qt::ItemIsUserCheckable | Qt::ItemIsEditable);
		item->setCheckState(j == csv_key ? Qt::Unchecked : Qt::Checked);
		item->setData(Qt::UserRole, (int) j);
		item->setToolTip(tr("Column \"%1\" of the file").arg(header));
	}
}

void ImportColumnsDialog::updatePreview()
{
	m_preview->clear();

	if (!m_csv_valid)
	{
		m_preview->setRowCount(0);
		m_preview->setColumnCount(0);
		return;
	}

	int ncol = (int) m_csv.column_count();
	int nrow = (int) std::min<intptr_t>(m_csv.row_count(), PREVIEW_ROWS);
	m_preview->setColumnCount(ncol);
	m_preview->setRowCount(nrow);

	QStringList labels;
	for (auto &h : m_csv.headers) labels << toQString(h);
	m_preview->setHorizontalHeaderLabels(labels);

	for (int i = 0; i < nrow; i++)
	{
		for (int j = 0; j < ncol; j++) {
			m_preview->setItem(i, j, new QTableWidgetItem(toQString(m_csv.rows[i][j])));
		}
	}
	m_preview->resizeColumnsToContents();
}

void ImportColumnsDialog::onCsvKeyChanged()
{
	// The key column is not usually worth importing: uncheck it, and give the previous key its checkbox back.
	int key = m_csv_key_combo->currentIndex();
	{
		QSignalBlocker blocker(m_column_list);
		if (m_previous_csv_key >= 0 && m_previous_csv_key < m_column_list->count()) {
			m_column_list->item(m_previous_csv_key)->setCheckState(Qt::Checked);
		}
		if (key >= 0 && key < m_column_list->count()) {
			m_column_list->item(key)->setCheckState(Qt::Unchecked);
		}
	}
	m_previous_csv_key = key;
	updateStatus();
}

void ImportColumnsDialog::setAllChecked(bool checked)
{
	{
		QSignalBlocker blocker(m_column_list);
		for (int i = 0; i < m_column_list->count(); i++) {
			m_column_list->item(i)->setCheckState(checked ? Qt::Checked : Qt::Unchecked);
		}
	}
	updateStatus();
}

QString ImportColumnsDialog::separator() const
{
	auto sep = m_sep_combo->currentData().toString();
	if (sep == OTHER_SEPARATOR)
	{
		sep = m_sep_edit->text();
		sep.replace(QStringLiteral("\\t"), QStringLiteral("\t"));
	}
	return sep;
}

ColumnImportOptions ImportColumnsDialog::options() const
{
	ColumnImportOptions opts;
	opts.mode = m_mode;

	if (m_csv_key_combo)
	{
		opts.conc_key_column = m_conc_key_combo->currentData().isValid() ? m_conc_key_combo->currentData().toInt() : -1;
		opts.csv_key_column = m_csv_key_combo->currentData().isValid() ? m_csv_key_combo->currentData().toInt() : -1;
		opts.ignore_case = m_ignore_case_check->isChecked();
	}

	for (int i = 0; i < m_column_list->count(); i++)
	{
		auto *item = m_column_list->item(i);
		if (item->checkState() != Qt::Checked) continue;
		opts.columns.append({ String(item->text().trimmed()), (intptr_t) item->data(Qt::UserRole).toInt() });
	}

	opts.missing = MissingValuePolicy(m_missing_combo->currentData().toInt());
	opts.custom_value = String(m_custom_edit->text());

	return opts;
}

void ImportColumnsDialog::updateStatus()
{
	bool can_import = false;
	QString msg;

	if (m_text.empty() && m_read_error.isEmpty())
	{
		msg = tr("Choose a file to import.");
	}
	else if (!m_csv_valid)
	{
		msg = tr("Cannot read the file: %1").arg(m_read_error);
	}
	else if (m_csv.row_count() == 0)
	{
		msg = tr("The file contains no data.");
	}
	else
	{
		auto opts = options();

		if (opts.columns.empty())
		{
			msg = tr("Select at least one column to import.");
		}
		else
		{
			try
			{
				auto plan = plan_column_import(*m_conc, m_csv, opts);
				auto n = (qlonglong) m_conc->match_count();
				QStringList parts;
				can_import = true;

				switch (m_mode)
				{
				case ColumnImportMode::ById:
					parts << tr("%1 of %2 rows have their ID in the file.").arg((qlonglong) plan.matched).arg(n);
					if (plan.unused_rows > 0) {
						parts << tr("%1 line(s) of the file have an ID that is not in the concordance.")
						         .arg((qlonglong) plan.unused_rows);
					}
					break;
				case ColumnImportMode::ByLine:
					parts << tr("Each of the %1 lines of the file will be added to the matching row.").arg(n);
					break;
				case ColumnImportMode::Lookup:
					parts << tr("%1 of %2 rows found their key in the file.").arg((qlonglong) plan.matched).arg(n);
					break;
				}

				if (plan.missing_cells > 0) {
					parts << tr("%1 cell(s) are empty or NA in the file.").arg((qlonglong) plan.missing_cells);
				}

				if (plan.has_missing())
				{
					switch (opts.missing)
					{
					case MissingValuePolicy::Error:
						parts << tr("The import will fail because some values are missing.");
						break;
					case MissingValuePolicy::Empty:
						parts << tr("Missing values will be left empty.");
						break;
					case MissingValuePolicy::NaN:
						parts << tr("Missing values will be set to nan.");
						break;
					case MissingValuePolicy::Custom:
						parts << tr("Missing values will be set to \"%1\".").arg(m_custom_edit->text());
						break;
					}
				}

				msg = parts.join(QLatin1Char(' '));
			}
			catch (std::exception &e)
			{
				msg = QString::fromUtf8(e.what());
			}
		}
	}

	m_status_label->setText(msg);
	m_buttons->button(QDialogButtonBox::Ok)->setEnabled(can_import);
}

void ImportColumnsDialog::accept()
{
	auto opts = options();

	try
	{
		import_columns(*m_conc, m_csv, opts);
	}
	catch (std::exception &e)
	{
		QMessageBox::critical(this, windowTitle(), QString::fromUtf8(e.what()));
		return;
	}

	m_imported_count = (int) opts.columns.size();
	QDialog::accept();
}

} // namespace phonometrica
