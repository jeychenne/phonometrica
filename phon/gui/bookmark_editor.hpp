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
 * Created: 24/04/2026                                                                                                 *
 *                                                                                                                     *
 * Purpose: Modal dialog for editing bookmark metadata (title and notes) before a bookmark is committed to the        *
 *          project. Used from the concordance view when the user bookmarks a match, and from the annotation view     *
 *          when the user bookmarks a selected event. A read-only context preview is shown at the top so the user     *
 *          can verify what they are bookmarking. The dialog exposes the edited title and notes via getters; it does  *
 *          NOT mutate a bookmark directly, so the caller is free to construct the bookmark on accept.                *
 *                                                                                                                     *
 ***********************************************************************************************************************/

#ifndef PHONOMETRICA_BOOKMARK_EDITOR_HPP
#define PHONOMETRICA_BOOKMARK_EDITOR_HPP

#include <QDialog>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QLabel>
#include <QDialogButtonBox>

namespace phonometrica {

class BookmarkEditor final : public QDialog
{
	Q_OBJECT

public:

	/// @param default_title  Seeded into the title field (user may edit or clear).
	/// @param default_notes  Seeded into the notes field (usually empty when creating).
	/// @param context_info   Read-only single-line description of what is being bookmarked
	///                       (e.g. "File: foo.wav — Layer 2 — 1.234s–1.567s — \"schwa\"").
	///                       Displayed at the top of the dialog. Pass an empty string to omit.
	BookmarkEditor(const QString &default_title,
	               const QString &default_notes,
	               const QString &context_info,
	               QWidget *parent = nullptr);

	/// Title entered by the user. Guaranteed non-empty when the dialog is accepted
	/// (accept() re-validates and refuses to close if the title is blank).
	QString title() const;

	/// Notes entered by the user. May be empty.
	QString notes() const;

public slots:

	void accept() override;

private slots:

	void onTitleChanged(const QString &text);

private:

	QLabel            *m_context_label = nullptr;
	QLineEdit         *m_title_edit    = nullptr;
	QPlainTextEdit    *m_notes_edit    = nullptr;
	QLabel            *m_warning_label = nullptr;
	QDialogButtonBox  *m_buttons       = nullptr;
};

} // namespace phonometrica

#endif // PHONOMETRICA_BOOKMARK_EDITOR_HPP
