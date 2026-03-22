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
#include <QEvent>
#include <phon/gui/view_panel.hpp>

namespace phonometrica {

ViewPanel::ViewPanel(View *view, QWidget *parent) :
	QWidget(parent)
{
	auto *layout = new QVBoxLayout(this);
	layout->setContentsMargins(0, 0, 0, 0);
	layout->setSpacing(0);

	m_splitter = new QSplitter(Qt::Vertical, this);
	m_splitter->setChildrenCollapsible(false);
	layout->addWidget(m_splitter);

	m_splitter->addWidget(view);
	trackView(view);
	m_active_view = view;
}


// ─────────────────────────────────────────────────
//  View access
// ─────────────────────────────────────────────────

View *ViewPanel::primaryView() const
{
	if (m_splitter->count() == 0)
		return nullptr;
	return qobject_cast<View *>(m_splitter->widget(0));
}

View *ViewPanel::activeView() const
{
	// If the tracked active view is still in the splitter, use it.
	if (m_active_view)
	{
		for (int i = 0; i < m_splitter->count(); i++)
		{
			if (m_splitter->widget(i) == m_active_view)
				return m_active_view;
		}
	}
	// Fallback to primary.
	return primaryView();
}

QList<View *> ViewPanel::views() const
{
	QList<View *> result;
	for (int i = 0; i < m_splitter->count(); i++)
	{
		auto *v = qobject_cast<View *>(m_splitter->widget(i));
		if (v)
			result.append(v);
	}
	return result;
}

int ViewPanel::viewCount() const
{
	return m_splitter->count();
}


// ─────────────────────────────────────────────────
//  Splitting
// ─────────────────────────────────────────────────

void ViewPanel::addView(View *view, Qt::Orientation orientation)
{
	// If the orientation differs from the current splitter, change it.
	// This is safe: QSplitter reflows its children when orientation changes.
	// For the first split (going from 1 to 2 views), set the orientation.
	// For subsequent additions, we keep the existing orientation (the caller
	// can set it explicitly if needed, but mixing orientations would need
	// nested splitters which we don't do for now).
	if (m_splitter->count() <= 1)
		m_splitter->setOrientation(orientation);

	m_splitter->addWidget(view);
	trackView(view);

	// Give each view equal space.
	QList<int> sizes;
	int total = (orientation == Qt::Vertical) ? m_splitter->height() : m_splitter->width();
	int n = m_splitter->count();
	for (int i = 0; i < n; i++)
		sizes.append(total / n);
	m_splitter->setSizes(sizes);
}

void ViewPanel::removeView(View *view)
{
	if (!view)
		return;

	untrackView(view);

	// Reset active view if we're removing it.
	if (m_active_view == view)
		m_active_view = nullptr;

	view->setParent(nullptr);
	view->deleteLater();

	if (m_splitter->count() == 0)
		emit lastViewClosed();
}

View *ViewPanel::detachView(View *view)
{
	if (!view)
		return nullptr;

	// Make sure it's actually one of ours.
	bool found = false;
	for (int i = 0; i < m_splitter->count(); i++)
	{
		if (m_splitter->widget(i) == view)
		{
			found = true;
			break;
		}
	}
	if (!found)
		return nullptr;

	untrackView(view);

	if (m_active_view == view)
		m_active_view = nullptr;

	// Reparent to nullptr — caller takes ownership.
	view->setParent(nullptr);

	emit viewDetached(view);

	if (m_splitter->count() == 0)
		emit lastViewClosed();

	return view;
}

bool ViewPanel::isSplit() const
{
	return m_splitter->count() > 1;
}


// ─────────────────────────────────────────────────
//  Aggregate queries
// ─────────────────────────────────────────────────

QString ViewPanel::label() const
{
	auto *pv = primaryView();
	return pv ? pv->label() : QString();
}

bool ViewPanel::isModified() const
{
	for (int i = 0; i < m_splitter->count(); i++)
	{
		auto *v = qobject_cast<View *>(m_splitter->widget(i));
		if (v && v->isModified())
			return true;
	}
	return false;
}

bool ViewPanel::saveAll()
{
	bool ok = true;
	for (int i = 0; i < m_splitter->count(); i++)
	{
		auto *v = qobject_cast<View *>(m_splitter->widget(i));
		if (v && v->isModified())
		{
			if (!v->save())
				ok = false;
		}
	}
	return ok;
}


// ─────────────────────────────────────────────────
//  Focus tracking
// ─────────────────────────────────────────────────

bool ViewPanel::eventFilter(QObject *watched, QEvent *event)
{
	if (event->type() == QEvent::FocusIn)
	{
		// Walk up the widget tree from the focused widget to find which View it belongs to.
		auto *w = qobject_cast<QWidget *>(watched);
		while (w)
		{
			auto *v = qobject_cast<View *>(w);
			if (v)
			{
				m_active_view = v;
				break;
			}
			w = w->parentWidget();
		}
	}
	return QWidget::eventFilter(watched, event);
}

void ViewPanel::trackView(View *view)
{
	// Install event filter on the view and all its descendants to catch focus changes.
	view->installEventFilter(this);
	for (auto *child : view->findChildren<QWidget *>())
		child->installEventFilter(this);

	// Forward title changes from any view to the panel signal.
	connect(view, &View::titleChanged, this, [this](const QString &) {
		// Always report the primary view's label as the tab title.
		emit titleChanged(label());
	});
}

void ViewPanel::untrackView(View *view)
{
	view->removeEventFilter(this);
	for (auto *child : view->findChildren<QWidget *>())
		child->removeEventFilter(this);

	disconnect(view, nullptr, this, nullptr);
}

} // namespace phonometrica
