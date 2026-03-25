/***********************************************************************************************************************
 *                                                                                                                     *
 * Copyright (C) 2019-2026 Julien Eychenne                                                                             *
 *                                                                                                                     *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0. If a copy of the MPL was not   *
 * distributed with this file, You can obtain one at https://mozilla.org/MPL/2.0/.                                     *
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
