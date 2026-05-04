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

#include <cmath>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFormLayout>
#include <QDialog>
#include <QDialogButtonBox>
#include <QHeaderView>
#include <QItemSelectionModel>
#include <QMenu>
#include <QEvent>
#include <QMouseEvent>
#include <QKeyEvent>
#include <QShowEvent>
#include <QPainter>
#include <QAction>
#include <QActionGroup>
#include <QToolButton>
#include <QMessageBox>
#include <QInputDialog>
#include <QGuiApplication>
#include <QClipboard>
#include <phon/gui/file_dialog.hpp>
#include <QFileInfo>
#include <algorithm>
#include <phon/gui/conc/concordance_view.hpp>
#include <phon/gui/conc/concordance_commands.hpp>
#include <phon/gui/conc/protocol_builder_dialog.hpp>
#include <phon/gui/bookmark_editor.hpp>
#include <phon/gui/recode_dialog.hpp>
#include <phon/gui/transform_dialog.hpp>
#include <phon/gui/convert_to_text_dialog.hpp>
#include <phon/gui/outlier_dialog.hpp>
#include <phon/gui/help_browser.hpp>
#include <phon/gui/font_helpers.hpp>
#include <phon/gui/vowel_normalization_dialog.hpp>
#include <phon/application/vowel_normalizer.hpp>
#include <phon/application/project.hpp>
#include <phon/application/dataset.hpp>
#include <phon/application/settings.hpp>
#include <phon/application/praat.hpp>
#include <phon/application/protocol.hpp>
#include <phon/application/protocol_apply.hpp>
#include <phon/analysis/column_metrics.hpp>
#include <phon/analysis/formula_engine.hpp>

namespace phonometrica {

// ─────────────────────────────────────────────────
//  ConcHeaderView
// ─────────────────────────────────────────────────
// Horizontal header that paints an overlay on the "current" column, i.e. the
// column of the view's current index. We cannot use Qt's built-in
// `highlightSections` here because it mirrors the whole selection, and in
// SelectRows mode a row selection intersects every column — lighting up the
// entire header strip. Tracking the current index instead gives us:
//   • Click cell (r, c)   → currentIndex.column() == c → header c highlighted
//   • Click header c      → selectWholeColumn sets current to (0, c) → same
// so both interactions produce the same "this is the active column" cue.
namespace {

class ConcHeaderView : public QHeaderView
{
public:

	explicit ConcHeaderView(QWidget *parent = nullptr) :
		QHeaderView(Qt::Horizontal, parent) {}

	void setCurrentColumn(int col)
	{
		if (m_current_col == col) return;
		m_current_col = col;
		viewport()->update();
	}

protected:

	// Overriding paintEvent rather than paintSection turned out to be necessary
	// in practice: on some platform styles, paintSection-based overlays were
	// silently dropped (possibly absorbed by native-style repaints after the
	// per-section call returned). Painting on the viewport AFTER the base
	// class's paintEvent completes is unambiguous — we're just drawing a last
	// pass on top of whatever Qt produced.
	void paintEvent(QPaintEvent *e) override
	{
		QHeaderView::paintEvent(e);

		if (m_current_col < 0 || m_current_col >= count()) return;

		int pos = sectionViewportPosition(m_current_col);
		int size = sectionSize(m_current_col);
		if (size <= 0) return;

		QPainter painter(viewport());
		QRect r(pos, 0, size, viewport()->height());
		// Translucent fill + solid underline. Explicit RGBA rather than a
		// palette role — palette resolution is unreliable on Windows native.
		painter.fillRect(r, QColor(70, 130, 200, 90));
		painter.setPen(QPen(QColor(40, 100, 180), 2));
		painter.drawLine(r.bottomLeft(), r.bottomRight());
	}

private:

	int m_current_col = -1;
};

} // anonymous namespace

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
	// Space is dispatched locally (no MainWindow-level claim on it); Esc and
	// Ctrl+Return are claimed globally by MainWindow and reach us via escape()
	// and execute() overrides, so we don't bind them here (would be "ambiguous
	// shortcut overload"). Tooltips still advertise all three to the user.
	auto *play_action = m_toolbar->addAction(QIcon(":/icons/play.svg"), tr("Play"));
	play_action->setToolTip(tr("Play selected match (Space)"));
	play_action->setShortcut(QKeySequence(Qt::Key_Space));
	play_action->setShortcutContext(Qt::WidgetWithChildrenShortcut);
	addAction(play_action);

	auto *stop_action = m_toolbar->addAction(QIcon(":/icons/square.svg"), tr("Stop"));
	stop_action->setToolTip(tr("Stop playback (Esc)"));

	// -- Navigation --
	auto *view_action = m_toolbar->addAction(QIcon(":/icons/scan-eye.svg"), tr("View"));
	view_action->setToolTip(tr("Open match in annotation (Ctrl+Return)"));

	m_toolbar->addSeparator();

	// -- Editing --
	auto *bookmark_action = m_toolbar->addAction(QIcon(":/icons/book-marked.svg"), tr("Bookmark"));
	bookmark_action->setToolTip(tr("Bookmark selected match"));

	m_toolbar->addSeparator();

	auto *del_action = m_toolbar->addAction(QIcon(":/icons/grid-2x2-x.svg"), tr("Delete"));
	del_action->setToolTip(tr("Delete selected row(s)"));

	auto *edit_action = m_toolbar->addAction(QIcon(":/icons/pencil-line.svg"), tr("Edit match text"));
	edit_action->setToolTip(tr("Edit selected match text (concordance only)"));

	m_toolbar->addSeparator();

	// -- Set operations --
	auto *union_action = m_toolbar->addAction(QIcon(":/icons/set-union.svg"), tr("Union"));
	union_action->setToolTip(tr("Unite with another concordance (A \u222a B)"));

	auto *intersect_action = m_toolbar->addAction(QIcon(":/icons/set-intersection.svg"), tr("Intersect"));
	intersect_action->setToolTip(tr("Intersect with another concordance (A \u2229 B)"));

	auto *compl_action = m_toolbar->addAction(QIcon(":/icons/set-complement.svg"), tr("Complement"));
	compl_action->setToolTip(tr("Get complement (A \u2216 B)"));

	auto *merge_action = m_toolbar->addAction(QIcon(":/icons/merge-tables.svg"), tr("Merge"));
	merge_action->setToolTip(tr("Horizontal merge: add columns from another table"));

	m_toolbar->addSeparator();

	auto *rename_action = m_toolbar->addAction(QIcon(":/icons/tag.svg"), tr("Rename"));
	rename_action->setToolTip(tr("Rename concordance..."));

	m_toolbar->addSeparator();

	auto *analyze_action = m_toolbar->addAction(QIcon(":/icons/statistics.svg"), tr("Analyze"));
	analyze_action->setToolTip(tr("Open analysis view for this concordance"));

	m_toolbar->addSeparator();

	// -- Filter / Subset --
	m_filter_action = m_toolbar->addAction(QIcon(":/icons/filter.svg"), tr("Filter"));
	m_filter_action->setToolTip(tr("Show/hide the filter bar"));
	m_filter_action->setCheckable(true);

	m_clear_filter_action = m_toolbar->addAction(QIcon(":/icons/filter-x.svg"), tr("Clear filters"));
	m_clear_filter_action->setToolTip(tr("Remove all filter rules"));
	m_clear_filter_action->setEnabled(false);

	m_subset_action = m_toolbar->addAction(QIcon(":/icons/scissors.svg"), tr("Subset"));
	m_subset_action->setToolTip(tr("Create a new concordance from the visible (filtered) rows"));
	m_subset_action->setEnabled(false);

	auto *metric_action = m_toolbar->addAction(QIcon(":/icons/sigma.svg"), tr("Metric column"));
	metric_action->setToolTip(tr("Compute a distance metric (z-score, etc.) for outlier detection"));

	auto *norm_action = m_toolbar->addAction(QIcon(":/icons/vowel-space.svg"), tr("Normalize vowels"));
	norm_action->setToolTip(tr("Apply vowel normalization (Lobanov, Nearey, Watt & Fabricius)"));

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

	// Insert this here since time appears between the context and metadata
	bool is_measurement_query = m_conc->nformant() > 0 || m_conc->is_pitch() || m_conc->is_intensity() || m_conc->is_spectral_moments();
	QAction *time_action = nullptr;
	if (is_measurement_query)
	{
		time_action = display_menu->addAction(tr("Measurement time"));
		time_action->setCheckable(true);
		time_action->setChecked(m_conc->has_time());
		time_action->setToolTip(tr("Show the absolute time (seconds) at which each measurement was taken"));
	}

	auto *meta_action = display_menu->addAction(tr("Metadata"));
	meta_action->setCheckable(true);
	meta_action->setChecked(m_show_metadata);
	meta_action->setToolTip(tr("Show file description and properties"));

	// Wide/Long toggle — shown for formant, pitch, intensity, spectral moments concordances;
	// enabled only for n-point data
	QAction *long_action = nullptr;
	if (is_measurement_query)
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

	auto *highlight_action = display_menu->addAction(tr("Highlight targets"));
	highlight_action->setCheckable(true);
	highlight_action->setChecked(m_conc->highlight_targets());
	highlight_action->setToolTip(tr("Show target columns in bold red"));

