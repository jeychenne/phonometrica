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
 * Purpose: see header.                                                                                                *
 *                                                                                                                     *
 ***********************************************************************************************************************/

#include <QVBoxLayout>
#include <QTimer>
#include <phon/gui/file_dialog.hpp>
#include <QTextList>
#include <QTextBlock>
#include <phon/gui/note_view.hpp>
#include <phon/application/project.hpp>
#include <phon/application/constants.hpp>

namespace phonometrica {

NoteView::NoteView(const Handle<Note> &note, QWidget *parent) :
		View(parent),
		m_note(note)
{
	setupUi();

	// Load existing content.
	if (m_note->has_path())
	{
		m_note->open();
		auto &content = m_note->content();
		auto html = QString::fromUtf8(content.data(), (int) content.size());
		m_editor->setHtml(html);
		m_editor->moveCursor(QTextCursor::Start);
	}

	connect(m_editor->document(), &QTextDocument::contentsChanged, this, &NoteView::onModification);
	connect(m_editor, &QTextEdit::currentCharFormatChanged, this, &NoteView::onCurrentCharFormatChanged);
	connect(m_editor, &QTextEdit::cursorPositionChanged, this, &NoteView::onCursorPositionChanged);

	// The document starts clean after loading.
	m_editor->document()->setModified(false);
	m_save_action->setEnabled(false);

	QTimer::singleShot(0, m_editor, [this]() { m_editor->setFocus(); });
}

void NoteView::setupUi()
{
	auto *layout = new QVBoxLayout(this);
	layout->setContentsMargins(0, 0, 0, 0);
	layout->setSpacing(0);

	// ── Toolbar ──

	m_toolbar = new QToolBar(this);
	m_toolbar->setIconSize(QSize(18, 18));

	m_save_action = m_toolbar->addAction(QIcon(QStringLiteral(":/icons/save.svg")), tr("Save"));
	m_save_action->setEnabled(false);
	connect(m_save_action, &QAction::triggered, this, &NoteView::save);

	m_toolbar->addSeparator();

	// Heading combo box.
	m_heading_combo = new QComboBox;
	m_heading_combo->addItem(tr("Normal text"));    // index 0
	m_heading_combo->addItem(tr("Heading 1"));      // index 1
	m_heading_combo->addItem(tr("Heading 2"));      // index 2
	m_heading_combo->addItem(tr("Heading 3"));      // index 3
	m_heading_combo->setMinimumWidth(120);
	connect(m_heading_combo, QOverload<int>::of(&QComboBox::activated), this, &NoteView::onHeadingChanged);
	m_toolbar->addWidget(m_heading_combo);

	m_toolbar->addSeparator();

	m_bold_action = m_toolbar->addAction(QIcon(QStringLiteral(":/icons/bold.svg")), tr("Bold"));
	m_bold_action->setShortcut(QKeySequence::Bold);
	m_bold_action->setCheckable(true);
	connect(m_bold_action, &QAction::triggered, this, &NoteView::onBold);

	m_italic_action = m_toolbar->addAction(QIcon(QStringLiteral(":/icons/italic.svg")), tr("Italic"));
	m_italic_action->setShortcut(QKeySequence::Italic);
	m_italic_action->setCheckable(true);
	connect(m_italic_action, &QAction::triggered, this, &NoteView::onItalic);

	m_underline_action = m_toolbar->addAction(QIcon(QStringLiteral(":/icons/underline.svg")), tr("Underline"));
	m_underline_action->setShortcut(QKeySequence::Underline);
	m_underline_action->setCheckable(true);
	connect(m_underline_action, &QAction::triggered, this, &NoteView::onUnderline);

	m_toolbar->addSeparator();

	auto *bullet_action = m_toolbar->addAction(QIcon(QStringLiteral(":/icons/list.svg")), tr("Bullet list"));
	connect(bullet_action, &QAction::triggered, this, &NoteView::onBulletList);

	auto *numbered_action = m_toolbar->addAction(QIcon(QStringLiteral(":/icons/list-ordered.svg")), tr("Numbered list"));
	connect(numbered_action, &QAction::triggered, this, &NoteView::onNumberedList);

	layout->addWidget(m_toolbar);

	// ── Editor ──

	m_editor = new QTextEdit(this);
	m_editor->setAcceptRichText(true);
	m_editor->setTabChangesFocus(false);
	layout->addWidget(m_editor);
}

// ─────────────────────────────────────────────────
//  View interface
// ─────────────────────────────────────────────────

QString NoteView::label() const
{
	auto qlabel = QString::fromUtf8(m_note->label().data(), (int) m_note->label().size());
	if (isModified())
		qlabel += QStringLiteral(" *");
	return tabLabel(qlabel);
}

String NoteView::path() const
{
	return m_note->path();
}

Document *NoteView::document() const
{
	return m_note.get();
}

bool NoteView::isModified() const
{
	return m_editor->document()->isModified();
}

bool NoteView::save()
{
	bool firstSave = !m_note->has_path();

	if (firstSave)
	{
		auto path = getSaveFileName(this, tr("Save note as..."),
			tr("Research note (*%1)").arg(QStringLiteral(PHON_EXT_NOTE)),
			defaultSaveName(m_note->label(), QStringLiteral(PHON_EXT_NOTE)));

		if (path.isEmpty())
			return false;

		auto bytes = path.toUtf8();
		m_note->set_path(String(bytes.constData(), bytes.size()), false);
	}

	auto html = m_editor->toHtml().toUtf8();
	m_note->set_content(String(html.constData(), html.size()), true);
	m_note->save();

	if (firstSave)
	{
		auto *project = Project::get();
		m_note->parent()->append(handle_cast<Element>(m_note), false);
		project->register_file(m_note->path(), handle_cast<Document>(m_note));
		project->modify();
		emit addedToProject();
	}

	m_save_action->setEnabled(false);
	m_editor->document()->setModified(false);
	m_note->discard_changes();
	emit titleChanged(label());
	return true;
}

void NoteView::discardChanges()
{
	m_editor->document()->setModified(false);
	m_note->discard_changes();
}

void NoteView::find()
{
	// No find bar for now.
}

void NoteView::replace()
{
	// No replace bar for now.
}

void NoteView::undo()
{
	m_editor->undo();
}

void NoteView::redo()
{
	m_editor->redo();
}

// ─────────────────────────────────────────────────
//  Formatting actions
// ─────────────────────────────────────────────────

void NoteView::onModification()
{
	// Only mark the note as modified when the document truly has unsaved content.
	// This avoids spurious modifications (e.g. from setModified(false) during save).
	if (m_editor->document()->isModified() && !m_note->modified())
	{
		m_note->set_pending_modifications();
		m_save_action->setEnabled(true);
		emit titleChanged(label());
	}
}

void NoteView::onBold()
{
	auto fmt = m_editor->currentCharFormat();
	fmt.setFontWeight(m_bold_action->isChecked() ? QFont::Bold : QFont::Normal);
	m_editor->mergeCurrentCharFormat(fmt);
}

void NoteView::onItalic()
{
	auto fmt = m_editor->currentCharFormat();
	fmt.setFontItalic(m_italic_action->isChecked());
	m_editor->mergeCurrentCharFormat(fmt);
}

void NoteView::onUnderline()
{
	auto fmt = m_editor->currentCharFormat();
	fmt.setFontUnderline(m_underline_action->isChecked());
	m_editor->mergeCurrentCharFormat(fmt);
}

void NoteView::onBulletList()
{
	auto cursor = m_editor->textCursor();
	auto *list = cursor.currentList();

	if (list && list->format().style() == QTextListFormat::ListDisc)
	{
		// Remove the list.
		QTextBlockFormat bfmt;
		bfmt.setObjectIndex(-1);
		cursor.mergeBlockFormat(bfmt);
		list->remove(cursor.block());
	}
	else
	{
		QTextListFormat fmt;
		fmt.setStyle(QTextListFormat::ListDisc);
		cursor.createList(fmt);
	}
}

void NoteView::onNumberedList()
{
	auto cursor = m_editor->textCursor();
	auto *list = cursor.currentList();

	if (list && list->format().style() == QTextListFormat::ListDecimal)
	{
		QTextBlockFormat bfmt;
		bfmt.setObjectIndex(-1);
		cursor.mergeBlockFormat(bfmt);
		list->remove(cursor.block());
	}
	else
	{
		QTextListFormat fmt;
		fmt.setStyle(QTextListFormat::ListDecimal);
		cursor.createList(fmt);
	}
}

void NoteView::onHeadingChanged(int index)
{
	if (m_updating_heading)
		return;

	auto cursor = m_editor->textCursor();
	QTextBlockFormat bfmt = cursor.blockFormat();
	QTextCharFormat cfmt;

	if (index == 0)
	{
		// Normal text.
		bfmt.setHeadingLevel(0);
		cfmt.setFontWeight(QFont::Normal);
		cfmt.setProperty(QTextFormat::FontSizeAdjustment, 0);
	}
	else
	{
		bfmt.setHeadingLevel(index);
		cfmt.setFontWeight(QFont::Bold);
		// H1 = +2, H2 = +1, H3 = 0
		cfmt.setProperty(QTextFormat::FontSizeAdjustment, 3 - index);
	}

	cursor.beginEditBlock();
	cursor.mergeBlockFormat(bfmt);

	// Apply character format to entire block.
	cursor.select(QTextCursor::BlockUnderCursor);
	cursor.mergeCharFormat(cfmt);
	cursor.endEditBlock();
}

void NoteView::onCurrentCharFormatChanged(const QTextCharFormat &format)
{
	updateFormatActions();
}

void NoteView::onCursorPositionChanged()
{
	updateFormatActions();

	// Update heading combo to reflect the current block's heading level.
	m_updating_heading = true;
	auto cursor = m_editor->textCursor();
	int level = cursor.blockFormat().headingLevel();
	if (level >= 0 && level <= 3)
		m_heading_combo->setCurrentIndex(level);
	m_updating_heading = false;
}

void NoteView::updateFormatActions()
{
	auto fmt = m_editor->currentCharFormat();
	m_bold_action->setChecked(fmt.fontWeight() >= QFont::Bold);
	m_italic_action->setChecked(fmt.fontItalic());
	m_underline_action->setChecked(fmt.fontUnderline());
}

} // namespace phonometrica
