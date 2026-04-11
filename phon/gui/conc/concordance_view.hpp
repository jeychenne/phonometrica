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
 * Purpose: View for displaying and interacting with query results (concordances). Provides a toolbar with play,       *
 *          stop, view-in-annotation, bookmark, edit, delete, set operations, CSV export, rename, and scale toggle.   *
 *          Results are displayed in a QTableView backed by ConcordanceModel.                                          *
 *                                                                                                                     *
 ***********************************************************************************************************************/

#ifndef PHONOMETRICA_CONCORDANCE_VIEW_HPP
#define PHONOMETRICA_CONCORDANCE_VIEW_HPP

#include <QTableView>
#include <QToolBar>
#include <QLabel>
#include <QSpinBox>
#include <phon/gui/view.hpp>
#include <phon/gui/conc/concordance_model.hpp>
#include <phon/gui/data_filter.hpp>
#include <phon/gui/filter_bar.hpp>
#include <phon/gui/outlier_dialog.hpp>
#include <phon/application/audio_player.hpp>

namespace phonometrica {

class ConcordanceView final : public View
{
	Q_OBJECT

public:

	explicit ConcordanceView(Handle<Concordance> conc, QWidget *parent = nullptr);

	~ConcordanceView() override;

	QString label() const override;
	String path() const override;
	Document* document() const override { return m_conc.get(); }
	bool isModified() const override;
	bool save() override;
	void discardChanges() override;
	QString helpAnchor() const override { return QStringLiteral("concordance"); }

	// Public helpers for undo/redo commands.
	ConcordanceModel *concModel() const { return m_model; }
	void refreshAfterRowChange();
	void refreshAfterStructuralChange();

	// Filter adjustment after column structural changes (0-based column indices).
	void adjustFiltersAfterColumnRemove(int col);
	void adjustFiltersAfterColumnInsert(int col);

signals:

	void openAnnotation(const Handle<Annotation> &annot, intptr_t layer, double start, double end, bool split);
	void requestAnalysis(Handle<DataTable> source);
	void concordanceCreated(Handle<Concordance> conc);

private slots:

	void onPlay();
	void onStop();
	void onViewMatch();
	void onBookmark();
	void onDeleteRows();
	void onEditEvent();
	void onEditMatchText();

private:

	void onExportCsv();
	void onRename();

	void onUnion();
	void onIntersection();
	void onComplement();
	void onMerge();

	void onToggleFilter();
	void onClearFilters();
	void onCreateSubset();
	void onAddMetricColumn();

	void onDoubleClick(const QModelIndex &index);
	void onContextMenu(const QPoint &pos);
	void onRecodeColumn(int section);
	void onTransformColumn(int section);
	void onConvertToText(int section);

	void onToggleMatchInfo(bool visible);
	void onToggleContext(bool visible);
	void onToggleMetadata(bool visible);
	void onToggleLayout(bool long_format);

	void onToggleErb(bool checked);
	void onToggleBark(bool checked);

	void onTogglePitchSemitones(bool checked);
	void onTogglePitchErb(bool checked);

	void onToggleAuxPitchSemitones(bool checked);
	void onToggleAuxPitchErb(bool checked);
	void onToggleAuxFormantErb(bool checked);
	void onToggleAuxFormantBark(bool checked);

	void onHeaderDoubleClick(int section);

private:

	void setupUi();
	// Shared dialog for single- and multi-target editing.
	// Returns one QString per target (1-based); empty list means cancelled.
	QVector<QString> promptTargetTexts(Match &match, int target_count,
	                                   const QString &title, bool edit_annotation);
	void updateCountLabel();
	void updateColumnVisibility();
	void setupFilterBar();
	void stopPlayer();
	void playMatch(int row);
	void viewMatch(int row);
	int selectedRow() const;
	QList<int> selectedRows() const;
	int mapToSourceRow(int proxyRow) const;
	Handle<Concordance> pickConcordance(const QString &title);
	Handle<DataTable> pickDataTable(const QString &title);

	ConcordanceModel *m_model = nullptr;
	DataFilterProxyModel *m_proxy = nullptr;
	FilterBar *m_filter_bar = nullptr;
	QTableView *m_table = nullptr;
	QToolBar *m_toolbar = nullptr;
	QLabel *m_count_label = nullptr;
	QSpinBox *m_target_spin = nullptr;
	QAction *m_filter_action = nullptr;
	QAction *m_clear_filter_action = nullptr;
	QAction *m_subset_action = nullptr;

	// Native scales menu actions
	QAction *m_erb_action = nullptr;
	QAction *m_bark_action = nullptr;
	QAction *m_pitch_st_action = nullptr;
	QAction *m_pitch_erb_action = nullptr;

	// Merged aux scales menu actions
	QAction *m_aux_pitch_st_action = nullptr;
	QAction *m_aux_pitch_erb_action = nullptr;
	QAction *m_aux_formant_erb_action = nullptr;
	QAction *m_aux_formant_bark_action = nullptr;

	std::unique_ptr<AudioPlayer> m_player;
	Handle<Concordance> m_conc;

	bool m_show_match_info = true;
	bool m_show_context = true;
	bool m_show_metadata = true;
	bool m_open_in_split = true;
};

} // namespace phonometrica

#endif // PHONOMETRICA_CONCORDANCE_VIEW_HPP
