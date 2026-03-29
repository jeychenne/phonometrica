/***********************************************************************************************************************
 *                                                                                                                     *
 * Copyright (C) 2019-2026 Julien Eychenne                                                                             *
 *                                                                                                                     *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0. If a copy of the MPL was not   *
 * distributed with this file, You can obtain one at https://mozilla.org/MPL/2.0/.                                     *
 *                                                                                                                     *
 * Created: 27/03/2026                                                                                                 *
 *                                                                                                                     *
 * Purpose: see header.                                                                                                *
 *                                                                                                                     *
 ***********************************************************************************************************************/

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QMenu>
#include <QAction>
#include <QActionGroup>
#include <QToolButton>
#include <QMessageBox>
#include <QInputDialog>
#include <QFileDialog>
#include <QFileInfo>
#include <phon/gui/conc/concordance_view.hpp>
#include <phon/gui/help_browser.hpp>
#include <phon/application/project.hpp>
#include <phon/application/settings.hpp>

namespace phonometrica {

ConcordanceView::ConcordanceView(Handle<Concordance> conc, QWidget *parent) :
	View(parent), m_conc(std::move(conc))
{
	m_conc->open(); // Ensure matches are loaded from disk if not already.
	setupUi();
}

ConcordanceView::~ConcordanceView()
{
	stopPlayer();
}

void ConcordanceView::setupUi()
{
	auto *layout = new QVBoxLayout(this);
	layout->setContentsMargins(6, 6, 6, 6);

	// ── Toolbar ────────────────────────────────────────
	m_toolbar = new QToolBar;
	m_toolbar->setIconSize(QSize(20, 20));

	// -- File operations --
	auto *save_action = m_toolbar->addAction(QIcon(":/icons/save.svg"), tr("Save concordance"));
	save_action->setToolTip(tr("Save concordance (Ctrl+S)"));

	auto *csv_action = m_toolbar->addAction(QIcon(":/icons/file-spreadsheet.svg"), tr("Export to CSV"));
	csv_action->setToolTip(tr("Export concordance to CSV..."));

	m_toolbar->addSeparator();

	// -- Playback --
	auto *play_action = m_toolbar->addAction(QIcon(":/icons/play.svg"), tr("Play"));
	play_action->setToolTip(tr("Play selected match"));

	auto *stop_action = m_toolbar->addAction(QIcon(":/icons/square.svg"), tr("Stop"));
	stop_action->setToolTip(tr("Stop playback"));

	// -- Navigation --
	auto *view_action = m_toolbar->addAction(QIcon(":/icons/scan-eye.svg"), tr("View"));
	view_action->setToolTip(tr("Open match in annotation"));

	m_toolbar->addSeparator();

	// -- Editing --
	auto *bookmark_action = m_toolbar->addAction(QIcon(":/icons/book-marked.svg"), tr("Bookmark"));
	bookmark_action->setToolTip(tr("Bookmark selected match"));

	m_toolbar->addSeparator();

	auto *del_action = m_toolbar->addAction(QIcon(":/icons/trash-2.svg"), tr("Delete"));
	del_action->setToolTip(tr("Delete selected row(s)"));

	auto *edit_action = m_toolbar->addAction(QIcon(":/icons/pencil-line.svg"), tr("Edit"));
	edit_action->setToolTip(tr("Edit selected event"));

	m_toolbar->addSeparator();

	// -- Set operations --
	auto *union_action = m_toolbar->addAction(QIcon(":/icons/set-union.svg"), tr("Union"));
	union_action->setToolTip(tr("Unite with another concordance (A \u222a B)"));

	auto *intersect_action = m_toolbar->addAction(QIcon(":/icons/set-intersection.svg"), tr("Intersect"));
	intersect_action->setToolTip(tr("Intersect with another concordance (A \u2229 B)"));

	auto *compl_action = m_toolbar->addAction(QIcon(":/icons/set-complement.svg"), tr("Complement"));
	compl_action->setToolTip(tr("Get complement (B \u2216 A)"));

	m_toolbar->addSeparator();

	auto *rename_action = m_toolbar->addAction(QIcon(":/icons/tag.svg"), tr("Rename"));
	rename_action->setToolTip(tr("Rename concordance..."));

	m_toolbar->addSeparator();

	// -- Display menu (show/hide column groups) --
	auto *display_menu = new QMenu(this);

	auto *info_action = display_menu->addAction(tr("Match info"));
	info_action->setCheckable(true);
	info_action->setChecked(m_show_match_info);
	info_action->setToolTip(tr("Show file, layer, start time, end time"));

	auto *ctx_action = display_menu->addAction(tr("Context"));
	ctx_action->setCheckable(true);
	ctx_action->setChecked(m_show_context && m_conc->has_context());
	ctx_action->setEnabled(m_conc->has_context());
	ctx_action->setToolTip(tr("Show left and right context"));

	auto *meta_action = display_menu->addAction(tr("Metadata"));
	meta_action->setCheckable(true);
	meta_action->setChecked(m_show_metadata);
	meta_action->setToolTip(tr("Show file description and properties"));

	// Wide/Long toggle — shown for formant and pitch concordances; enabled only for n-point data
	QAction *long_action = nullptr;
	if (m_conc->nformant() > 0 || m_conc->is_pitch())
	{
		display_menu->addSeparator();
		long_action = display_menu->addAction(tr("Long format (one row per time point)"));
		long_action->setCheckable(true);
		long_action->setChecked(m_conc->layout() == Concordance::Layout::Long);
		long_action->setEnabled(m_conc->has_measurement_data());
		if (m_conc->has_measurement_data())
			long_action->setToolTip(tr("Toggle between wide format (one row per match) and long format (one row per time point)"));
		else
			long_action->setToolTip(tr("Long format is only available for n-point measurements (not midpoint)"));
	}

	display_menu->addSeparator();
	auto *split_action = display_menu->addAction(tr("Open matches in split view"));
	split_action->setCheckable(true);
	split_action->setChecked(m_open_in_split);
	split_action->setToolTip(tr("When checked, open the annotation beside the concordance; otherwise open it in a new tab"));

	auto *display_action = new QAction(QIcon(":/icons/display.svg"), tr("Display settings"), this);
	display_action->setMenu(display_menu);
	m_toolbar->addAction(display_action);
	if (auto *db = qobject_cast<QToolButton *>(m_toolbar->widgetForAction(display_action)))
		db->setPopupMode(QToolButton::InstantPopup);

	// -- Scales menu (Add/Remove ERB, Bark) — only for formant concordances --
	if (m_conc->nformant() > 0)
	{
		auto *scales_menu = new QMenu(this);

		m_erb_action = scales_menu->addAction(tr("ERB values"));
		m_erb_action->setCheckable(true);
		m_erb_action->setChecked(m_conc->has_erb());
		m_erb_action->setToolTip(tr("Show formant values converted to the ERB scale"));

		m_bark_action = scales_menu->addAction(tr("Bark values"));
		m_bark_action->setCheckable(true);
		m_bark_action->setChecked(m_conc->has_bark());
		m_bark_action->setToolTip(tr("Show formant values converted to the Bark scale"));

		auto *scales_action = new QAction(QIcon(":/icons/ruler.svg"), tr("Scales"), this);
		scales_action->setMenu(scales_menu);
		m_toolbar->addAction(scales_action);
		if (auto *sb = qobject_cast<QToolButton *>(m_toolbar->widgetForAction(scales_action)))
			sb->setPopupMode(QToolButton::InstantPopup);

		connect(m_erb_action, &QAction::toggled, this, &ConcordanceView::onToggleErb);
		connect(m_bark_action, &QAction::toggled, this, &ConcordanceView::onToggleBark);
	}

	// -- Scales menu for pitch concordances --
	if (m_conc->is_pitch())
	{
		auto *scales_menu = new QMenu(this);

		m_pitch_st_action = scales_menu->addAction(tr("Semitones"));
		m_pitch_st_action->setCheckable(true);
		m_pitch_st_action->setChecked(m_conc->has_semitones());
		m_pitch_st_action->setToolTip(tr("Show pitch values converted to semitones"));

		m_pitch_erb_action = scales_menu->addAction(tr("ERB rate"));
		m_pitch_erb_action->setCheckable(true);
		m_pitch_erb_action->setChecked(m_conc->has_pitch_erb());
		m_pitch_erb_action->setToolTip(tr("Show pitch values converted to the ERB scale"));

		auto *scales_action = new QAction(QIcon(":/icons/ruler.svg"), tr("Scales"), this);
		scales_action->setMenu(scales_menu);
		m_toolbar->addAction(scales_action);
		if (auto *sb = qobject_cast<QToolButton *>(m_toolbar->widgetForAction(scales_action)))
			sb->setPopupMode(QToolButton::InstantPopup);

		connect(m_pitch_st_action, &QAction::toggled, this, &ConcordanceView::onTogglePitchSemitones);
		connect(m_pitch_erb_action, &QAction::toggled, this, &ConcordanceView::onTogglePitchErb);
	}

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

	// ── Count label + active target spinner ────────────
	auto *info_row = new QHBoxLayout;
	m_count_label = new QLabel;
	m_count_label->setStyleSheet("font-weight: bold;");
	info_row->addWidget(m_count_label);
	info_row->addStretch();

	info_row->addWidget(new QLabel(tr("Active target:")));
	m_target_spin = new QSpinBox;
	m_target_spin->setRange(1, m_conc->target_count());
	m_target_spin->setEnabled(m_conc->target_count() > 1);
	info_row->addWidget(m_target_spin);

	layout->addLayout(info_row);

	// ── Table ──────────────────────────────────────────
	m_model = new ConcordanceModel(m_conc, this);
	m_table = new QTableView;
	m_table->setModel(m_model);
	m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
	m_table->setSelectionMode(QAbstractItemView::ExtendedSelection);
	// Allow double-click editing on editable cells (measurement columns).
	// The model's flags() method controls which cells are actually editable.
	m_table->setEditTriggers(QAbstractItemView::DoubleClicked | QAbstractItemView::EditKeyPressed);
	m_table->setAlternatingRowColors(true);
	m_table->verticalHeader()->setDefaultSectionSize(28);
	m_table->horizontalHeader()->setStretchLastSection(false);
	m_table->setContextMenuPolicy(Qt::CustomContextMenu);
	m_table->setSortingEnabled(false);

	// Monospace font for the table body.
	QFont mono("monospace", m_table->font().pointSize());
	mono.setStyleHint(QFont::Monospace);
	m_table->setFont(mono);

	m_table->resizeColumnsToContents();

	layout->addWidget(m_table, 1);

	updateCountLabel();
	updateColumnVisibility();

	// ── Connections ────────────────────────────────────
	connect(save_action, &QAction::triggered, this, &ConcordanceView::save);
	connect(csv_action, &QAction::triggered, this, &ConcordanceView::onExportCsv);
	connect(play_action, &QAction::triggered, this, &ConcordanceView::onPlay);
	connect(stop_action, &QAction::triggered, this, &ConcordanceView::onStop);
	connect(view_action, &QAction::triggered, this, &ConcordanceView::onViewMatch);
	connect(bookmark_action, &QAction::triggered, this, &ConcordanceView::onBookmark);
	connect(del_action, &QAction::triggered, this, &ConcordanceView::onDeleteRows);
	connect(edit_action, &QAction::triggered, this, &ConcordanceView::onEditEvent);
	connect(union_action, &QAction::triggered, this, &ConcordanceView::onUnion);
	connect(intersect_action, &QAction::triggered, this, &ConcordanceView::onIntersection);
	connect(compl_action, &QAction::triggered, this, &ConcordanceView::onComplement);
	connect(rename_action, &QAction::triggered, this, &ConcordanceView::onRename);
	connect(m_table, &QTableView::doubleClicked, this, &ConcordanceView::onDoubleClick);
	connect(m_table, &QTableView::customContextMenuRequested, this, &ConcordanceView::onContextMenu);

	connect(info_action, &QAction::toggled, this, &ConcordanceView::onToggleMatchInfo);
	connect(ctx_action, &QAction::toggled, this, &ConcordanceView::onToggleContext);
	connect(meta_action, &QAction::toggled, this, &ConcordanceView::onToggleMetadata);

	if (long_action)
	{
		connect(long_action, &QAction::toggled, this, &ConcordanceView::onToggleLayout);
	}

	connect(split_action, &QAction::toggled, this, [this](bool checked) {
		m_open_in_split = checked;
	});

	// Header double-click for column renaming
	connect(m_table->horizontalHeader(), &QHeaderView::sectionDoubleClicked,
	        this, &ConcordanceView::onHeaderDoubleClick);
}

// ── View interface ──────────────────────────────────────

QString ConcordanceView::label() const
{
	auto lbl = m_conc->label();
	auto qlabel = tabLabel(QString::fromUtf8(lbl.data(), (int) lbl.size()));
	if (m_conc->modified())
		qlabel += QStringLiteral(" *");
	return qlabel;
}

String ConcordanceView::path() const
{
	return m_conc->path();
}

bool ConcordanceView::isModified() const
{
	return m_conc->modified();
}

bool ConcordanceView::save()
{
	bool is_new = m_conc->path().empty();

	if (is_new)
	{
		// Offer the current label as candidate filename.
		auto current_label = QString::fromUtf8(m_conc->label().data(), (int) m_conc->label().size());
		auto suggested = current_label + QStringLiteral(".phon-conc");

		auto path = QFileDialog::getSaveFileName(this, tr("Save concordance..."),
			suggested, tr("Concordance (*.phon-conc)"));
		if (path.isEmpty()) return false;

		// If the user kept the suggested base name, clear the explicit label
		// so that the concordance derives it from the filename.
		auto chosen_base = QFileInfo(path).completeBaseName();
		if (chosen_base == current_label)
			m_conc->set_label(String(), false);

		m_conc->set_path(String(path.toUtf8().constData()), true);
	}
	// Force the modified flag so Document::save() doesn't skip write().
	m_conc->set_content_modified(true);
	m_conc->save();

	// Register with the project so it appears in the Data tables folder.
	if (is_new)
	{
		auto *project = Project::get();
		project->remove_temp_concordance(m_conc);
		project->data()->append(m_conc, true);
		project->register_file(m_conc->path(), m_conc);
		Project::updated();
	}

	emit titleChanged(label());
	return true;
}

void ConcordanceView::discardChanges()
{
	m_conc->discard_changes();
}

// ── Toolbar actions ─────────────────────────────────────

void ConcordanceView::onPlay()
{
	int row = selectedRow();
	if (row < 0)
	{
		QMessageBox::information(this, tr("Information"),
			tr("Select a single match to play."));
		return;
	}
	playMatch(row);
}

void ConcordanceView::onStop()
{
	stopPlayer();
}

void ConcordanceView::onViewMatch()
{
	int row = selectedRow();
	if (row < 0)
	{
		QMessageBox::information(this, tr("Information"),
			tr("Select a single match to view."));
		return;
	}
	viewMatch(row);
}

void ConcordanceView::onBookmark()
{
	int row = selectedRow();
	if (row < 0) return;

	auto &match = m_conc->get_match(row + 1);
	int target = m_target_spin->value();
	auto context = m_conc->get_context(row + 1);

	String title = match.get_value(target);
	String notes;
	auto bm = match.to_bookmark(target, title, notes, context);
	if (bm)
	{
		Project::get()->add_bookmark(std::move(bm));
		Project::updated();
	}
}

void ConcordanceView::onDeleteRows()
{
	auto rows = selectedRows();
	if (rows.isEmpty()) return;

	for (int i = rows.size() - 1; i >= 0; i--)
		m_model->removeMatch(rows[i]);

	m_conc->modify();
	updateCountLabel();
	emit titleChanged(label());
}

void ConcordanceView::onEditEvent()
{
	QMessageBox::information(this, tr("Not yet implemented"),
		tr("Event editing in concordances will be available soon."));
}

void ConcordanceView::onExportCsv()
{
	auto path = QFileDialog::getSaveFileName(this, tr("Export to CSV..."),
		QString(), tr("CSV files (*.csv *.txt)"));
	if (path.isEmpty()) return;

	try
	{
		m_conc->to_csv(String(path.toUtf8().constData()), ",");
		QMessageBox::information(this, tr("Export"), tr("Concordance exported successfully."));
	}
	catch (std::exception &e)
	{
		QMessageBox::critical(this, tr("Export error"), QString::fromUtf8(e.what()));
	}
}

void ConcordanceView::onRename()
{
	bool ok;
	auto current = QString::fromUtf8(m_conc->label().data(), (int) m_conc->label().size());
	auto name = QInputDialog::getText(this, tr("Rename concordance"), tr("New name:"),
		QLineEdit::Normal, current, &ok);
	if (ok && !name.isEmpty())
	{
		m_conc->set_label(String(name.toUtf8().constData()), true);
		emit titleChanged(label());
	}
}

// ── Set operations ──────────────────────────────────────

Handle<Concordance> ConcordanceView::pickConcordance(const QString &title)
{
	auto concordances = Project::get()->get_concordances();
	QStringList names;

	for (auto &c : concordances)
	{
		if (c.get() != m_conc.get())
			names << QString::fromUtf8(c->label().data(), (int) c->label().size());
	}

	if (names.isEmpty())
	{
		QMessageBox::information(this, title, tr("No other concordances available."));
		return {};
	}

	bool ok;
	auto choice = QInputDialog::getItem(this, title, tr("Select concordance:"), names, 0, false, &ok);
	if (!ok) return {};

	for (auto &c : concordances)
	{
		auto lbl = QString::fromUtf8(c->label().data(), (int) c->label().size());
		if (lbl == choice && c.get() != m_conc.get())
		{
			c->open();
			return c;
		}
	}

	return {};
}

void ConcordanceView::onUnion()
{
	auto other = pickConcordance(tr("Unite concordances"));
	if (!other) return;

	bool ok;
	auto name = QInputDialog::getText(this, tr("Union"), tr("Name for the result:"),
		QLineEdit::Normal, tr("Union"), &ok);
	if (!ok || name.isEmpty()) return;

	try
	{
		auto result = m_conc->unite(*other, String(name.toUtf8().constData()));
		Project::updated();
		emit addedToProject();
	}
	catch (std::exception &e)
	{
		QMessageBox::critical(this, tr("Error"), QString::fromUtf8(e.what()));
	}
}

void ConcordanceView::onIntersection()
{
	auto other = pickConcordance(tr("Intersect concordances"));
	if (!other) return;

	bool ok;
	auto name = QInputDialog::getText(this, tr("Intersect"), tr("Name for the result:"),
		QLineEdit::Normal, tr("Intersection"), &ok);
	if (!ok || name.isEmpty()) return;

	try
	{
		auto result = m_conc->intersect(*other, String(name.toUtf8().constData()));
		Project::updated();
		emit addedToProject();
	}
	catch (std::exception &e)
	{
		QMessageBox::critical(this, tr("Error"), QString::fromUtf8(e.what()));
	}
}

void ConcordanceView::onComplement()
{
	auto other = pickConcordance(tr("Complement"));
	if (!other) return;

	bool ok;
	auto name = QInputDialog::getText(this, tr("Complement"), tr("Name for the result:"),
		QLineEdit::Normal, tr("Complement"), &ok);
	if (!ok || name.isEmpty()) return;

	try
	{
		auto result = m_conc->complement(*other, String(name.toUtf8().constData()));
		Project::updated();
		emit addedToProject();
	}
	catch (std::exception &e)
	{
		QMessageBox::critical(this, tr("Error"), QString::fromUtf8(e.what()));
	}
}

// ── Display toggles ─────────────────────────────────────

void ConcordanceView::onToggleMatchInfo(bool visible)
{
	m_show_match_info = visible;
	updateColumnVisibility();
}

void ConcordanceView::onToggleContext(bool visible)
{
	m_show_context = visible;
	updateColumnVisibility();
}

void ConcordanceView::onToggleMetadata(bool visible)
{
	m_show_metadata = visible;
	updateColumnVisibility();
}

void ConcordanceView::onToggleLayout(bool long_format)
{
	m_conc->set_layout(long_format ? Concordance::Layout::Long : Concordance::Layout::Wide);
	m_model->refreshAll();
	updateCountLabel();
	updateColumnVisibility();
	m_table->resizeColumnsToContents();
}

// ── Scales (ERB / Bark) ─────────────────────────────────

void ConcordanceView::onToggleErb(bool checked)
{
	m_conc->set_has_erb(checked);
	m_model->refreshAll();
	updateColumnVisibility();
	m_table->resizeColumnsToContents();
	emit titleChanged(label());
}

void ConcordanceView::onToggleBark(bool checked)
{
	m_conc->set_has_bark(checked);
	m_model->refreshAll();
	updateColumnVisibility();
	m_table->resizeColumnsToContents();
	emit titleChanged(label());
}

// ── Pitch scales (Semitones / ERB) ────────────────────────

void ConcordanceView::onTogglePitchSemitones(bool checked)
{
	m_conc->set_has_semitones(checked);
	m_model->refreshAll();
	updateColumnVisibility();
	m_table->resizeColumnsToContents();
	emit titleChanged(label());
}

void ConcordanceView::onTogglePitchErb(bool checked)
{
	m_conc->set_has_pitch_erb(checked);
	m_model->refreshAll();
	updateColumnVisibility();
	m_table->resizeColumnsToContents();
	emit titleChanged(label());
}

// ── Column renaming ─────────────────────────────────────

void ConcordanceView::onHeaderDoubleClick(int section)
{
	// Get the current display name and the default name
	auto current_display = m_model->headerData(section, Qt::Horizontal, Qt::DisplayRole).toString();
	auto default_name = m_model->headerData(section, Qt::Horizontal, Qt::EditRole).toString();

	bool ok;
	auto new_name = QInputDialog::getText(this,
		tr("Rename column"),
		tr("Column name (leave empty or use \"%1\" to revert to default):").arg(default_name),
		QLineEdit::Normal, current_display, &ok);

	if (!ok) return;

	m_model->setHeaderData(section, Qt::Horizontal, new_name, Qt::EditRole);
	emit titleChanged(label());
}

void ConcordanceView::updateColumnVisibility()
{
	int ncol = m_model->columnCount();

	// First, show all columns to clear stale hidden states from before
	// a model reset (column indices may have shifted after adding/removing ERB/Bark).
	for (int j = 0; j < ncol; j++)
		m_table->setColumnHidden(j, false);

	// Then apply the hide rules.
	for (int j = 0; j < ncol; j++)
	{
		intptr_t col = j + 1; // 1-based for Concordance

		if (m_conc->is_file_info_column(col))
		{
			m_table->setColumnHidden(j, !m_show_match_info);
		}
		else if (m_conc->is_left_context(col) || m_conc->is_right_context(col))
		{
			m_table->setColumnHidden(j, !m_show_context);
		}
		else if (m_conc->is_metadata_column(col))
		{
			m_table->setColumnHidden(j, !m_show_metadata);
		}
		// Target and measurement columns are always visible.
	}
}

// ── Mouse events ────────────────────────────────────────

void ConcordanceView::onDoubleClick(const QModelIndex &index)
{
	if (!index.isValid()) return;

	intptr_t col = index.column() + 1;

	// If the cell is editable (measurement column), let the default editor handle it.
	// The model's flags() returns Qt::ItemIsEditable for these cells.
	if (m_conc->is_editable_measurement(col))
		return;

	// Otherwise, navigate to the match in the annotation.
	viewMatch(index.row());
}

void ConcordanceView::onContextMenu(const QPoint &pos)
{
	auto index = m_table->indexAt(pos);
	if (!index.isValid()) return;

	m_table->selectRow(index.row());
	auto &match = m_conc->get_match(index.row() + 1);

	QMenu menu(this);

	menu.addAction(QIcon(":/icons/file-search-corner.svg"), tr("View in annotation"), this, &ConcordanceView::onViewMatch);

	if (match.annotation()->has_sound())
		menu.addAction(QIcon(":/icons/play-selection.svg"), tr("Play match"), this, &ConcordanceView::onPlay);

	menu.addSeparator();

	// Edit cell — only for editable measurement columns
	intptr_t col = index.column() + 1;
	if (m_conc->is_editable_measurement(col))
	{
		menu.addAction(QIcon(":/icons/pencil-line.svg"), tr("Edit cell"), this, [this, index]() {
			m_table->edit(index);
		});
	}

	menu.addAction(QIcon(":/icons/pencil-line.svg"), tr("Edit event"), this, &ConcordanceView::onEditEvent);
	menu.addAction(QIcon(":/icons/trash-2.svg"), tr("Remove match"), this, &ConcordanceView::onDeleteRows);
	menu.addSeparator();
	menu.addAction(QIcon(":/icons/book-marked.svg"), tr("Bookmark match"), this, &ConcordanceView::onBookmark);

	menu.exec(m_table->viewport()->mapToGlobal(pos));
}

// ── Helpers ─────────────────────────────────────────────

void ConcordanceView::updateCountLabel()
{
	if (m_conc->layout() == Concordance::Layout::Long && m_conc->has_measurement_data())
	{
		auto nmatches = m_conc->row_count() / m_conc->measurement_points().size();
		auto nrows = m_conc->row_count();
		m_count_label->setText(tr("%1 match(es) \u00d7 %2 point(s) = %3 row(s)")
			.arg((int) nmatches)
			.arg((int) m_conc->measurement_points().size())
			.arg((int) nrows));
	}
	else
	{
		auto count = m_conc->row_count();
		m_count_label->setText(tr("%1 match(es)").arg((int) count));
	}
}

void ConcordanceView::stopPlayer()
{
	if (m_player)
	{
		m_player->stop();
		m_player.reset();
	}
}

void ConcordanceView::playMatch(int row)
{
	stopPlayer();

	auto &match = m_conc->get_match(row + 1);
	auto &annot = match.annotation();
	if (!annot->has_sound()) return;

	int target = m_target_spin->value();
	double start = match.get_start_time(target);
	double end = match.get_end_time(target);

	try
	{
		auto sound = annot->sound();
		sound->open(); // Ensure audio data is loaded from disk.
		m_player = std::make_unique<AudioPlayer>(sound);
		m_player->play(start, end);
	}
	catch (std::exception &e)
	{
		QMessageBox::warning(this, tr("Playback error"), QString::fromUtf8(e.what()));
		m_player.reset();
	}
}

void ConcordanceView::viewMatch(int row)
{
	auto &match = m_conc->get_match(row + 1);
	int target = m_target_spin->value();
	auto layer = match.get_layer(target);
	auto start = match.get_start_time(target);
	auto end = match.get_end_time(target);

	emit openAnnotation(match.annotation(), layer, start, end, m_open_in_split);
}

int ConcordanceView::selectedRow() const
{
	auto sel = m_table->selectionModel()->selectedRows();
	if (sel.size() != 1) return -1;
	return sel.first().row();
}

QList<int> ConcordanceView::selectedRows() const
{
	QList<int> rows;
	for (auto &idx : m_table->selectionModel()->selectedRows())
		rows.append(idx.row());
	std::sort(rows.begin(), rows.end());
	return rows;
}

} // namespace phonometrica
