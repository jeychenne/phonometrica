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
 * Created: 22/03/2026                                                                                                 *
 *                                                                                                                     *
 * Purpose: see header.                                                                                                *
 *                                                                                                                     *
 ***********************************************************************************************************************/

#include <QVBoxLayout>
#include <QPlainTextEdit>
#include <phon/gui/file_dialog.hpp>
#include <QMessageBox>
#include <QDesktopServices>
#include <QUrl>
#include <QTimer>
#include <QKeyEvent>
#include <QShortcut>
#include <QApplication>
#include <phon/gui/script_view.hpp>
#include <phon/gui/script_editor.hpp>
#include <phon/gui/search_bar.hpp>
#include <phon/gui/console.hpp>
#include <phon/gui/font_helpers.hpp>
#include <phon/gui/help_browser.hpp>
#include <phon/gui/output_panel.hpp>
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

	// Intercept keyboard shortcuts before QScintilla swallows them.
	m_editor->installEventFilter(this);

	// QShortcuts handle Ctrl+F / Ctrl+H when a non-QScintilla child (e.g. the
	// search bar) has focus.  The event filter above handles the QScintilla case.
	auto *findShortcut = new QShortcut(QKeySequence::Find, this);
	findShortcut->setContext(Qt::WidgetWithChildrenShortcut);
	connect(findShortcut, &QShortcut::activated, this, &ScriptView::find);

	auto *replaceShortcut = new QShortcut(QKeySequence(tr("Ctrl+H")), this);
	replaceShortcut->setContext(Qt::WidgetWithChildrenShortcut);
	connect(replaceShortcut, &QShortcut::activated, this, &ScriptView::replace);

	// Defer focus: setFocus() during construction is ignored because the widget
	// is not yet visible. Posting to the next event-loop tick lets Qt process
	// the show event first, so Scintilla can accept the focus.
	QTimer::singleShot(0, m_editor, [this]() { m_editor->setFocus(); });
}


// ─────────────────────────────────────────────────
//  Save
// ─────────────────────────────────────────────────

bool ScriptView::save()
{
	bool firstSave = !m_script->has_path();

	if (firstSave)
	{
		auto path = getSaveFileName(this, tr("Save script as..."),
			tr("Phonometrica script (*.phon)"),
			defaultSaveName(m_script->label(), QStringLiteral(".phon")));

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

	// Show a busy cursor while the script runs. Script execution is synchronous
	// on the main thread (the GUI blocks until `do_string` returns), so this is
	// the main visual cue that something is happening — especially useful for
	// long operations such as fitting a model. RAII ensures the cursor is
	// restored even if an exception escapes the try blocks below.
	struct WaitCursorGuard
	{
		WaitCursorGuard()
		{
			QApplication::setOverrideCursor(Qt::WaitCursor);
			// Repaint pending events once so the cursor actually shows before
			// we enter the (blocking) runtime call.
			QApplication::processEvents();
		}
		~WaitCursorGuard() { QApplication::restoreOverrideCursor(); }
	} wait_guard;

	// Redirect `print` output and the scripting `clear()` target to the
	// OutputPanel while running a script. Console output (typed
	// interactively) is unaffected because it goes through Console::runCode.
	//
	// Both callbacks are saved and restored together. A lightweight scope
	// guard ensures restoration even if an unexpected exception escapes
	// the catch blocks below — otherwise the runtime would be left with
	// dangling lambdas capturing the OutputPanel pointer.
	auto oldPrint = m_runtime.print;
	auto oldClear = m_runtime.clear_output;

	struct Restore
	{
		Runtime &rt;
		decltype(oldPrint) &p;
		decltype(oldClear) &c;
		~Restore() { rt.print = std::move(p); rt.clear_output = std::move(c); }
	} restore{ m_runtime, oldPrint, oldClear };

	auto *output = OutputPanel::instance();
	if (output)
	{
		m_runtime.print = [output](const String &s) {
			auto qs = QString::fromUtf8(s.data(), (int) s.size());
			output->appendText(qs);
		};
		m_runtime.clear_output = [output] {
			output->clear();
		};
	}

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
			m_console->appendNewLine();
			m_console->showError(QString("Error at line %1").arg(e.line_no()));
			m_console->showError(e.what());
			m_console->addPrompt();
		}
	}
	catch (std::exception &e)
	{
		if (m_console)
		{
			m_console->appendNewLine();
			m_console->showError(e.what());
			m_console->addPrompt();
		}
	}

	// `restore` and `wait_guard` destructors put the callbacks and cursor back
	// in place, in reverse declaration order (cursor last).
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
	if (m_script->has_path())
		m_script->reload();
	else
		m_script->discard_changes();
}

QString ScriptView::label() const
{
	auto lbl = m_script->label();
	auto qlbl = tabLabel(QString::fromUtf8(lbl.data(), (int) lbl.size()));
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
		view->setFont(defaultMonoFont());
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

bool ScriptView::eventFilter(QObject *obj, QEvent *event)
{
	if (obj == m_editor)
	{
		if (event->type() == QEvent::ShortcutOverride)
		{
			// QScintilla accepts ShortcutOverride for many key combos,
			// preventing Qt from dispatching them as shortcuts. We claim
			// Ctrl+F and Ctrl+H here so QScintilla never sees them; they
			// will arrive as a normal KeyPress which we handle below.
			auto *ke = static_cast<QKeyEvent *>(event);
			if (ke->matches(QKeySequence::Find) ||
				(ke->modifiers() == Qt::ControlModifier && ke->key() == Qt::Key_H))
			{
				event->accept();
				return true;
			}
		}
		else if (event->type() == QEvent::KeyPress)
		{
			auto *ke = static_cast<QKeyEvent *>(event);
			if (ke->matches(QKeySequence::Find))
			{
				find();
				return true;
			}
			if (ke->modifiers() == Qt::ControlModifier && ke->key() == Qt::Key_H)
			{
				replace();
				return true;
			}
		}
	}
	return QWidget::eventFilter(obj, event);
}

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

void ScriptView::undo()
{
	m_editor->undo();
}

void ScriptView::redo()
{
	m_editor->redo();
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
