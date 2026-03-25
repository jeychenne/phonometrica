/***********************************************************************************************************************
 *                                                                                                                     *
 * Copyright (C) 2019-2026 Julien Eychenne                                                                             *
 *                                                                                                                     *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0. If a copy of the MPL was not   *
 * distributed with this file, You can obtain one at https://mozilla.org/MPL/2.0/.                                     *
 *                                                                                                                     *
 * Created: 25/03/2026                                                                                                 *
 *                                                                                                                     *
 * Purpose: see header.                                                                                                *
 *                                                                                                                     *
 ***********************************************************************************************************************/

#include <QVBoxLayout>
#include <QToolBar>
#include <QTextBrowser>
#include <QMessageBox>
#include <QFile>
#include <phon/gui/help_browser.hpp>

namespace phonometrica {

// Minimal stylesheet for rendering Sphinx qthelp HTML in QTextBrowser.
// QTextBrowser supports a subset of CSS 2.1 — keep it simple.
static const char *defaultStyleSheet = R"(
    body {
        font-family: sans-serif;
        font-size: 10pt;
        margin: 8px;
        color: #1a1a1a;
    }
    h1 { font-size: 16pt; margin-top: 12px; margin-bottom: 8px; }
    h2 { font-size: 13pt; margin-top: 10px; margin-bottom: 6px; }
    h3 { font-size: 11pt; margin-top: 8px;  margin-bottom: 4px; }
    h4 { font-size: 10pt; margin-top: 6px;  margin-bottom: 4px; }

    a { color: #0057ae; }

    code, tt {
        font-family: monospace;
        background-color: #f0f0f0;
        padding: 1px 3px;
    }
    pre {
        font-family: monospace;
        background-color: #f5f5f5;
        border: 1px solid #ddd;
        padding: 8px;
        margin: 6px 0;
    }

    dt { font-weight: bold; margin-top: 6px; }
    dd { margin-left: 20px; }

    .admonition, .note, .warning, .tip {
        border: 1px solid #ccc;
        background-color: #fafafa;
        padding: 6px;
        margin: 8px 0;
    }
    .warning { border-color: #c9a227; background-color: #fff9e6; }

    li { margin-bottom: 3px; }

    .math { font-style: italic; }
)";

QPointer<HelpBrowser> HelpBrowser::s_instance;


void HelpBrowser::showPage(const QString &anchor, QWidget *parent)
{
#ifdef PHON_EMBEDDED_DOCS
	if (!s_instance) {
		s_instance = new HelpBrowser(parent);
	}
	s_instance->navigateTo(anchor);
	s_instance->show();
	s_instance->raise();
	s_instance->activateWindow();
#else
	Q_UNUSED(anchor);
	QMessageBox::information(parent, QObject::tr("Help"),
		QObject::tr("The documentation was not embedded in this build.\n\n"
		            "Install Sphinx and rebuild:\n"
		            "  pip install sphinx\n"
		            "  cmake --build . --target phonometrica"));
#endif
}


HelpBrowser::HelpBrowser(QWidget *parent)
	: QDialog(parent, Qt::Window)      // Qt::Window gives it a proper title bar and taskbar entry
{
	setWindowTitle(tr("Phonometrica Help"));
	resize(720, 560);
	setAttribute(Qt::WA_DeleteOnClose, false); // keep alive for reuse

	auto *layout = new QVBoxLayout(this);
	layout->setContentsMargins(0, 0, 0, 0);

	// ── Navigation toolbar ───────────────────────────
	auto *toolbar = new QToolBar(this);
	toolbar->setMovable(false);
	toolbar->setIconSize(QSize(16, 16));

	m_back_action = toolbar->addAction(QIcon(":/icons/arrow-left.svg"), tr("Back"));
	m_back_action->setEnabled(false);

	m_forward_action = toolbar->addAction(QIcon(":/icons/arrow-right.svg"), tr("Forward"));
	m_forward_action->setEnabled(false);

	toolbar->addSeparator();

	auto *home_action = toolbar->addAction(QIcon(":/icons/book-marked.svg"), tr("Home"));

	layout->addWidget(toolbar);

	// ── Browser ──────────────────────────────────────
	m_browser = new QTextBrowser(this);
	m_browser->setOpenLinks(true);
	m_browser->setOpenExternalLinks(true);
	m_browser->document()->setDefaultStyleSheet(defaultStyleSheet);
	layout->addWidget(m_browser);

	// Connections
	connect(m_back_action, &QAction::triggered, m_browser, &QTextBrowser::backward);
	connect(m_forward_action, &QAction::triggered, m_browser, &QTextBrowser::forward);
	connect(home_action, &QAction::triggered, this, [this]() { navigateTo({}); });

	connect(m_browser, &QTextBrowser::backwardAvailable,
		m_back_action, &QAction::setEnabled);
	connect(m_browser, &QTextBrowser::forwardAvailable,
		m_forward_action, &QAction::setEnabled);
}


void HelpBrowser::navigateTo(const QString &anchor)
{
	QString page = anchor.isEmpty()
		? QStringLiteral("index.html")
		: anchor + QStringLiteral(".html");

	m_browser->setSource(QUrl(QStringLiteral("qrc:/docs/") + page));
}


void HelpBrowser::updateNavActions()
{
	m_back_action->setEnabled(m_browser->isBackwardAvailable());
	m_forward_action->setEnabled(m_browser->isForwardAvailable());
}

} // namespace phonometrica
