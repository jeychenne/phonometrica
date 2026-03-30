/***********************************************************************************************************************
 *                                                                                                                     *
 * Copyright (C) 2019-2026 Julien Eychenne                                                                             *
 *                                                                                                                     *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0. If a copy of the MPL was not   *
 * distributed with this file, You can obtain one at https://mozilla.org/MPL/2.0/.                                     *
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
	bool isModified() const override;
	bool save() override;
	void discardChanges() override;
	QString helpAnchor() const override { return QStringLiteral("concordance"); }

signals:

	void openAnnotation(const Handle<Annotation> &annot, intptr_t layer, double start, double end, bool split);
	void requestAnalysis(Handle<DataTable> source);

private slots:

	void onPlay();
	void onStop();
	void onViewMatch();
	void onBookmark();
	void onDeleteRows();
	void onEditEvent();
	void onExportCsv();
	void onRename();

	void onUnion();
	void onIntersection();
	void onComplement();

	void onDoubleClick(const QModelIndex &index);
	void onContextMenu(const QPoint &pos);

	void onToggleMatchInfo(bool visible);
	void onToggleContext(bool visible);
	void onToggleMetadata(bool visible);
	void onToggleLayout(bool long_format);

	void onToggleErb(bool checked);
	void onToggleBark(bool checked);

	void onTogglePitchSemitones(bool checked);
	void onTogglePitchErb(bool checked);

	void onHeaderDoubleClick(int section);

private:

	void setupUi();
	void updateCountLabel();
	void updateColumnVisibility();
	void stopPlayer();
	void playMatch(int row);
	void viewMatch(int row);
	int selectedRow() const;
	QList<int> selectedRows() const;
	Handle<Concordance> pickConcordance(const QString &title);

	ConcordanceModel *m_model = nullptr;
	QTableView *m_table = nullptr;
	QToolBar *m_toolbar = nullptr;
	QLabel *m_count_label = nullptr;
	QSpinBox *m_target_spin = nullptr;

	// Scales menu actions (stored so we can update checked state)
	QAction *m_erb_action = nullptr;
	QAction *m_bark_action = nullptr;

	// Pitch scales menu actions
	QAction *m_pitch_st_action = nullptr;
	QAction *m_pitch_erb_action = nullptr;

	std::unique_ptr<AudioPlayer> m_player;
	Handle<Concordance> m_conc;

	bool m_show_match_info = true;
	bool m_show_context = true;
	bool m_show_metadata = false;
	bool m_open_in_split = true;
};

} // namespace phonometrica

#endif // PHONOMETRICA_CONCORDANCE_VIEW_HPP
