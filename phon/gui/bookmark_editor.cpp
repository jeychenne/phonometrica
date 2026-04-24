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
 * Purpose: see header.                                                                                                *
 *                                                                                                                     *
 ***********************************************************************************************************************/

#include <QVBoxLayout>
#include <QFormLayout>
#include <QFrame>
#include <QPushButton>
#include <phon/gui/bookmark_editor.hpp>

namespace phonometrica {

BookmarkEditor::BookmarkEditor(const QString &default_title,
                               const QString &default_notes,
                               const QString &context_info,
                               QWidget *parent)
	: QDialog(parent)
{
	setWindowTitle(tr("Bookmark"));
	// A modest default size: wide enough to comfortably see a sentence of notes,
	// tall enough for a few lines of text.
	resize(520, 340);

	auto *main = new QVBoxLayout(this);

	// ── Optional context preview (read-only) ─────────────────
	// Shown at the top so the user can verify what they are bookmarking
	// before committing. Wraps in case the match text is long.
	if (!context_info.isEmpty())
	{
		m_context_label = new QLabel(context_info);
		m_context_label->setWordWrap(true);
		m_context_label->setTextInteractionFlags(Qt::TextSelectableByMouse);
		m_context_label->setStyleSheet(QStringLiteral(
			"QLabel { color: palette(mid); padding: 4px 0px; }"));
		main->addWidget(m_context_label);

		auto *sep = new QFrame;
		sep->setFrameShape(QFrame::HLine);
		sep->setFrameShadow(QFrame::Sunken);
		main->addWidget(sep);
	}

	// ── Title / Notes fields ────────────────────────────────
	m_title_edit = new QLineEdit;
	m_title_edit->setText(default_title);
	m_title_edit->setPlaceholderText(tr("A short label for the file manager"));
	// Select-all so the user can immediately replace or keep the default.
	m_title_edit->selectAll();

	m_notes_edit = new QPlainTextEdit;
	m_notes_edit->setPlainText(default_notes);
	m_notes_edit->setPlaceholderText(tr("Optional notes about this bookmark"));
	// Tab should move to the next widget rather than insert a tab character.
	m_notes_edit->setTabChangesFocus(true);

	auto *form = new QFormLayout;
	form->addRow(tr("Title:"), m_title_edit);
	form->addRow(tr("Notes:"), m_notes_edit);
	main->addLayout(form, 1);

	// ── Inline validation warning ───────────────────────────
	// Shown (in red) if the user presses OK with an empty title.
	// Hidden by default and while the field has content.
	m_warning_label = new QLabel;
	m_warning_label->setStyleSheet(QStringLiteral("QLabel { color: #c0392b; }"));
	m_warning_label->hide();
	main->addWidget(m_warning_label);

	// ── Standard OK/Cancel buttons ──────────────────────────
	m_buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
	main->addWidget(m_buttons);

	connect(m_buttons, &QDialogButtonBox::accepted, this, &BookmarkEditor::accept);
	connect(m_buttons, &QDialogButtonBox::rejected, this, &BookmarkEditor::reject);

	// Disable OK while the title is empty, so the blocked case is discoverable
	// before the user clicks. The inline warning only appears if they try anyway
	// (e.g. by pressing Enter on an empty field).
	connect(m_title_edit, &QLineEdit::textChanged, this, &BookmarkEditor::onTitleChanged);
	onTitleChanged(m_title_edit->text());

	// Focus the title field so the user can start typing (or tab into notes) immediately.
	m_title_edit->setFocus();
}

QString BookmarkEditor::title() const
{
	return m_title_edit->text().trimmed();
}

QString BookmarkEditor::notes() const
{
	return m_notes_edit->toPlainText();
}

void BookmarkEditor::onTitleChanged(const QString &text)
{
	const bool ok = !text.trimmed().isEmpty();
	if (auto *ok_button = m_buttons->button(QDialogButtonBox::Ok))
		ok_button->setEnabled(ok);

	if (ok)
		m_warning_label->hide();
}

void BookmarkEditor::accept()
{
	// Guard in case something bypassed the OK-button disable (e.g. Enter on an empty
	// title). Show an inline warning instead of silently rejecting, as the Dolmen
	// version did, so the user understands why nothing happened.
	if (m_title_edit->text().trimmed().isEmpty())
	{
		m_warning_label->setText(tr("Please enter a title for the bookmark."));
		m_warning_label->show();
		m_title_edit->setFocus();
		return;
	}

	QDialog::accept();
}

} // namespace phonometrica
