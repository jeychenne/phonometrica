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
 * Created: 06/04/2026                                                                                                 *
 *                                                                                                                     *
 * Purpose: View for research notes. Provides a rich-text editor (QTextEdit) with a minimal formatting toolbar         *
 * (bold, italic, underline, bullet list, heading levels) and serialises to HTML.                                      *
 *                                                                                                                     *
 ***********************************************************************************************************************/

#ifndef PHONOMETRICA_NOTE_VIEW_HPP
#define PHONOMETRICA_NOTE_VIEW_HPP

#include <QToolBar>
#include <QTextEdit>
#include <QComboBox>
#include <QAction>
#include <phon/gui/view.hpp>
#include <phon/application/note.hpp>

namespace phonometrica {

class NoteView : public View
{
	Q_OBJECT

public:

	explicit NoteView(const Handle<Note> &note, QWidget *parent = nullptr);

	// ── View interface ─────────────────────────────────

	QString label() const override;
	String path() const override;
	Document *document() const override;
	bool isModified() const override;
	bool save() override;
	void discardChanges() override;
	void find() override;
	void replace() override;
	bool supportsFind() const override { return false; }
	void undo() override;
	void redo() override;

	QString helpAnchor() const override { return {}; }

	Handle<Note> note() const { return m_note; }

private slots:

	void onModification();
	void onBold();
	void onItalic();
	void onUnderline();
	void onBulletList();
	void onNumberedList();
	void onHeadingChanged(int index);
	void onCurrentCharFormatChanged(const QTextCharFormat &format);
	void onCursorPositionChanged();

private:

	void setupUi();
	void updateFormatActions();

	Handle<Note> m_note;

	QTextEdit *m_editor = nullptr;
	QToolBar *m_toolbar = nullptr;
	QComboBox *m_heading_combo = nullptr;

	QAction *m_save_action = nullptr;
	QAction *m_bold_action = nullptr;
	QAction *m_italic_action = nullptr;
	QAction *m_underline_action = nullptr;

	bool m_updating_heading = false;
};

} // namespace phonometrica

#endif // PHONOMETRICA_NOTE_VIEW_HPP
