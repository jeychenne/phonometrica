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
 * Created: 12/04/2026                                                                                                 *
 *                                                                                                                     *
 * Purpose: see header.                                                                                                *
 *                                                                                                                     *
 ***********************************************************************************************************************/

#include <QLabel>
#include <QPushButton>
#include <QFrame>
#include <QIcon>
#include <QFileInfo>
#include <QShowEvent>
#include <phon/gui/start_view.hpp>
#include <phon/application/settings.hpp>
#include <phon/runtime/variant.hpp>
#include <phon/utils/helpers.hpp>

namespace phonometrica {

// ---------------------------------------------------------------------------
//  Helpers
// ---------------------------------------------------------------------------

// Create a flat action button with an icon and text, fixed height.
static QPushButton *makeActionButton(const QString &iconPath, const QString &text, QWidget *parent)
{
	auto *btn = new QPushButton(QIcon(iconPath), text, parent);
	btn->setFlat(true);
	btn->setCursor(Qt::PointingHandCursor);
	btn->setIconSize(QSize(20, 20));
	btn->setFixedHeight(38);
	btn->setStyleSheet(QStringLiteral(
		"QPushButton {"
		"  text-align: left;"
		"  padding: 4px 12px;"
		"  border-radius: 6px;"
		"  font-size: 13px;"
		"}"
		"QPushButton:hover {"
		"  background: palette(midlight);"
		"}"
	));
	return btn;
}

// Create a clickable label styled as a link for a recent project path.
static QPushButton *makeRecentEntry(const QString &displayText, QWidget *parent)
{
	auto *btn = new QPushButton(displayText, parent);
	btn->setFlat(true);
	btn->setCursor(Qt::PointingHandCursor);
	btn->setFixedHeight(28);
	btn->setStyleSheet(QStringLiteral(
		"QPushButton {"
		"  text-align: left;"
		"  padding: 2px 8px;"
		"  border-radius: 4px;"
		"  color: palette(link);"
		"  font-size: 12px;"
		"}"
		"QPushButton:hover {"
		"  background: palette(midlight);"
		"  text-decoration: underline;"
		"}"
	));
	return btn;
}

// ---------------------------------------------------------------------------
//  StartView
// ---------------------------------------------------------------------------

StartView::StartView(QWidget *parent) : QWidget(parent)
{
	// ── Outer layout: centers everything vertically ────────────────
	auto *outer = new QVBoxLayout(this);
	outer->setAlignment(Qt::AlignCenter);

	// ── Inner container with a max width ───────────────────────────
	auto *container = new QWidget(this);
	container->setMaximumWidth(420);
	auto *layout = new QVBoxLayout(container);
	layout->setSpacing(6);
	layout->setContentsMargins(24, 0, 24, 0);

	// ── App icon ───────────────────────────────────────────────────
	auto *icon_label = new QLabel(container);
	QPixmap icon(":/icons/phonometrica.svg");
	icon_label->setPixmap(icon.scaled(72, 72, Qt::KeepAspectRatio, Qt::SmoothTransformation));
	icon_label->setAlignment(Qt::AlignCenter);
	layout->addWidget(icon_label);

	// ── Title ──────────────────────────────────────────────────────
	auto *title = new QLabel(QStringLiteral("<h2 style='margin-bottom:0'>Phonometrica</h2>"), container);
	title->setAlignment(Qt::AlignCenter);
	layout->addWidget(title);

	// ── Version + tagline ──────────────────────────────────────────
	auto version = QString::fromStdString(utils::get_version());
	auto *subtitle = new QLabel(
		QStringLiteral("<span style='color:gray; font-size:12px;'>Version %1</span><br>")
			.arg(version),
		container);
	subtitle->setAlignment(Qt::AlignCenter);
	layout->addWidget(subtitle);

	layout->addSpacing(16);

	// ── Quick actions ──────────────────────────────────────────────
	auto *actions_label = new QLabel(QStringLiteral(
		"<span style='font-size:11px; color:gray; text-transform:uppercase; letter-spacing:1px;'>Quick actions</span>"),
		container);
	layout->addWidget(actions_label);

	auto *open_btn = makeActionButton(QStringLiteral(":/icons/database.svg"),    tr("Open Project..."),    container);
	auto *add_btn  = makeActionButton(QStringLiteral(":/icons/circle-plus.svg"), tr("Add Files..."),       container);
	auto *annot_btn  = makeActionButton(QStringLiteral(":/icons/waveform.svg"),  tr("New Annotation..."),  container);
	auto *analyze_btn = makeActionButton(QStringLiteral(":/icons/statistics.svg"), tr("Analyze Data..."),  container);
	auto *help_btn   = makeActionButton(QStringLiteral(":/icons/circle-help.svg"), tr("Documentation"),    container);

	layout->addWidget(open_btn);
	layout->addWidget(add_btn);
	layout->addWidget(annot_btn);
	layout->addWidget(analyze_btn);
	layout->addWidget(help_btn);

	connect(open_btn,    &QPushButton::clicked, this, &StartView::openProjectRequested);
	connect(add_btn,     &QPushButton::clicked, this, &StartView::addFilesRequested);
	connect(annot_btn,   &QPushButton::clicked, this, &StartView::newAnnotationRequested);
	connect(analyze_btn, &QPushButton::clicked, this, &StartView::analyzeDataRequested);
	connect(help_btn,    &QPushButton::clicked, this, &StartView::documentationRequested);

	// ── Recent projects section ────────────────────────────────────
	m_recent_section = new QWidget(container);
	auto *recent_vbox = new QVBoxLayout(m_recent_section);
	recent_vbox->setContentsMargins(0, 0, 0, 0);
	recent_vbox->setSpacing(4);

	auto *recent_label = new QLabel(QStringLiteral(
		"<span style='font-size:11px; color:gray; text-transform:uppercase; letter-spacing:1px;'>Recent projects</span>"),
		m_recent_section);
	recent_vbox->addWidget(recent_label);

	m_recent_layout = new QVBoxLayout();
	m_recent_layout->setSpacing(0);
	m_recent_layout->setContentsMargins(0, 0, 0, 0);
	recent_vbox->addLayout(m_recent_layout);

	layout->addSpacing(12);
	layout->addWidget(m_recent_section);

	// ── Populate recent list ───────────────────────────────────────
	refreshRecentProjects();

	// ── Final assembly ─────────────────────────────────────────────
	outer->addStretch();
	outer->addWidget(container, 0, Qt::AlignHCenter);
	outer->addStretch();
}

void StartView::refreshRecentProjects()
{
	// Clear existing entries.
	while (auto *item = m_recent_layout->takeAt(0))
	{
		delete item->widget();
		delete item;
	}

	bool has_recent = false;

	try
	{
		auto &lst = Settings::get_list("recent_projects");

		for (intptr_t i = 1; i <= lst.size() && i <= 10; i++)
		{
			auto path = cast<String>(lst[i]);
			auto qpath = QString::fromUtf8(path.data(), (int) path.size());

			// Show just the filename for a compact display, with full path as tooltip.
			auto display = QFileInfo(qpath).fileName();
			if (display.isEmpty())
				display = qpath;

			auto *entry = makeRecentEntry(display, m_recent_section);
			entry->setToolTip(qpath);

			connect(entry, &QPushButton::clicked, this, [this, path]() {
				emit recentProjectRequested(path);
			});

			m_recent_layout->addWidget(entry);
			has_recent = true;
		}
	}
	catch (...)
	{
	}

	m_recent_section->setVisible(has_recent);
}

void StartView::showEvent(QShowEvent *event)
{
	QWidget::showEvent(event);
	refreshRecentProjects();
}

} // namespace phonometrica