	auto *display_action = new QAction(QIcon(":/icons/display.svg"), tr("Display settings"), this);
	display_action->setMenu(display_menu);
	m_toolbar->addAction(display_action);
	if (auto *db = qobject_cast<QToolButton *>(m_toolbar->widgetForAction(display_action)))
		db->setPopupMode(QToolButton::InstantPopup);

	// -- Scales menu — shows relevant sections based on native + merged measurement types --
	bool need_scales = (m_conc->nformant() > 0) || m_conc->is_pitch()
	                 || m_conc->has_aux_pitch() || m_conc->has_aux_formant();
	if (need_scales)
	{
		auto *scales_menu = new QMenu(this);

		// Native formant scales
		if (m_conc->nformant() > 0)
		{
			m_erb_action = scales_menu->addAction(tr("ERB values"));
			m_erb_action->setCheckable(true);
			m_erb_action->setChecked(m_conc->has_erb());
			m_erb_action->setToolTip(tr("Show formant values converted to the ERB scale"));

			m_bark_action = scales_menu->addAction(tr("Bark values"));
			m_bark_action->setCheckable(true);
			m_bark_action->setChecked(m_conc->has_bark());
			m_bark_action->setToolTip(tr("Show formant values converted to the Bark scale"));

			connect(m_erb_action, &QAction::toggled, this, &ConcordanceView::onToggleErb);
			connect(m_bark_action, &QAction::toggled, this, &ConcordanceView::onToggleBark);
		}

		// Native pitch scales
		if (m_conc->is_pitch())
		{
			if (m_conc->nformant() > 0) scales_menu->addSeparator();

			m_pitch_st_action = scales_menu->addAction(tr("Semitones"));
			m_pitch_st_action->setCheckable(true);
			m_pitch_st_action->setChecked(m_conc->has_semitones());
			m_pitch_st_action->setToolTip(tr("Show pitch values converted to semitones"));

			m_pitch_erb_action = scales_menu->addAction(tr("ERB rate"));
			m_pitch_erb_action->setCheckable(true);
			m_pitch_erb_action->setChecked(m_conc->has_pitch_erb());
			m_pitch_erb_action->setToolTip(tr("Show pitch values converted to the ERB scale"));

			connect(m_pitch_st_action, &QAction::toggled, this, &ConcordanceView::onTogglePitchSemitones);
			connect(m_pitch_erb_action, &QAction::toggled, this, &ConcordanceView::onTogglePitchErb);
		}

		// Merged F0 scales
		if (m_conc->has_aux_pitch())
		{
			if (m_conc->nformant() > 0 || m_conc->is_pitch()) scales_menu->addSeparator();

			m_aux_pitch_st_action = scales_menu->addAction(tr("F0 \u2192 Semitones"));
			m_aux_pitch_st_action->setCheckable(true);
			m_aux_pitch_st_action->setChecked(m_conc->aux_pitch_semitones());
			m_aux_pitch_st_action->setToolTip(tr("Show F0 values converted to semitones"));

			m_aux_pitch_erb_action = scales_menu->addAction(tr("F0 \u2192 ERB rate"));
			m_aux_pitch_erb_action->setCheckable(true);
			m_aux_pitch_erb_action->setChecked(m_conc->aux_pitch_erb());
			m_aux_pitch_erb_action->setToolTip(tr("Show F0 values converted to the ERB scale"));

			connect(m_aux_pitch_st_action, &QAction::toggled, this, &ConcordanceView::onToggleAuxPitchSemitones);
			connect(m_aux_pitch_erb_action, &QAction::toggled, this, &ConcordanceView::onToggleAuxPitchErb);
		}

		// Merged formant scales
		if (m_conc->has_aux_formant())
		{
			if (m_conc->nformant() > 0 || m_conc->is_pitch() || m_conc->has_aux_pitch())
				scales_menu->addSeparator();

			m_aux_formant_erb_action = scales_menu->addAction(tr("formants \u2192 ERB"));
			m_aux_formant_erb_action->setCheckable(true);
			m_aux_formant_erb_action->setChecked(m_conc->aux_formant_erb());
			m_aux_formant_erb_action->setToolTip(tr("Show formant values converted to ERB"));

			m_aux_formant_bark_action = scales_menu->addAction(tr("formants \u2192 Bark"));
			m_aux_formant_bark_action->setCheckable(true);
			m_aux_formant_bark_action->setChecked(m_conc->aux_formant_bark());
			m_aux_formant_bark_action->setToolTip(tr("Show formant values converted to Bark"));

			connect(m_aux_formant_erb_action, &QAction::toggled, this, &ConcordanceView::onToggleAuxFormantErb);
			connect(m_aux_formant_bark_action, &QAction::toggled, this, &ConcordanceView::onToggleAuxFormantBark);
		}

		auto *scales_action = new QAction(QIcon(":/icons/ruler.svg"), tr("Scales"), this);
		scales_action->setMenu(scales_menu);
		m_toolbar->addAction(scales_action);
		if (auto *sb = qobject_cast<QToolButton *>(m_toolbar->widgetForAction(scales_action)))
			sb->setPopupMode(QToolButton::InstantPopup);
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

	// ── Filter bar (hidden by default) ────────────────
	m_model = new ConcordanceModel(m_conc, this);
	m_proxy = new DataFilterProxyModel(this);
	m_proxy->setSourceModel(m_model);

	// Refresh column layout when a script appends a column (Project::updated()
	// fires notify_update; the scoped_connection auto-disconnects on destruction).
	if (auto *project = Project::get()) {
		m_project_update_conn = project->notify_update.connect([this]() {
			m_model->refreshAll();
			m_table->resizeColumnsToContents();
		});
	}

	m_filter_bar = new FilterBar(m_proxy, this);
	m_filter_bar->hide();
	setupFilterBar();
	layout->addWidget(m_filter_bar);

	// Restore saved filter rules from the document.
	if (!m_conc->filter_rules().empty())
	{
		auto &saved = m_conc->filter_rules();
		// Apply logic BEFORE adding rules so any re-evaluation uses the right one.
		m_proxy->setLogic(string_to_filter_logic(m_conc->filter_logic().data()));
		for (intptr_t r = 1; r <= saved.size(); r++)
		{
			auto &rd = saved[r];
			FilterRule rule;
			intptr_t col = m_conc->find_column(rd.column);
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
		m_proxy->setFilterEnabled(m_conc->filter_enabled());
		m_filter_bar->rebuild();  // rebuild() also syncs the radio buttons to the proxy's logic
		m_filter_bar->show();
		m_filter_action->setChecked(true);
	}

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
	m_table = new QTableView;
	m_table->setModel(m_proxy);
	m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
	m_table->setSelectionMode(QAbstractItemView::ExtendedSelection);
	// Allow double-click editing on editable cells (measurement columns).
	// The model's flags() method controls which cells are actually editable.
	m_table->setEditTriggers(QAbstractItemView::DoubleClicked | QAbstractItemView::EditKeyPressed);
	m_table->setAlternatingRowColors(true);
	m_table->verticalHeader()->setDefaultSectionSize(28);

	// Install our column-aware header so the current-focus column is highlighted.
	// Must happen before any per-header configuration below.
	auto *hdr = new ConcHeaderView(m_table);
	m_table->setHorizontalHeader(hdr);
	hdr->setStretchLastSection(false);

	m_table->setContextMenuPolicy(Qt::CustomContextMenu);
	m_table->setSortingEnabled(false);

	// Sorting is handled via the header context menu (right-click); left-click
	// selects the whole column as a visual cue that column-level operations
	// (sort, rename, recode, transform, delete) are available.
	hdr->setSectionsClickable(true);
	// Don't mirror the cell-selection state onto the header — row selection
	// intersects every column, which would light up the entire header strip.
	// The current-column highlight (see ConcHeaderView::paintEvent) takes
	// care of showing which column is in focus.
	hdr->setHighlightSections(false);
	hdr->setSortIndicatorShown(false);
	hdr->setContextMenuPolicy(Qt::CustomContextMenu);
	hdr->setToolTip(tr("Click to select column; right-click for options (sort, rename, recode, transform)"));

	// Track the current index so the header highlight follows cell clicks
	// (row selection) as well as header clicks (column selection). We wire
	// multiple signals for robustness: currentChanged covers keyboard nav,
	// clicked covers mouse on cells, and selectWholeColumn calls the setter
	// directly for the header-click path.
	connect(m_table->selectionModel(), &QItemSelectionModel::currentChanged, this,
		[hdr](const QModelIndex &current, const QModelIndex &) {
			hdr->setCurrentColumn(current.isValid() ? current.column() : -1);
		});
	connect(m_table, &QAbstractItemView::clicked, this,
		[hdr](const QModelIndex &index) {
			hdr->setCurrentColumn(index.isValid() ? index.column() : -1);
		});

	// Monospace font for the table body.
	m_table->setFont(defaultMonoFont());

	// Hover-tracking for the editable-cell cursor hint. With mouse tracking on,
	// MouseMove events reach the viewport even when no button is held down; the
	// event filter below swaps the cursor to IBeam over editable cells.
	m_table->setMouseTracking(true);
	m_table->viewport()->setMouseTracking(true);
	m_table->viewport()->installEventFilter(this);
	// Also filter the table proper, for keyboard events (the viewport only
	// receives mouse events; keystrokes go to the table widget).
	m_table->installEventFilter(this);

	// When the tab system or any caller focuses the ConcordanceView, hand
	// focus transparently to the table. Without this, Qt's tab-activation
	// machinery focuses the view itself, keystrokes go nowhere interesting,
	// and e.g. the Down-arrow empty-state shortcut doesn't see any events.
	setFocusProxy(m_table);

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
	connect(edit_action, &QAction::triggered, this, &ConcordanceView::onEditMatchText);
	connect(union_action, &QAction::triggered, this, &ConcordanceView::onUnion);
	connect(intersect_action, &QAction::triggered, this, &ConcordanceView::onIntersection);
	connect(compl_action, &QAction::triggered, this, &ConcordanceView::onComplement);
	connect(merge_action, &QAction::triggered, this, &ConcordanceView::onMerge);
	connect(rename_action, &QAction::triggered, this, &ConcordanceView::onRename);
	connect(analyze_action, &QAction::triggered, this, [this]() {
		emit requestAnalysis(m_conc);
	});
	connect(m_filter_action, &QAction::toggled, this, &ConcordanceView::onToggleFilter);
	connect(m_clear_filter_action, &QAction::triggered, this, &ConcordanceView::onClearFilters);
	connect(m_subset_action, &QAction::triggered, this, &ConcordanceView::onCreateSubset);
	connect(metric_action, &QAction::triggered, this, &ConcordanceView::onAddMetricColumn);
	connect(norm_action, &QAction::triggered, this, &ConcordanceView::onNormalizeVowels);
	connect(m_table, &QTableView::doubleClicked, this, &ConcordanceView::onDoubleClick);
	connect(m_table, &QTableView::customContextMenuRequested, this, &ConcordanceView::onContextMenu);
	connect(m_model, &ConcordanceModel::cellEdited, this, &ConcordanceView::onCellEdited);

	connect(info_action, &QAction::toggled, this, &ConcordanceView::onToggleMatchInfo);
	connect(ctx_action, &QAction::toggled, this, &ConcordanceView::onToggleContext);
	connect(meta_action, &QAction::toggled, this, &ConcordanceView::onToggleMetadata);

	if (long_action)
	{
		connect(long_action, &QAction::toggled, this, &ConcordanceView::onToggleLayout);
	}

	if (time_action)
	{
		connect(time_action, &QAction::toggled, this, &ConcordanceView::onToggleMeasurementTime);
	}

	connect(split_action, &QAction::toggled, this, [this](bool checked) {
		m_open_in_split = checked;
	});

	connect(highlight_action, &QAction::toggled, this, [this](bool checked) {
		m_conc->set_highlight_targets(checked);
		m_conc->modify();
		Document::file_modified();
		m_model->refreshAll();
		emit titleChanged(label());
	});

	// Header context menu: sort + rename + recode + transform
	connect(hdr, &QHeaderView::sectionClicked, this, &ConcordanceView::selectWholeColumn);

	connect(hdr, &QHeaderView::customContextMenuRequested, this, [this](const QPoint &pos) {
		auto *header = m_table->horizontalHeader();
		int section = header->logicalIndexAt(pos);
		if (section < 0) return;

		// Select the column first so there's a visual cue before the menu appears.
		selectWholeColumn(section);

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
		menu.addAction(tr("Rename column..."), this, [this, section]() {
			onHeaderDoubleClick(section);
		});

		// ── Delete (aux columns only) ──────────────────
		intptr_t aux_idx = m_conc->resolve_aux_column(col_1based);
		if (aux_idx > 0)
		{
			// Walk back to the first display column of this aux group to get the base header name.
			intptr_t first_col = col_1based;
			while (first_col > 1 && m_conc->resolve_aux_column(first_col - 1) == aux_idx) {
				first_col--;
			}
			auto base_name_str = m_conc->get_default_header(first_col);
			auto base_name = QString::fromUtf8(base_name_str.data(), (int) base_name_str.size());

			menu.addAction(QIcon(":/icons/circle-minus.svg"), tr("Delete column \"%1\"").arg(base_name),
				this, [this, aux_idx, base_name]()
			{
				auto answer = QMessageBox::question(this, tr("Delete column"),
					tr("Delete column \"%1\"?").arg(base_name),
					QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
				if (answer != QMessageBox::Yes) return;

				m_conc->remove_aux_column(aux_idx);
				m_model->refreshAll();
				m_table->resizeColumnsToContents();
				updateColumnVisibility();
				setupFilterBar();
				updateCountLabel();
				emit titleChanged(label());
			});
		}

		// ── Detect column type by sampling cells ───────
		intptr_t nrows = m_conc->row_count();
		intptr_t sample_limit = std::min(nrows, (intptr_t)20);
		bool all_numeric = true;
		bool found_any = false;

		for (intptr_t i = 1; i <= sample_limit; i++) {
			auto cell = m_conc->get_cell(i, col_1based);
			if (cell.empty() || cell == "nan") continue;
			found_any = true;
			bool ok;
			cell.to_float(&ok);
			if (!ok) {
				all_numeric = false;
				break;
			}
		}

		bool is_numeric = found_any && all_numeric;
		bool is_text = found_any && !all_numeric;

		// ── Recode (text columns only) ─────────────────
		if (is_text)
		{
			menu.addSeparator();
			menu.addAction(tr("Recode values..."), this, [this, section]() {
				onRecodeColumn(section);
			});
			menu.addAction(tr("Apply coding protocol..."), this, [this, col_1based]() {
				auto qpath = getOpenFileName(this, tr("Apply coding protocol"),
					tr("Coding protocols (*.json);;All files (*)"));
				if (qpath.isEmpty()) return;

				String path(qpath.toUtf8().constData());

				// Load the protocol. The Runtime is needed because Phonometrica parses JSON via the
				// scripting engine (JSON is a subset of the Phon script language).
				std::shared_ptr<Protocol> protocol;
				try {
					protocol = std::make_shared<Protocol>(Project::get()->runtime(), path);
				}
				catch (std::exception &e) {
					QMessageBox::critical(this, tr("Protocol error"),
						tr("Could not load protocol:\n%1").arg(QString::fromUtf8(e.what())));
					return;
				}

				// Apply the protocol to the clicked column. Structural errors (e.g. malformed
				// match_all) throw; per-row parse failures are collected in result.failed_rows.
				ProtocolApplyResult result;
				try {
					result = m_conc->apply_protocol(col_1based, *protocol, /*translate=*/true);
				}
				catch (std::exception &e) {
					QMessageBox::critical(this, tr("Apply coding protocol"),
						tr("Could not apply protocol:\n%1").arg(QString::fromUtf8(e.what())));
					return;
				}

				// Refresh the view so the newly-appended aux columns become visible. Mirrors the
				// refresh sequence used after remove_aux_column above.
				m_model->refreshAll();
				m_table->resizeColumnsToContents();
				updateColumnVisibility();
				setupFilterBar();
				updateCountLabel();
				emit titleChanged(label());

				if (!result.failed_rows.empty() || !result.untranslated_rows.empty()) {
					QStringList msgs;
					if (!result.failed_rows.empty()) {
						msgs << tr("%1 row(s) did not match the protocol and were left blank.")
							.arg((qlonglong) result.failed_rows.size());
					}
					if (!result.untranslated_rows.empty()) {
						msgs << tr("%1 row(s) contain values not defined in the protocol; "
						           "raw values were kept.")
							.arg((qlonglong) result.untranslated_rows.size());
					}
					QMessageBox::warning(this, tr("Apply coding protocol"),
						msgs.join(QStringLiteral("\n\n")));
				}
			});
			menu.addAction(tr("Build coding protocol..."), this, [this, col_1based]() {
				// Seed the dialog with the first 10 values of the clicked column so the user
				// has concrete sample input to iterate the protocol against.
				Array<String> samples;
				intptr_t n_rows = m_conc->row_count();
				intptr_t n = std::min<intptr_t>(n_rows, 10);
				for (intptr_t i = 1; i <= n; i++) {
					samples.append(m_conc->get_cell(i, col_1based));
				}

				ProtocolBuilderDialog dlg(Project::get()->runtime(), m_conc, col_1based, this);
				dlg.setSampleText(samples);
				if (dlg.exec() == QDialog::Accepted) {
					// Apply was clicked and succeeded inside the dialog; refresh the view.
					m_model->refreshAll();
					m_table->resizeColumnsToContents();
					updateColumnVisibility();
					setupFilterBar();
					updateCountLabel();
					emit titleChanged(label());
				}
			});
		}

		// ── Transform (numeric columns only) ───────────
		if (is_numeric)
		{
			menu.addSeparator();
			menu.addAction(tr("Transform..."), this, [this, section]() {
				onTransformColumn(section);
			});
			menu.addAction(tr("Convert to text..."), this, [this, section]() {
				onConvertToText(section);
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
				auto h = m_conc->get_header(r.column + 1);
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
		m_conc->set_filter_rules(std::move(saved), m_proxy->isFilterEnabled(),
		                        String(filter_logic_to_string(m_proxy->logic())));
	});

	// When a cell edit causes a row to fall out of (or back into) the current
	// filter, QSortFilterProxyModel re-evaluates filterAcceptsRow dynamically
	// and emits rowsRemoved/rowsInserted on the proxy — but NOT filterChanged
	// (that signal is for rule/logic/enabled changes only). Refresh the count
	// label on those signals so "Showing X of Y" stays in sync with the view.
	connect(m_proxy, &QAbstractItemModel::rowsInserted, this,
	        [this](const QModelIndex&, int, int) { updateCountLabel(); });
	connect(m_proxy, &QAbstractItemModel::rowsRemoved, this,
	        [this](const QModelIndex&, int, int) { updateCountLabel(); });

	// The filter bar's radio buttons push logic changes to the proxy;
	// the proxy then fires filterChanged, which the handler above serializes.
	connect(m_filter_bar, &FilterBar::logicChanged, m_proxy, &DataFilterProxyModel::setLogic);

	connect(m_filter_bar, &FilterBar::addBooleanColumnRequested,
	        this, &ConcordanceView::onAddBooleanColumn);
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
		// Offer the current label as candidate filename; placeholder labels
		// like "Untitled" collapse to lowercase "untitled" so all save
		// dialogs propose the same fallback (see file_dialog.hpp).
		auto current_label = normalizedSaveLabel(m_conc->label());
		auto suggested = current_label + QStringLiteral(".phon-conc");

		auto path = getSaveFileName(this, tr("Save concordance..."),
			tr("Concordance (*.phon-conc)"), suggested);
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
	if (m_conc->has_path()) {
		m_conc->reload();
	} else {
		// In-memory document: remove from project tree since there's nothing to revert to.
		m_conc->detach();
		Project::updated();
	}
}

// Ctrl+Return — dispatched from MainWindow::onExecuteCurrentView when this
// view is active. Semantics for a concordance: open the selected match in its
// annotation (what the "View" toolbar button does).
void ConcordanceView::execute()
{
	onViewMatch();
}

// Escape — dispatched from MainWindow::onEscapeCurrentView. For a concordance,
// the most useful cancel action is stopping any ongoing match playback.
void ConcordanceView::escape()
{
	onStop();
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
	playMatch(mapToSourceRow(row));
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
	viewMatch(mapToSourceRow(row));
}

void ConcordanceView::onBookmark()
{
	int row = selectedRow();
	if (row < 0) return;

	int src = mapToSourceRow(row);
	auto &match = m_conc->get_match(src + 1);
	int target = m_target_spin->value();
	auto context = m_conc->get_context(src + 1);

	// Default title is the match value; the user can edit it in the dialog.
	String match_value = match.get_value(target);
	QString default_title = QString::fromUtf8(match_value.data(), (int) match_value.size());

	// Build a human-readable context preview for the dialog header:
	// "« left »  <match>  « right »" — compact but enough to disambiguate.
	const auto &left  = context.first;
	const auto &right = context.second;
	QString preview;
	{
		QString q_left  = QString::fromUtf8(left.data(), (int) left.size()).trimmed();
		QString q_match = default_title;
		QString q_right = QString::fromUtf8(right.data(), (int) right.size()).trimmed();
		preview = tr("Match: %1 %2 %3")
			.arg(q_left.isEmpty() ? QString()  : QStringLiteral("… ") + q_left)
			.arg(QStringLiteral("〈") + q_match + QStringLiteral("〉"))
			.arg(q_right.isEmpty() ? QString() : q_right + QStringLiteral(" …"))
			.simplified();
	}

	BookmarkEditor dlg(default_title, QString(), preview, this);
	if (dlg.exec() != QDialog::Accepted)
		return;

	QByteArray t_utf8 = dlg.title().toUtf8();
	QByteArray n_utf8 = dlg.notes().toUtf8();
	String title(t_utf8.constData(), t_utf8.size());
	String notes(n_utf8.constData(), n_utf8.size());

	auto bm = match.to_bookmark(target, title, notes, context);
	if (bm)
	{
		Project::get()->add_bookmark(std::move(bm));
		Project::updated();
	}
}

void ConcordanceView::onDeleteRows()
{
	auto proxy_rows = selectedRows();
	if (proxy_rows.isEmpty()) return;

	// Map proxy rows to source rows.
	QList<int> source_rows;
	for (int pr : proxy_rows) {
		source_rows.append(mapToSourceRow(pr));
	}
	std::sort(source_rows.begin(), source_rows.end());

	// Remove from bottom to top, collecting removed matches for undo.
	std::vector<DeleteMatchesCommand::RemovedMatch> removed;
	removed.reserve(source_rows.size());
	for (int i = source_rows.size() - 1; i >= 0; i--)
	{
		int row = source_rows[i];
		auto match = m_model->removeMatch(row);
		removed.push_back({row, std::move(match)});
	}

	// Reverse so they're sorted ascending by source_row (for undo insertion order).
	std::reverse(removed.begin(), removed.end());

	m_conc->modify();
	Document::file_modified();
	updateCountLabel();
	emit titleChanged(label());

	auto cmd = std::make_unique<DeleteMatchesCommand>(this, std::move(removed));
	record(std::move(cmd));
}

QVector<QString> ConcordanceView::promptTargetTexts(Match &match, int target_count,
                                                     const QString &title, bool edit_annotation)
{
	auto &annot = *match.annotation();

	// Collect current text for each target.
	QVector<QString> current(target_count + 1); // 1-based
	for (int i = 1; i <= target_count; i++)
	{
		auto *t = match.get(i);
		if (edit_annotation)
		{
			intptr_t event_idx = annot.get_event_index(t->layer, t->start_time);
			if (event_idx > 0)
			{
				const auto &ev = annot.get_event(t->layer, event_idx);
				current[i] = QString::fromUtf8(ev.text.data(), (int) ev.text.size());
			}
		}
		else
		{
			current[i] = QString::fromUtf8(t->value.data(), (int) t->value.size());
		}
	}

	// Single target: use a plain QInputDialog.
	if (target_count == 1)
	{
		bool ok;
		auto result = QInputDialog::getText(this, title,
			tr("Text:"), QLineEdit::Normal, current[1], &ok);
		if (!ok) return {};
		return { QString(), result }; // index 0 unused, result at index 1
	}

	// Multiple targets: one QLineEdit per target in a form dialog.
	QDialog dlg(this);
	dlg.setWindowTitle(title);

	auto *form = new QFormLayout;
	QVector<QLineEdit *> edits(target_count + 1, nullptr);

	for (int i = 1; i <= target_count; i++)
	{
		auto *t = match.get(i);
		auto layer_lbl = annot.get_layer_label(t->layer);
		auto label = QString("Target %1 (%2)").arg(i)
		             .arg(QString::fromUtf8(layer_lbl.data(), (int) layer_lbl.size()));

		auto *edit = new QLineEdit(current[i], &dlg);
		edits[i] = edit;
		form->addRow(label, edit);
	}

	auto *buttons = new QDialogButtonBox(
		QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dlg);
	connect(buttons, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
	connect(buttons, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);

	auto *layout = new QVBoxLayout(&dlg);
	layout->addLayout(form);
	layout->addWidget(buttons);

	if (dlg.exec() != QDialog::Accepted)
		return {};

	QVector<QString> result(target_count + 1);
	for (int i = 1; i <= target_count; i++)
		result[i] = edits[i]->text();
	return result;
}

void ConcordanceView::onEditEvent()
{
	auto rows = selectedRows();
	if (rows.size() != 1)
	{
		QMessageBox::information(this, tr("Edit annotation event"),
			tr("Select a single match to edit."));
		return;
	}

	int proxy_row = rows.first();
	int src = mapToSourceRow(proxy_row);
	auto &match = m_conc->get_match(src + 1);
	int target_count = m_conc->target_count();

	auto new_texts = promptTargetTexts(match, target_count,
	                                   tr("Edit annotation event"), true);
	if (new_texts.isEmpty())
		return;

	auto &annot = *match.annotation();
	bool any_changed = false;

	for (int i = 1; i <= target_count; i++)
	{
		auto *t = match.get(i);
		intptr_t event_idx = annot.get_event_index(t->layer, t->start_time);
		if (event_idx == 0) continue;

		const auto &event = annot.get_event(t->layer, event_idx);
		auto old_qs = QString::fromUtf8(event.text.data(), (int) event.text.size());
		if (new_texts[i] == old_qs) continue;

		annot.set_event_text(t->layer, event_idx,
		                     String(new_texts[i].toUtf8().constData()));
		any_changed = true;
	}

	if (!any_changed)
		return;

	annot.set_graph_modified(true);

	// Re-sync all targets from the updated annotation.
	for (int i = 1; i <= target_count; i++)
	{
		bool modified;
		match.update(i, modified);
	}

	// match.update wrote new values into the in-memory concordance, so its
	// state has diverged from the .phon-conc on disk — mark it modified too.
	// Document::file_modified is then the single broadcast that refreshes
	// both the annotation tab and the concordance tab in one go.
	m_conc->modify();
	Document::file_modified();
	emit titleChanged(label());
	m_model->refreshRow(src);
	Project::get()->modify();
}

void ConcordanceView::onEditMatchText()
{
	auto rows = selectedRows();
	if (rows.size() != 1)
	{
		QMessageBox::information(this, tr("Edit match text"),
			tr("Select a single match to edit."));
		return;
	}

	int proxy_row = rows.first();
	int src = mapToSourceRow(proxy_row);
	auto &match = m_conc->get_match(src + 1);
	int target_count = m_conc->target_count();

	auto new_texts = promptTargetTexts(match, target_count,
	                                   tr("Edit match text"), false);
	if (new_texts.isEmpty())
		return;

	bool any_changed = false;

	for (int i = 1; i <= target_count; i++)
	{
		auto *t = match.get(i);
		auto old_qs = QString::fromUtf8(t->value.data(), (int) t->value.size());
		if (new_texts[i] == old_qs) continue;

		t->value = String(new_texts[i].toUtf8().constData());
		any_changed = true;
	}

	if (!any_changed)
		return;

	m_conc->modify();
	// Broadcast the modification so MainWindow refreshes the file manager
	// and every open tab's title (the modified-state asterisk in particular).
	// Also emit the per-view signal as a direct fast path.
	Document::file_modified();
	emit titleChanged(label());
	m_model->refreshRow(src);
	Project::get()->modify();
}



void ConcordanceView::onExportCsv()
{
	auto path = getSaveFileName(this, tr("Export to CSV..."),
		tr("CSV files (*.csv *.txt)"),
		defaultSaveName(m_conc->label(), QStringLiteral(".csv")));
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
		emit concordanceCreated(result);
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
		emit concordanceCreated(result);
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
		emit concordanceCreated(result);
	}
	catch (std::exception &e)
	{
		QMessageBox::critical(this, tr("Error"), QString::fromUtf8(e.what()));
	}
}

Handle<DataTable> ConcordanceView::pickDataTable(const QString &title)
{
	auto concordances = Project::get()->get_concordances();
	auto datasets = Project::get()->get_datasets();
	QStringList names;

	for (auto &c : concordances)
	{
		if (c.get() != m_conc.get())
			names << QString::fromUtf8(c->label().data(), (int) c->label().size());
	}
	for (auto &d : datasets)
	{
		names << QString::fromUtf8(d->label().data(), (int) d->label().size());
	}

	if (names.isEmpty())
	{
		QMessageBox::information(this, title, tr("No other data tables available."));
		return {};
	}

	bool ok;
	auto choice = QInputDialog::getItem(this, title, tr("Select data table:"), names, 0, false, &ok);
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
	for (auto &d : datasets)
	{
		auto lbl = QString::fromUtf8(d->label().data(), (int) d->label().size());
		if (lbl == choice)
		{
			d->open();
			return d;
		}
	}

	return {};
}

void ConcordanceView::onMerge()
{
	auto other = pickDataTable(tr("Merge tables"));
	if (!other) return;

	// Check row count compatibility.
	if (m_conc->row_count() != other->row_count())
	{
		QMessageBox::critical(this, tr("Error"),
			tr("Cannot merge: tables have different numbers of rows (%1 vs %2).")
				.arg((int) m_conc->row_count()).arg((int) other->row_count()));
		return;
	}

	// If the other table is a concordance, check match equality.
	auto *other_conc = dynamic_cast<Concordance *>(other.get());
	if (other_conc && !m_conc->matches_equal(*other_conc))
	{
		QMessageBox::critical(this, tr("Error"),
			tr("Cannot merge concordances: they have different matches."));
		return;
	}

	// Classify columns: shared, unique to B, or conflicting.
	auto a_cols = m_conc->column_count();
	auto b_cols = other->column_count();
	auto rows = m_conc->row_count();

	std::map<String, intptr_t> a_headers;
	for (intptr_t j = 1; j <= a_cols; j++) {
		a_headers[m_conc->get_header(j)] = j;
	}

	Array<std::pair<String, intptr_t>> columns_to_add;

	for (intptr_t j = 1; j <= b_cols; j++)
	{
		// Skip derived measurement columns (ERB, Bark, semitones) in source concordance.
		// These will be recomputed from the raw stored values via the aux toggle mechanism.
		if (other_conc && other_conc->is_measurement_column(j) && !other_conc->is_stored_measurement(j))
			continue;

		auto header = other->get_header(j);
		auto it = a_headers.find(header);

		if (it == a_headers.end())
		{
			// B-only column: add it.
			columns_to_add.append(std::make_pair(header, j));
		}
		else
		{
			// Same name: check if values differ.
			bool same = true;
			for (intptr_t i = 1; i <= rows; i++)
			{
				if (m_conc->get_cell(i, it->second) != other->get_cell(i, j)) {
					same = false;
					break;
				}
			}

			if (!same)
			{
				auto qheader = QString::fromUtf8(header.data(), (int) header.size());
				auto answer = QMessageBox::question(this, tr("Column conflict"),
					tr("Column \"%1\" has different values.\nAdd the other table's values as \"%1 (B)\"?")
						.arg(qheader),
					QMessageBox::Yes | QMessageBox::No, QMessageBox::No);

				if (answer == QMessageBox::Yes)
				{
					auto suffixed = header;
					suffixed.append(" (B)");
					columns_to_add.append(std::make_pair(suffixed, j));
				}
			}
			// If same values: skip (already present through A).
		}
	}

	if (columns_to_add.empty())
	{
		QMessageBox::information(this, tr("Merge"),
			tr("No new columns to add. The other table has no unique or conflicting columns."));
		return;
	}

	bool ok;
	auto name = QInputDialog::getText(this, tr("Merge"), tr("Name for the result:"),
		QLineEdit::Normal, tr("Merged"), &ok);
	if (!ok || name.isEmpty()) return;

	try
	{
		auto result = m_conc->merge(*other, String(name.toUtf8().constData()), columns_to_add);
		Project::updated();
		emit concordanceCreated(result);
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
	// Wide↔Long changes how display rows map to matches (1:1 vs many:1), which
	// invalidates any EditCellCommand / DeleteMatchesCommand that stored source
	// row indices under the previous layout. Dropping the stacks is the safe call.
	m_commands.clear();
	emit undoRedoChanged(false, false);

	m_conc->set_layout(long_format ? Concordance::Layout::Long : Concordance::Layout::Wide);
	// set_layout is an inline header setter that does NOT touch m_content_modified;
	// the layout is persisted to XML on save, so the GUI must drive the flag here
	// (and broadcast so the tab title's modified-asterisk shows up).
	m_conc->modify();
	Document::file_modified();
	emit titleChanged(label());
	m_model->refreshAll();
	updateCountLabel();
	updateColumnVisibility();
	m_table->resizeColumnsToContents();
}


void ConcordanceView::onToggleMeasurementTime(bool checked)
{
	m_conc->set_has_time(checked);
	m_model->refreshAll();
	updateColumnVisibility();
	m_table->resizeColumnsToContents();
	emit titleChanged(label());
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

// ── Merged aux scales ──────────────────────────────────────

void ConcordanceView::onToggleAuxPitchSemitones(bool checked)
{
	m_conc->set_aux_pitch_semitones(checked);
	m_model->refreshAll();
	updateColumnVisibility();
	m_table->resizeColumnsToContents();
	emit titleChanged(label());
}

void ConcordanceView::onToggleAuxPitchErb(bool checked)
{
	m_conc->set_aux_pitch_erb(checked);
	m_model->refreshAll();
	updateColumnVisibility();
	m_table->resizeColumnsToContents();
	emit titleChanged(label());
}

void ConcordanceView::onToggleAuxFormantErb(bool checked)
{
	m_conc->set_aux_formant_erb(checked);
	m_model->refreshAll();
	updateColumnVisibility();
	m_table->resizeColumnsToContents();
	emit titleChanged(label());
}

void ConcordanceView::onToggleAuxFormantBark(bool checked)
{
	m_conc->set_aux_formant_bark(checked);
	m_model->refreshAll();
	updateColumnVisibility();
	m_table->resizeColumnsToContents();
	emit titleChanged(label());
}

// ── Column renaming ─────────────────────────────────────

// ── Column recode / transform ──────────────────────────

void ConcordanceView::onRecodeColumn(int section)
{
	intptr_t col = section + 1; // 1-based
	intptr_t nrows = m_conc->row_count();
	auto col_hdr = m_conc->get_header(col);
	auto col_name = QString::fromUtf8(col_hdr.data(), (int) col_hdr.size());

	// Collect unique levels.
	QSet<QString> seen;
	for (intptr_t i = 1; i <= nrows; i++) {
		auto cell = m_conc->get_cell(i, col);
		seen.insert(QString::fromUtf8(cell.data(), (int) cell.size()));
	}
	QStringList levels(seen.begin(), seen.end());
	levels.sort(Qt::CaseInsensitive);

	RecodeDialog dlg(col_name, levels, this);
	if (dlg.exec() != QDialog::Accepted) return;

	auto new_col_name = dlg.newColumnName();
	if (new_col_name.isEmpty()) return;

	auto mapping = dlg.mapping();

	try
	{
		std::vector<String> new_values(nrows);
		for (intptr_t i = 1; i <= nrows; i++)
		{
			auto cell = m_conc->get_cell(i, col);
			auto original = QString::fromUtf8(cell.data(), (int) cell.size());
			auto it = mapping.find(original);
			if (it != mapping.end()) {
				new_values[i - 1] = String(it.value().toUtf8().constData());
			}
			else {
				new_values[i - 1] = cell;
			}
		}

		m_conc->add_text_column(String(new_col_name.toUtf8().constData()), new_values);
		m_model->refreshAll();
		m_table->resizeColumnsToContents();
		updateColumnVisibility();
		setupFilterBar();
		updateCountLabel();
		emit titleChanged(label());

		record(std::make_unique<AddConcAuxColumnCommand>(this));
	}
	catch (std::exception &e)
	{
		QMessageBox::critical(this, tr("Error"), QString::fromUtf8(e.what()));
	}
}

void ConcordanceView::onTransformColumn(int section)
{
	intptr_t col = section + 1; // 1-based
	intptr_t nrows = m_conc->row_count();
	auto col_hdr = m_conc->get_header(col);
	auto col_name = QString::fromUtf8(col_hdr.data(), (int) col_hdr.size());

	// Collect sample values for the preview (first 8 rows).
	QVector<double> samples;
	intptr_t sample_count = std::min(nrows, (intptr_t)8);
	for (intptr_t i = 1; i <= sample_count; i++)
	{
		auto cell = m_conc->get_cell(i, col);
		if (cell.empty() || cell == "nan") {
			samples.append(std::numeric_limits<double>::quiet_NaN());
		}
		else {
			bool ok;
			double v = cell.to_float(&ok);
			samples.append(ok ? v : std::numeric_limits<double>::quiet_NaN());
		}
	}

	TransformDialog dlg(col_name, samples, this);
	if (dlg.exec() != QDialog::Accepted) return;

	auto new_col_name = dlg.newColumnName();
	if (new_col_name.isEmpty()) return;

	try
	{
		FormulaEngine engine;
		engine.parse(dlg.formula().toStdString());

		std::vector<double> result(nrows);
		int nan_count = 0;

		for (intptr_t i = 1; i <= nrows; i++)
		{
			auto cell = m_conc->get_cell(i, col);
			double val;

			if (cell.empty() || cell == "nan") {
				val = std::numeric_limits<double>::quiet_NaN();
			}
			else {
				bool ok;
				val = cell.to_float(&ok);
				if (!ok) val = std::numeric_limits<double>::quiet_NaN();
			}

			result[i - 1] = engine.evaluate(val);
			if (std::isnan(result[i - 1]) && !std::isnan(val))
				nan_count++;
		}

		m_conc->add_numeric_column(String(new_col_name.toUtf8().constData()), result);
		m_model->refreshAll();
		m_table->resizeColumnsToContents();
		updateColumnVisibility();
		setupFilterBar();
		updateCountLabel();
		emit titleChanged(label());

		record(std::make_unique<AddConcAuxColumnCommand>(this));

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

void ConcordanceView::onConvertToText(int section)
{
	intptr_t col = section + 1; // 1-based
	intptr_t nrows = m_conc->row_count();
	auto col_hdr = m_conc->get_header(col);
	auto col_name = QString::fromUtf8(col_hdr.data(), (int) col_hdr.size());

	// Collect sample values for the preview (first 8 rows, as strings).
	QStringList samples;
	intptr_t sample_count = std::min(nrows, (intptr_t)8);
	for (intptr_t i = 1; i <= sample_count; i++)
	{
		auto cell = m_conc->get_cell(i, col);
		samples.append(QString::fromUtf8(cell.data(), (int) cell.size()));
	}

	ConvertToTextDialog dlg(col_name, samples, this);
	if (dlg.exec() != QDialog::Accepted) return;

	auto new_col_name = dlg.newColumnName();
	if (new_col_name.isEmpty()) return;

	auto tmpl = dlg.templateString();

	try
	{
		std::vector<String> result(nrows);

		for (intptr_t i = 1; i <= nrows; i++)
		{
			auto cell = m_conc->get_cell(i, col);
			auto cell_q = QString::fromUtf8(cell.data(), (int) cell.size());
			auto text = ConvertToTextDialog::applyTemplate(tmpl, cell_q);
			result[i - 1] = String(text.toUtf8().constData());
		}

		m_conc->add_text_column(String(new_col_name.toUtf8().constData()), result);
		m_model->refreshAll();
		m_table->resizeColumnsToContents();
		updateColumnVisibility();
		setupFilterBar();
		updateCountLabel();
		emit titleChanged(label());

		record(std::make_unique<AddConcAuxColumnCommand>(this));
	}
	catch (std::exception &e)
	{
		QMessageBox::critical(this, tr("Error"), QString::fromUtf8(e.what()));
	}
}

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

	auto cmd = std::make_unique<RenameConcColumnCommand>(this, section,
		current_display, new_name);
	submit(std::move(cmd));
}

void ConcordanceView::updateColumnVisibility()
{
	int ncol = m_model->columnCount();

	// Check if all annotations have empty descriptions (computed once).
	bool hide_description = false;
	if (m_show_metadata)
	{
		hide_description = true;
		intptr_t nrows = m_conc->row_count();
		for (intptr_t r = 1; r <= nrows; r++)
		{
			if (!m_conc->get_match(r).annotation()->description().empty()) {
				hide_description = false;
				break;
			}
		}
	}

	intptr_t desc_col = m_conc->column_count(); // 1-based index of description column

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
			bool hidden = !m_show_metadata;
			// Hide description column if all descriptions are empty.
			if (!hidden && hide_description && col == desc_col) {
				hidden = true;
			}
			m_table->setColumnHidden(j, hidden);
		}
		// Target and measurement columns are always visible.
	}
}

// ── Mouse events ────────────────────────────────────────

void ConcordanceView::onDoubleClick(const QModelIndex &index)
{
	if (!index.isValid()) return;

	// Map the proxy column to source for the editable check.
	auto source_idx = m_proxy->mapToSource(index);
	intptr_t col = source_idx.column() + 1;

	// If the cell is editable (measurement or aux base column), let the default
	// editor handle it. The model's flags() returns Qt::ItemIsEditable for these.
	if (m_conc->is_editable_cell(col))
		return;

	// Otherwise, navigate to the match in the annotation.
	viewMatch(source_idx.row());
}

void ConcordanceView::onContextMenu(const QPoint &pos)
{
	auto index = m_table->indexAt(pos);
	if (!index.isValid()) return;

	m_table->selectRow(index.row());
	int src = mapToSourceRow(index.row());
	auto &match = m_conc->get_match(src + 1);

	QMenu menu(this);

	// Copy the cell's display value to the clipboard. Uses the display role
	// so what's copied matches what the user sees (e.g. native-scale F1/F2
	// rather than the stored Hz when ERB/Bark is toggled on).
	menu.addAction(QIcon(":/icons/clipboard-copy.svg"), tr("Copy value"), this, [this, index]() {
		auto text = index.data(Qt::DisplayRole).toString();
		QGuiApplication::clipboard()->setText(text);
	});
	menu.addSeparator();

	menu.addAction(QIcon(":/icons/file-search-corner.svg"), tr("View in annotation"), this, &ConcordanceView::onViewMatch);

	if (match.annotation()->has_sound())
		menu.addAction(QIcon(":/icons/play.svg"), tr("Play match"), this, &ConcordanceView::onPlay);

	if (praat::available() && match.annotation()->is_textgrid())
	{
		int target = m_target_spin->value();
		auto layer = match.get_layer(target);
		auto start = match.get_start_time(target);
		auto end = match.get_end_time(target);
		auto annot = match.annotation();

		menu.addAction(tr("Open in Praat"), this, [annot, layer, start, end]() {
			try {
				String snd_path;
				if (annot->has_sound())
					snd_path = annot->sound()->path();
				praat::open_at_time(layer, start, end, annot->path(), snd_path);
			}
			catch (...) {}
		});
	}

	menu.addSeparator();

	// Edit cell — any editable cell (measurement or aux base column)
	auto source_idx = m_proxy->mapToSource(index);
	intptr_t col = source_idx.column() + 1;
	if (m_conc->is_editable_cell(col))
	{
		menu.addAction(QIcon(":/icons/pencil-line.svg"), tr("Edit cell"), this, [this, index]() {
			m_table->edit(index);
		});
	}

	menu.addAction(QIcon(":/icons/pencil-line.svg"), tr("Edit match text"), this, &ConcordanceView::onEditMatchText);
	menu.addAction(QIcon(":/icons/pencil-line.svg"), tr("Edit annotation event"), this, &ConcordanceView::onEditEvent);
	menu.addAction(QIcon(":/icons/grid-2x2-x.svg"), tr("Remove match"), this, &ConcordanceView::onDeleteRows);
	menu.addSeparator();
	menu.addAction(QIcon(":/icons/book-marked.svg"), tr("Bookmark match"), this, &ConcordanceView::onBookmark);

	menu.exec(m_table->viewport()->mapToGlobal(pos));
}

// ── Helpers ─────────────────────────────────────────────

void ConcordanceView::updateCountLabel()
{
	auto total = m_conc->row_count();
	auto visible = m_proxy->visibleRowCount();

	if (m_conc->layout() == Concordance::Layout::Long && m_conc->has_measurement_data())
	{
		auto npoints = (int) m_conc->measurement_points().size();
		auto nmatches = total / npoints;
		if (visible < (int) total) {
			m_count_label->setText(tr("Showing %1 of %2 row(s) (%3 match(es) \u00d7 %4 point(s))")
				.arg(visible).arg((int) total).arg((int) nmatches).arg(npoints));
		}
		else {
			m_count_label->setText(tr("%1 match(es) \u00d7 %2 point(s) = %3 row(s)")
				.arg((int) nmatches).arg(npoints).arg((int) total));
		}
	}
	else
	{
		if (visible < (int) total) {
			m_count_label->setText(tr("Showing %1 of %2 match(es)").arg(visible).arg((int) total));
		}
		else {
			m_count_label->setText(tr("%1 match(es)").arg((int) total));
		}
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

int ConcordanceView::mapToSourceRow(int proxyRow) const
{
	auto source_idx = m_proxy->mapToSource(m_proxy->index(proxyRow, 0));
	return source_idx.row();
}

// ── Filter / Subset ────────────────────────────────────────

void ConcordanceView::setupFilterBar()
{
	QStringList headers;
	for (intptr_t j = 1; j <= m_conc->column_count(); j++) {
		auto h = m_conc->get_header(j);
		headers << QString::fromUtf8(h.data(), (int) h.size());
	}

	m_filter_bar->setColumns(headers,
		// isNumeric callback: try to parse the first non-empty cell.
		[this](int col) -> bool {
			intptr_t c = col + 1;
			for (intptr_t i = 1; i <= m_conc->row_count() && i <= 20; i++) {
				auto cell = m_conc->get_cell(i, c);
				if (cell.empty()) continue;
				bool ok;
				cell.to_float(&ok);
				return ok;
			}
			return false;
		},
		// getLevels callback: collect unique values (sample first 5000 rows for speed).
		[this](int col) -> QStringList {
			QSet<QString> seen;
			intptr_t c = col + 1;
			intptr_t limit = std::min(m_conc->row_count(), (intptr_t)5000);
			for (intptr_t i = 1; i <= limit; i++) {
				auto cell = m_conc->get_cell(i, c);
				auto qs = QString::fromUtf8(cell.data(), (int) cell.size());
				seen.insert(qs);
			}
			QStringList levels(seen.begin(), seen.end());
			levels.sort(Qt::CaseInsensitive);
			return levels;
		}
	);
}

void ConcordanceView::onToggleFilter()
{
	bool show = m_filter_action->isChecked();
	m_filter_bar->setVisible(show);
	m_proxy->setFilterEnabled(show);

	// Auto-add a first rule so the user can start filtering immediately.
	if (show && m_proxy->ruleCount() == 0) {
		m_filter_bar->appendStrip();
	}
}

void ConcordanceView::onClearFilters()
{
	m_proxy->clearRules();
	m_filter_bar->rebuild();
}

void ConcordanceView::onCreateSubset()
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
		auto result = m_conc->subset(rows, String(name.toUtf8().constData()));
		Project::updated();
		emit concordanceCreated(result);
	}
	catch (std::exception &e)
	{
		QMessageBox::critical(this, tr("Error"), QString::fromUtf8(e.what()));
	}
}

void ConcordanceView::onAddMetricColumn()
{
	auto nrows = m_conc->row_count();
	auto ncols = m_conc->column_count();

	if (nrows == 0) {
		QMessageBox::information(this, tr("Information"), tr("The concordance is empty."));
		return;
	}

	// ── Detect numeric and text columns by sampling cells ──
	QStringList num_names, text_names;
	QVector<int> num_indices, text_indices;

	intptr_t sample_limit = std::min(nrows, (intptr_t)20);

	for (intptr_t j = 1; j <= ncols; j++)
	{
		auto h = m_conc->get_header(j);
		auto qh = QString::fromUtf8(h.data(), (int) h.size());

		// Sample the first non-empty cells to decide the type.
		bool all_numeric = true;
		bool found_any = false;

		for (intptr_t i = 1; i <= sample_limit; i++) {
			auto cell = m_conc->get_cell(i, j);
			if (cell.empty() || cell == "nan") continue;
			found_any = true;
			bool ok;
			cell.to_float(&ok);
			if (!ok) {
				all_numeric = false;
				break;
			}
		}

		if (found_any && all_numeric) {
			num_names << qh;
			num_indices << (int) j;
		}
		else if (found_any) {
			text_names << qh;
			text_indices << (int) j;
		}
	}

	if (num_names.isEmpty()) {
		QMessageBox::information(this, tr("Information"),
			tr("No numeric columns detected."));
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
			groups.resize(nrows);
			for (intptr_t i = 1; i <= nrows; i++) {
				std::string key;
				for (int gc : group_cols) {
					auto cell = m_conc->get_cell(i, gc);
					if (!key.empty()) key += '|';
					key.append(cell.data(), cell.size());
				}
				groups[i - 1] = std::move(key);
			}
		}

		// Helper: extract a numeric column by parsing cells.
		auto extract_column = [&](int col_1based) -> std::vector<double> {
			std::vector<double> vals(nrows);
			for (intptr_t i = 1; i <= nrows; i++) {
				auto cell = m_conc->get_cell(i, col_1based);
				if (cell.empty() || cell == "nan") {
					vals[i - 1] = std::nan("");
				}
				else {
					bool ok;
					double v = cell.to_float(&ok);
					vals[i - 1] = ok ? v : std::nan("");
				}
			}
			return vals;
		};

		std::vector<double> result;

		if (stats::is_multivariate(metric))
		{
			auto cols = dlg.selectedColumns();
			if (cols.size() < 2) {
				QMessageBox::information(this, tr("Information"),
					tr("Please select at least two columns for this metric."));
				return;
			}
			std::vector<std::vector<double>> columns;
			for (int c : cols) {
				columns.push_back(extract_column(c));
			}
			result = stats::compute_multivariate_metric(columns, groups, metric);
		}
		else
		{
			int col = dlg.selectedColumn();
			auto values = extract_column(col);
			result = stats::compute_column_metric(values, groups, metric);
		}

		// Add as a new auxiliary column.
		m_conc->add_numeric_column(String(col_name.toUtf8().constData()), result);
		m_model->refreshAll();
		m_table->resizeColumnsToContents();
		updateColumnVisibility();
		setupFilterBar();
		updateCountLabel();
		emit titleChanged(label());

		record(std::make_unique<AddConcAuxColumnCommand>(this));

		// Auto-add filter rule if requested.
		if (dlg.addFilter())
		{
			double threshold = dlg.filterThreshold();

			// Find the new column by header name (it's NOT the last column in concordances
			// because property/description columns come after aux columns).
			int new_col_idx = -1;
			for (int j = 0; j < m_model->columnCount(); j++) {
				auto h = m_model->headerData(j, Qt::Horizontal).toString();
				if (h == col_name) {
					new_col_idx = j;
					break;
				}
			}
			if (new_col_idx < 0) return; // shouldn't happen

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

			m_filter_action->setChecked(true);
			m_filter_bar->rebuild();
		}
	}
	catch (std::exception &e)
	{
		QMessageBox::critical(this, tr("Error"), QString::fromUtf8(e.what()));
	}
}


void ConcordanceView::onAddBooleanColumn()
{
	auto nrows = m_conc->row_count();
	if (nrows == 0) {
		QMessageBox::information(this, tr("Information"), tr("The concordance is empty."));
		return;
	}

	if (m_proxy->ruleCount() == 0) {
		QMessageBox::information(this, tr("Information"),
			tr("There are no filter rules. Add at least one rule before creating a boolean column."));
		return;
	}

	// Prompt for the column name. Default is "flag"; ensure uniqueness against
	// existing headers (case-sensitive, matching find_column).
	QString default_name = tr("flag");
	bool ok = false;
	QString col_name;
	while (true)
	{
		col_name = QInputDialog::getText(this,
			tr("Add boolean column"),
			tr("Name for the new column:"),
			QLineEdit::Normal, default_name, &ok);

		if (!ok) return;  // user cancelled

		col_name = col_name.trimmed();
		if (col_name.isEmpty()) {
			QMessageBox::warning(this, tr("Invalid name"),
				tr("The column name cannot be empty."));
			continue;
		}
		if (m_conc->find_column(String(col_name.toUtf8().constData())) != 0) {
			QMessageBox::warning(this, tr("Duplicate name"),
				tr("A column named \"%1\" already exists. Please choose another name.").arg(col_name));
			default_name = col_name;
			continue;
		}
		break;
	}

	// Evaluate rules directly against every source row. We intentionally use
	// evaluateRow() (which ignores the enabled flag) so that the flag column
	// is a faithful snapshot of the rule set: toggling the filter bar off
	// afterwards doesn't silently change the stored values.
	std::vector<double> flags;
	flags.reserve(static_cast<size_t>(nrows));
	for (intptr_t i = 0; i < nrows; i++) {
		flags.push_back(m_proxy->evaluateRow(static_cast<int>(i)) ? 1.0 : 0.0);
	}

	try
	{
		m_conc->add_numeric_column(String(col_name.toUtf8().constData()), flags);
		m_model->refreshAll();
		m_table->resizeColumnsToContents();
		updateColumnVisibility();
		setupFilterBar();
		updateCountLabel();
		emit titleChanged(label());

		record(std::make_unique<AddConcAuxColumnCommand>(this));
	}
	catch (std::exception &e)
	{
		QMessageBox::critical(this, tr("Error"), QString::fromUtf8(e.what()));
	}
}


void ConcordanceView::onNormalizeVowels()
{
	auto nrows = m_conc->row_count();
	auto ncols = m_conc->column_count();

	if (nrows == 0) {
		QMessageBox::information(this, tr("Information"), tr("The concordance is empty."));
		return;
	}

	// ── Detect numeric and text columns by sampling cells ──
	QStringList num_names, text_names;
	QVector<int> num_indices, text_indices;
	QVector<QStringList> vowel_levels;

	intptr_t sample_limit = std::min(nrows, (intptr_t)20);

	for (intptr_t j = 1; j <= ncols; j++)
	{
		auto h = m_conc->get_header(j);
		auto qh = QString::fromUtf8(h.data(), (int) h.size());

		bool all_numeric = true;
		bool found_any = false;

		for (intptr_t i = 1; i <= sample_limit; i++) {
			auto cell = m_conc->get_cell(i, j);
			if (cell.empty() || cell == "nan") continue;
			found_any = true;
			bool ok;
			cell.to_float(&ok);
			if (!ok) {
				all_numeric = false;
				break;
			}
		}

		if (found_any && all_numeric) {
			num_names << qh;
			num_indices << (int) j;
		}
		else if (found_any) {
			text_names << qh;
			text_indices << (int) j;
			// Collect unique levels for this text column.
			QSet<QString> seen;
			QStringList levels;
			for (intptr_t i = 1; i <= nrows; i++) {
				auto cell = m_conc->get_cell(i, j);
				auto qs = QString::fromUtf8(cell.data(), (int) cell.size());
				if (!qs.isEmpty() && !seen.contains(qs)) {
					seen.insert(qs);
					levels << qs;
				}
			}
			levels.sort();
			vowel_levels << levels;
		}
	}

	if (num_names.isEmpty()) {
		QMessageBox::information(this, tr("Information"),
			tr("No numeric columns detected."));
		return;
	}
	if (text_names.isEmpty()) {
		QMessageBox::information(this, tr("Information"),
			tr("No text columns available (need at least a speaker column)."));
		return;
	}

	VowelNormalizationDialog dlg(num_names, num_indices, text_names, text_indices, vowel_levels, this);
	if (dlg.exec() != QDialog::Accepted) return;

	try
	{
		auto method = dlg.selectedMethod();
		auto formant_cols = dlg.selectedFormantColumns();
		int spk_col = dlg.speakerColumn();

		// Helper: extract a numeric column by parsing cells (1-based column index).
		auto extract_numeric = [&](int col_1based) -> std::vector<double> {
			std::vector<double> vals(nrows);
			for (intptr_t i = 1; i <= nrows; i++) {
				auto cell = m_conc->get_cell(i, col_1based);
				if (cell.empty() || cell == "nan") {
					vals[i - 1] = std::nan("");
				}
				else {
					bool ok;
					double v = cell.to_float(&ok);
					vals[i - 1] = ok ? v : std::nan("");
				}
			}
			return vals;
		};

		// Helper: extract a text column (1-based column index).
		auto extract_text = [&](int col_1based) -> std::vector<std::string> {
			std::vector<std::string> vals(nrows);
			for (intptr_t i = 1; i <= nrows; i++) {
				auto cell = m_conc->get_cell(i, col_1based);
				vals[i - 1].assign(cell.data(), cell.size());
			}
			return vals;
		};

		// Build formant data and spans.
		std::vector<std::vector<double>> formant_data;
		for (int c : formant_cols) {
			formant_data.push_back(extract_numeric(c));
		}
		std::vector<std::span<const double>> formant_spans;
		for (auto &fd : formant_data) {
			formant_spans.emplace_back(fd);
		}

		auto speakers = extract_text(spk_col);

		std::vector<std::vector<double>> result;

		switch (method) {
			case VowelNormMethod::Lobanov:
				result = VowelNormalizer::lobanov(formant_spans, speakers);
				break;
			case VowelNormMethod::Nearey1:
				result = VowelNormalizer::nearey1(formant_spans, speakers);
				break;
			case VowelNormMethod::Nearey2:
				result = VowelNormalizer::nearey2(formant_spans, speakers);
				break;
			case VowelNormMethod::WattFabricius: {
				int vow_col = dlg.vowelColumn();
				auto vowels = extract_text(vow_col);
				auto li = dlg.labelI().toStdString();
				auto la = dlg.labelA().toStdString();
				auto lu = dlg.labelU().toStdString();
				result = VowelNormalizer::watt_fabricius(formant_spans, speakers, vowels, li, la, lu);
				break;
			}
		}

		// Append result columns.
		auto names = dlg.outputColumnNames();
		for (int f = 0; f < (int) result.size(); f++) {
			m_conc->add_numeric_column(String(names[f].toUtf8().constData()), result[f]);
			record(std::make_unique<AddConcAuxColumnCommand>(this));
		}

		m_model->refreshAll();
		m_table->resizeColumnsToContents();
		updateColumnVisibility();
		setupFilterBar();
		updateCountLabel();
		emit titleChanged(label());
	}
	catch (std::exception &e)
	{
		QMessageBox::critical(this, tr("Error"), QString::fromUtf8(e.what()));
	}
}


// ─────────────────────────────────────────────────
//  Undo/redo helpers
// ─────────────────────────────────────────────────

void ConcordanceView::refreshAfterRowChange()
{
	m_conc->modify();
	Document::file_modified();
	updateCountLabel();
	emit titleChanged(label());
}

void ConcordanceView::refreshAfterStructuralChange()
{
	m_model->refreshAll();
	m_table->resizeColumnsToContents();
	updateColumnVisibility();
	setupFilterBar();
	updateCountLabel();
	emit titleChanged(label());
}

void ConcordanceView::adjustFiltersAfterColumnRemove(int col)
{
	m_proxy->adjustAfterColumnRemove(col);
	m_filter_bar->rebuild();
}

void ConcordanceView::adjustFiltersAfterColumnInsert(int col)
{
	m_proxy->adjustAfterColumnInsert(col);
	m_filter_bar->rebuild();
}

void ConcordanceView::selectWholeColumn(int section)
{
	if (section < 0) return;
	auto *sm = m_table->selectionModel();
	if (!sm) return;

	// Drive the header highlight directly. currentChanged() should also fire
	// from the setCurrentIndex() call below, but the NoUpdate flag can suppress
	// it on some Qt versions — calling the setter explicitly removes that
	// dependency. The static_cast is safe because setupUi() always installs a
	// ConcHeaderView (defined in the anonymous namespace at the top of this file).
	static_cast<ConcHeaderView *>(m_table->horizontalHeader())->setCurrentColumn(section);

	int nrows = m_proxy->rowCount();
	if (nrows <= 0) {
		sm->clearSelection();
		return;
	}

	// Build a column-spanning selection. The Columns flag tells the selection
	// model to treat the range as a full column regardless of the view's
	// SelectRows behavior — cell clicks continue to produce row selections.
	QItemSelection sel(m_proxy->index(0, section),
	                   m_proxy->index(nrows - 1, section));
	sm->select(sel, QItemSelectionModel::ClearAndSelect | QItemSelectionModel::Columns);

	// Anchor the current index so keyboard navigation starts from the top of the column.
	sm->setCurrentIndex(m_proxy->index(0, section), QItemSelectionModel::NoUpdate);
}

void ConcordanceView::showEvent(QShowEvent *event)
{
	View::showEvent(event);
	// Grab keyboard focus for the match table so Down, Space, etc. reach it
	// immediately — Qt's tab-activation machinery focuses the ViewPanel wrapper
	// above us, which on its own never propagates down to a useful widget.
	m_table->setFocus(Qt::OtherFocusReason);
}

bool ConcordanceView::eventFilter(QObject *watched, QEvent *event)
{
	// Down-arrow on the table with no row selected lands on the first row —
	// the usual "empty-state" navigation affordance. Must run as a filter
	// (not in the view's keyPressEvent) because the table consumes arrow
	// keys before they can bubble up to parent widgets.
	//
	// The trigger condition is "no full row is selected". A column selection
	// (made by clicking a column header) does NOT count — Qt's default Down
	// behavior would move relative to the column's anchor and land on row 1
	// instead of row 0, which is never what the user wants.
	if (watched == m_table && event->type() == QEvent::KeyPress)
	{
		auto *ke = static_cast<QKeyEvent *>(event);
		if (ke->key() == Qt::Key_Down
			&& ke->modifiers() == Qt::NoModifier
			&& m_proxy->rowCount() > 0
			&& m_table->selectionModel()->selectedRows().isEmpty())
		{
			auto idx = m_proxy->index(0, 0);
			m_table->setCurrentIndex(idx);
			m_table->selectRow(0);
			return true;
		}
	}

	// IBeam cursor over editable cells, arrow elsewhere. The viewport is what
	// receives mouse events for the table body; the header has its own widget
	// (not filtered here) so the cursor stays arrow over column headers.
	if (watched == m_table->viewport())
	{
		auto type = event->type();
		if (type == QEvent::MouseMove)
		{
			auto *me = static_cast<QMouseEvent *>(event);
			auto proxy_idx = m_table->indexAt(me->pos());
			bool editable = false;
			if (proxy_idx.isValid()) {
				auto src_idx = m_proxy->mapToSource(proxy_idx);
				intptr_t col = src_idx.column() + 1;
				editable = m_conc->is_editable_cell(col);
			}

			auto *vp = m_table->viewport();
			if (editable)
				vp->setCursor(Qt::IBeamCursor);
			else
				vp->unsetCursor();
		}
		else if (type == QEvent::Leave)
		{
			// Restore default when the mouse leaves the viewport entirely
			// (hovering over headers, scrollbars, or outside the table).
			m_table->viewport()->unsetCursor();
		}
	}
	return View::eventFilter(watched, event);
}

void ConcordanceView::onCellEdited(int row, int col, const QString &old_text, const QString &new_text)
{
	// The edit has already been committed by ConcordanceModel::setData() when this
	// fires; we register it on the undo stack via record() (not submit()), so
	// execute() is only invoked on redo. record() itself emits undoRedoChanged.
	record(std::make_unique<EditCellCommand>(this, row, col, old_text, new_text));
}

} // namespace phonometrica
