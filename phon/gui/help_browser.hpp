/***********************************************************************************************************************
 *                                                                                                                     *
 * Copyright (C) 2019-2026 Julien Eychenne                                                                             *
 *                                                                                                                     *
 * This program is free software: you can redistribute it and/or modify it under the terms of the GNU General Public   *
 * License as published by the Free Software Foundation, either version 2 of the License, or (at your option) any      *
 * later version.                                                                                                      *
 *                                                                                                                     *
 * This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied  *
 * warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for more       *
 * details.                                                                                                            *
 *                                                                                                                     *
 * You should have received a copy of the GNU General Public License along with this program. If not, see              *
 * <http://www.gnu.org/licenses/>.                                                                                     *
 *                                                                                                                     *
 * Created: 25/03/2026                                                                                                 *
 *                                                                                                                     *
 * Purpose: In-app help browser. Displays documentation pages embedded as Qt resources in a                            *
 *          QTextBrowser dialog. The Sphinx qthelp builder produces the HTML at build time;                             *
 *          CMake compiles it into the binary via rcc.                                                                  *
 *                                                                                                                     *
 *          Public API: call HelpBrowser::showPage("sound") from anywhere. The dialog is                               *
 *          created once and reused across calls (non-modal, can stay open while working).                              *
 *                                                                                                                     *
 ***********************************************************************************************************************/

#ifndef PHONOMETRICA_HELP_BROWSER_HPP
#define PHONOMETRICA_HELP_BROWSER_HPP

#include <QDialog>
#include <QPointer>

class QTextBrowser;
class QAction;

namespace phonometrica {

class HelpBrowser : public QDialog
{
	Q_OBJECT

public:

	// Show the help page identified by its anchor.
	// The anchor is a Sphinx page name relative to the doc root, without extension.
	// Examples: "sound", "scripting/index", "intro/install".
	// If the anchor is empty the top-level index page is opened.
	static void showPage(const QString &anchor = {}, QWidget *parent = nullptr);

private:

	explicit HelpBrowser(QWidget *parent);

	void navigateTo(const QString &anchor);

	void updateNavActions();

	QTextBrowser *m_browser = nullptr;
	QAction *m_back_action = nullptr;
	QAction *m_forward_action = nullptr;

	// Single instance, lazily created.
	static QPointer<HelpBrowser> s_instance;
};

} // namespace phonometrica

#endif // PHONOMETRICA_HELP_BROWSER_HPP
