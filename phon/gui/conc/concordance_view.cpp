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
#include <phon/gui/conc/concordance_view.hpp>
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
	auto *view_action = m_toolbar->addAction(QIcon(":/icons/file-search-corner.svg"), tr("View"));
	view_action->setToolTip(tr("Open match in annotation"));

	m_toolbar->addSeparator();

	// -- Editing --
	auto *bookmark_action = m_toolbar->addAction(QIcon(":/icons/book-marked.svg"), tr("Bookmark"));
	bookmark_action->setToolTip(tr("Bookmark selected match"));

	m_toolbar->addSeparator();

	// Placeholder icons: trash-2.svg and pencil-line.svg are not yet in the project.
	// Add them from Lucide (https://lucide.dev) and register in CMakeLists.txt.
	auto *del_action = m_toolbar->addAction(QIcon(":/icons/trash-2.svg"), tr("Delete"));
	del_action->setToolTip(tr("Delete selected row(s)"));

	auto *edit_action = m_toolbar->addAction(QIcon(":/icons/pencil-line.svg"), tr("Edit"));
	edit_action->setToolTip(tr("Edit selected event"));

	m_toolbar->addSeparator();

	// -- Set operations --
	auto *union_action = m_toolbar->addAction(QIcon(":/icons/set-union.svg"), tr("Union"));
	union_action->setToolTip(tr("Unite with another concordance (A \u222a B)"));

	// Placeholder icon: circle-dot.svg is not yet in the project.
	auto *intersect_action = m_toolbar->addAction(QIcon(":/icons/set-intersection.svg"), tr("Intersect"));
	intersect_action->setToolTip(tr("Intersect with another concordance (A \u2229 B)"));

	auto *compl_action = m_toolbar->addAction(QIcon(":/icons/set-complement.svg"), tr("Complement"));
	compl_action->setToolTip(tr("Get complement (B \u2216 A)"));

	m_toolbar->addSeparator();

	// Placeholder icon: tag.svg is not yet in the project.
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
	ctx_action->setChecked(m_show_context);
	ctx_action->setToolTip(tr("Show left and right context"));

	auto *meta_action = display_menu->addAction(tr("Metadata"));
	meta_action->setCheckable(true);
	meta_action->setChecked(m_show_metadata);
	meta_action->setToolTip(tr("Show file description and properties"));

	auto *display_action = new QAction(QIcon(":/icons/display.svg"), tr("Display settings"), this);
	display_action->setMenu(display_menu);
	m_toolbar->addAction(display_action);
	if (auto *db = qobject_cast<QToolButton *>(m_toolbar->widgetForAction(display_action)))
		db->setPopupMode(QToolButton::InstantPopup);

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
	m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
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
}

// ── View interface ──────────────────────────────────────

QString ConcordanceView::label() const
{
	auto lbl = m_conc->label();
	auto qlabel = QString::fromUtf8(lbl.data(), (int) lbl.size());
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
		auto path = QFileDialog::getSaveFileName(this, tr("Save concordance..."),
			QString(), tr("Concordance (*.phon-conc)"));
		if (path.isEmpty()) return false;
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

void ConcordanceView::updateColumnVisibility()
{
	int ncol = m_model->columnCount();

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
		// Target columns are always visible.
	}
}

// ── Mouse events ────────────────────────────────────────

void ConcordanceView::onDoubleClick(const QModelIndex &index)
{
	if (!index.isValid()) return;
	playMatch(index.row());
}

void ConcordanceView::onContextMenu(const QPoint &pos)
{
	auto index = m_table->indexAt(pos);
	if (!index.isValid()) return;

	m_table->selectRow(index.row());
	auto &match = m_conc->get_match(index.row() + 1);

	QMenu menu(this);

	if (match.annotation()->has_sound())
		menu.addAction(QIcon(":/icons/play-selection.svg"), tr("Play match"), this, &ConcordanceView::onPlay);

	menu.addAction(QIcon(":/icons/file-search-corner.svg"), tr("View in annotation"), this, &ConcordanceView::onViewMatch);
	menu.addSeparator();
	menu.addAction(QIcon(":/icons/pencil-line.svg"), tr("Edit event"), this, &ConcordanceView::onEditEvent);
	menu.addAction(QIcon(":/icons/trash-2.svg"), tr("Remove match"), this, &ConcordanceView::onDeleteRows);
	menu.addSeparator();
	menu.addAction(QIcon(":/icons/book-marked.svg"), tr("Bookmark match"), this, &ConcordanceView::onBookmark);

	menu.exec(m_table->viewport()->mapToGlobal(pos));
}

// ── Helpers ─────────────────────────────────────────────

void ConcordanceView::updateCountLabel()
{
	auto count = m_conc->row_count();
	m_count_label->setText(tr("%1 match(es)").arg((int) count));
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

	emit openAnnotation(match.annotation(), layer, start, end);
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
