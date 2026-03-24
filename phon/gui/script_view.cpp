/***********************************************************************************************************************
 *                                                                                                                     *
 * Copyright (C) 2019-2026 Julien Eychenne                                                                             *
 *                                                                                                                     *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0. If a copy of the MPL was not   *
 * distributed with this file, You can obtain one at https://mozilla.org/MPL/2.0/.                                     *
 *                                                                                                                     *
 * Created: 22/03/2026                                                                                                 *
 *                                                                                                                     *
 * Purpose: see header.                                                                                                *
 *                                                                                                                     *
 ***********************************************************************************************************************/

#include <QVBoxLayout>
#include <QPlainTextEdit>
#include <QFileDialog>
#include <QMessageBox>
#include <QDesktopServices>
#include <QUrl>
#include <phon/gui/script_view.hpp>
#include <phon/gui/script_editor.hpp>
#include <phon/gui/search_bar.hpp>
#include <phon/gui/console.hpp>
#include <phon/application/settings.hpp>
#include <phon/application/project.hpp>
#include <phon/application/macros.hpp>
#include <phon/file.hpp>
#include <phon/utils/file_system.hpp>
#include <phon/runtime/error.hpp>

namespace phonometrica {

ScriptView::ScriptView(Runtime &rt, Console *console, const Handle<Script> &script, QWidget *parent) :
	View(parent), m_runtime(rt), m_console(console), m_script(script)
{
	setupUi();

	if (script->has_path())
	{
		if (!filesystem::is_file(script->path()))
		{
			QMessageBox::critical(this, tr("Invalid script"),
				tr("This file doesn't exist: %1").arg(
					QString::fromUtf8(script->path().data(), (int) script->path().size())));
			return;
		}

		auto content = File::read_all(script->path());
		m_editor->setText(QString::fromUtf8(content.data(), (int) content.size()));
	}

	connect(m_editor, &ScriptEditor::contentModified, this, &ScriptView::onModification);
}

void ScriptView::setupUi()
{
	auto *layout = new QVBoxLayout(this);
	layout->setContentsMargins(0, 0, 0, 0);
	layout->setSpacing(0);

	// ── Toolbar ────────────────────────────────────

	m_toolbar = new QToolBar(this);
	m_toolbar->setIconSize(QSize(24, 24));
	m_toolbar->setMovable(false);

	m_save_action = m_toolbar->addAction(QIcon(":/icons/save.svg"),
		tr("Save script (" CTRL_KEY "S)"));
	m_save_action->setEnabled(false);
	connect(m_save_action, &QAction::triggered, this, [this]() { save(); });

	m_toolbar->addSeparator();

	auto *run_action = m_toolbar->addAction(QIcon(":/icons/play.svg"),
		tr("Execute script or selection (" CTRL_KEY RETURN_KEY ")"));
	connect(run_action, &QAction::triggered, this, &ScriptView::execute);

	m_hint_action = m_toolbar->addAction(QIcon(":/icons/info.svg"),
		tr("Activate auto-completion and call tips"));
	m_hint_action->setCheckable(true);
	bool autohints = false;
	try { autohints = Settings::get_boolean("autohints"); } catch (...) { }
	m_hint_action->setChecked(autohints);
	connect(m_hint_action, &QAction::toggled, this, &ScriptView::onToggleHints);

	auto *bytecode_action = m_toolbar->addAction(QIcon(":/icons/binary.svg"),
		tr("View bytecode"));
	connect(bytecode_action, &QAction::triggered, this, &ScriptView::onViewBytecode);

	m_toolbar->addSeparator();

	auto *indent_action = m_toolbar->addAction(QIcon(":/icons/arrow-right-to-line.svg"),
		tr("Indent line or selection"));
	connect(indent_action, &QAction::triggered, this, &ScriptView::onIndentSelection);

	auto *unindent_action = m_toolbar->addAction(QIcon(":/icons/arrow-left-to-line.svg"),
		tr("Unindent line or selection"));
	connect(unindent_action, &QAction::triggered, this, &ScriptView::onUnindentSelection);

	auto *comment_action = m_toolbar->addAction(QIcon(":/icons/toggle-left.svg"),
		tr("Comment line or selection"));
	connect(comment_action, &QAction::triggered, this, &ScriptView::onCommentSelection);

	auto *uncomment_action = m_toolbar->addAction(QIcon(":/icons/toggle-right.svg"),
		tr("Uncomment line or selection"));
	connect(uncomment_action, &QAction::triggered, this, &ScriptView::onUncommentSelection);

	layout->addWidget(m_toolbar);

	// ── Editor ─────────────────────────────────────

	m_editor = new ScriptEditor(this);
	m_editor->activateHints(autohints);
	layout->addWidget(m_editor, 1);

	// ── Search bar ─────────────────────────────────

	m_searchbar = new SearchBar(this);
	layout->addWidget(m_searchbar);

	connect(m_searchbar, &SearchBar::findRequested, this, &ScriptView::onFind);
	connect(m_searchbar, &SearchBar::replaceRequested, this, &ScriptView::onReplace);
	connect(m_searchbar, &SearchBar::replaceAllRequested, this, &ScriptView::onReplaceAll);

	m_editor->setFocus();
}


// ─────────────────────────────────────────────────
//  Save
// ─────────────────────────────────────────────────

bool ScriptView::save()
{
	bool firstSave = !m_script->has_path();

	if (firstSave)
	{
		auto path = QFileDialog::getSaveFileName(this, tr("Save script as..."),
			QStringLiteral("untitled.phon"),
			tr("Phonometrica script (*.phon)"));

		if (path.isEmpty())
			return false;

		auto bytes = path.toUtf8();
		m_script->set_path(String(bytes.constData(), bytes.size()), false);
	}

	auto text = m_editor->text().toUtf8();
	m_script->set_content(String(text.constData(), text.size()), true);
	m_script->save();

	if (firstSave)
	{
		// Register the script with the project so it appears in the file manager.
		auto *project = Project::get();
		m_script->parent()->append(recast<Element>(m_script), false);
		project->register_file(m_script->path(), recast<Document>(m_script));
		project->modify();
		emit addedToProject();
	}

	m_save_action->setEnabled(false);
	m_script->discard_changes();
	emit titleChanged(label());
	return true;
}


// ─────────────────────────────────────────────────
//  Execute
// ─────────────────────────────────────────────────

void ScriptView::execute()
{
	QString code;
	if (m_editor->hasSelectedText())
		code = m_editor->selectedText();
	else
		code = m_editor->text();

	if (m_console)
		m_console->appendNewLine();

	auto bytes = code.toUtf8();

	try
	{
		m_runtime.do_string(String(bytes.constData(), bytes.size()));
	}
	catch (RuntimeError &e)
	{
		m_editor->showError(e.line_no());
		if (m_console)
		{
			m_console->showError(QString("Error at line %1").arg(e.line_no()));
			m_console->showError(e.what());
		}
	}
	catch (std::exception &e)
	{
		if (m_console)
		{
			m_console->showError(e.what());
		}
	}

	if (m_console)
	{
		m_console->addPrompt();
		m_console->scrollToEnd();
	}
}


// ─────────────────────────────────────────────────
//  Modification tracking
// ─────────────────────────────────────────────────

void ScriptView::onModification()
{
	if (!m_script->modified())
	{
		m_script->set_pending_modifications();
		m_save_action->setEnabled(true);
		emit titleChanged(label());
		emit modificationChanged(true);
	}
}

bool ScriptView::isModified() const
{
	return m_script->modified();
}

void ScriptView::discardChanges()
{
	m_script->discard_changes();
}

QString ScriptView::label() const
{
	auto lbl = m_script->label();
	auto qlbl = QString::fromUtf8(lbl.data(), (int) lbl.size());
	if (m_script->modified())
		qlbl += QStringLiteral(" *");
	return qlbl;
}

String ScriptView::path() const
{
	return m_script->path();
}


// ─────────────────────────────────────────────────
//  Toolbar actions
// ─────────────────────────────────────────────────

void ScriptView::onCommentSelection()
{
	m_editor->addStartCharacter(QStringLiteral("# "));
}

void ScriptView::onUncommentSelection()
{
	m_editor->removeStartCharacter(QStringLiteral("# "));
}

void ScriptView::onIndentSelection()
{
	m_editor->addStartCharacter(QStringLiteral("\t"));
}

void ScriptView::onUnindentSelection()
{
	m_editor->removeStartCharacter(QStringLiteral("\t"));
}

void ScriptView::onToggleHints(bool checked)
{
	m_editor->activateHints(checked);
}

void ScriptView::onViewBytecode()
{
	auto oldPrint = m_runtime.print;

	try
	{
		String buffer;
		m_runtime.print = [&buffer](const String &s) {
			buffer.append(s);
		};

		auto code = m_editor->text().toUtf8();
		auto closure = m_runtime.compile_string(String(code.constData(), code.size()));
		m_runtime.disassemble(*closure, "main");

		auto text = QString::fromUtf8(buffer.data(), (int) buffer.size());

		// Show in a simple dialog.
		QDialog dlg(this);
		dlg.setWindowTitle(tr("Bytecode viewer"));
		dlg.resize(700, 400);
		auto *layout = new QVBoxLayout(&dlg);
		auto *view = new QPlainTextEdit(&dlg);
		view->setReadOnly(true);
		view->setPlainText(text);
#if PHON_MACOS
		view->setFont(QFont("Monaco", 12));
#elif PHON_WINDOWS
		view->setFont(QFont("Consolas", 10));
#else
		view->setFont(QFont("Monospace", 11));
#endif
		layout->addWidget(view);
		dlg.exec();
	}
	catch (std::exception &e)
	{
		QMessageBox::critical(this, tr("Syntax error"), e.what());
	}

	m_runtime.print = oldPrint;
}


// ─────────────────────────────────────────────────
//  Find / Replace
// ─────────────────────────────────────────────────

void ScriptView::find()
{
	m_searchbar->setSearch();
}

void ScriptView::replace()
{
	m_searchbar->setSearchAndReplace();
}

void ScriptView::escape()
{
	m_searchbar->hide();
	m_editor->setFocus();
}

void ScriptView::onFind()
{
	auto needle = m_searchbar->searchText();
	if (needle.isEmpty())
		return;

	bool isRegex = m_searchbar->usesRegex();
	bool caseSensitive = m_searchbar->isCaseSensitive();

	// findFirst: expr, re, cs, wo, wrap, forward
	bool found = m_editor->findFirst(needle, isRegex, caseSensitive,
		false /*wholeWord*/, true /*wrap*/, true /*forward*/);

	if (!found)
		QMessageBox::information(this, tr("Find"), tr("Text not found!"));
}

void ScriptView::onReplace()
{
	auto needle = m_searchbar->searchText();
	auto replacement = m_searchbar->replacementText();
	if (needle.isEmpty())
		return;

	// If there is a current match (selection), replace it first.
	if (m_editor->hasSelectedText())
		m_editor->replace(replacement);

	// Then find the next occurrence.
	onFind();
}

void ScriptView::onReplaceAll()
{
	auto needle = m_searchbar->searchText();
	auto replacement = m_searchbar->replacementText();
	if (needle.isEmpty())
		return;

	bool isRegex = m_searchbar->usesRegex();
	bool caseSensitive = m_searchbar->isCaseSensitive();

	int count = 0;

	// Start from the beginning of the document.
	bool found = m_editor->findFirst(needle, isRegex, caseSensitive,
		false /*wholeWord*/, false /*wrap*/, true /*forward*/,
		0 /*line*/, 0 /*index*/);

	while (found)
	{
		m_editor->replace(replacement);
		count++;
		found = m_editor->findNext();
	}

	if (count == 0)
		QMessageBox::information(this, tr("Replace"), tr("Text not found!"));
	else
		onModification();
}

} // namespace phonometrica
