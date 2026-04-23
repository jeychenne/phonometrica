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
#include <QEvent>
#include <QMessageBox>
#include <phon/gui/view_panel.hpp>
#include <phon/gui/view_header.hpp>

namespace phonometrica {

ViewPanel::ViewPanel(View *view, QWidget *parent) :
	QWidget(parent)
{
	auto *layout = new QVBoxLayout(this);
	layout->setContentsMargins(0, 0, 0, 0);
	layout->setSpacing(0);

	m_splitter = new QSplitter(Qt::Horizontal, this);
	m_splitter->setChildrenCollapsible(false);
	layout->addWidget(m_splitter);

	auto *slot = createSlot(view, true);
	m_splitter->addWidget(slot);
	trackView(view);
	m_active_view = view;

	updateHeaders(); // single view → headers hidden
}


// ─────────────────────────────────────────────────
//  View access
// ─────────────────────────────────────────────────

View *ViewPanel::primaryView() const
{
	if (m_splitter->count() == 0)
		return nullptr;
	return viewFromSlot(m_splitter->widget(0));
}

View *ViewPanel::activeView() const
{
	// If the tracked active view is still in the splitter, use it.
	if (m_active_view)
	{
		for (int i = 0; i < m_splitter->count(); i++)
		{
			if (viewFromSlot(m_splitter->widget(i)) == m_active_view)
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
		auto *v = viewFromSlot(m_splitter->widget(i));
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
	// For the first split, set the orientation.
	if (m_splitter->count() <= 1)
		m_splitter->setOrientation(orientation);

	auto *slot = createSlot(view, false);
	m_splitter->addWidget(slot);
	trackView(view);

	updateHeaders(); // now 2+ views → headers visible

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

	auto *slot = slotForView(view);
	if (!slot)
		return;

	untrackView(view);

	if (m_active_view == view)
		m_active_view = nullptr;

	// Remove the slot (which owns the view as a child) and delete both.
	slot->setParent(nullptr);
	slot->deleteLater();

	updateHeaders();

	if (m_splitter->count() == 0)
		emit lastViewClosed();
}

View *ViewPanel::detachView(View *view)
{
	if (!view)
		return nullptr;

	auto *slot = slotForView(view);
	if (!slot)
		return nullptr;

	untrackView(view);

	if (m_active_view == view)
		m_active_view = nullptr;

	// Reparent the view out of the slot so it doesn't get deleted with it.
	view->setParent(nullptr);

	// Delete the now-empty slot.
	slot->setParent(nullptr);
	slot->deleteLater();

	updateHeaders();

	if (m_splitter->count() == 0)
		emit lastViewClosed();

	return view;
}

bool ViewPanel::isSplit() const
{
	return m_splitter->count() > 1;
}

bool ViewPanel::closeSecondaryView()
{
	// No-op when not split — closing the last view is the caller's concern
	// (it also needs to dispose of the tab, which we don't control from here).
	if (!isSplit())
		return false;

	// The primary view (index 0) anchors the tab (supplies its label, owns the
	// query result for concordance tabs, etc.) and is never closed by this
	// path. Close the last-added view instead; with 3+ panes this unwinds the
	// split right-to-left, one press per pane.
	int last = m_splitter->count() - 1;
	auto *v = viewFromSlot(m_splitter->widget(last));
	if (!v)
		return false;

	return confirmAndRemoveView(v);
}

bool ViewPanel::confirmAndRemoveView(View *view)
{
	if (!view)
		return false;

	if (view->isModified())
	{
		auto answer = QMessageBox::question(this, tr("Unsaved changes"),
			tr("\"%1\" has unsaved changes. Save before closing?").arg(view->label()),
			QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel);
		if (answer == QMessageBox::Cancel)
			return false;
		if (answer == QMessageBox::Save)
		{
			if (!view->save())
				return false;
		}
		else
		{
			view->discardChanges();
		}
	}
	removeView(view);
	return true;
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
		auto *v = viewFromSlot(m_splitter->widget(i));
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
		auto *v = viewFromSlot(m_splitter->widget(i));
		if (v && v->isModified())
		{
			if (!v->save())
				ok = false;
		}
	}
	return ok;
}


// ─────────────────────────────────────────────────
//  Slot helpers
// ─────────────────────────────────────────────────

QWidget *ViewPanel::createSlot(View *view, bool isPrimary)
{
	auto *slot = new QWidget;
	auto *layout = new QVBoxLayout(slot);
	layout->setContentsMargins(0, 0, 0, 0);
	layout->setSpacing(0);

	auto *header = new ViewHeader(view, slot);
	header->setVisible(false); // shown only when panel is split

	// The primary view's header shows only the label (no detach or close).
	if (isPrimary)
	{
		header->setDetachVisible(false);
		header->setCloseVisible(false);
	}
	else
	{
		connect(header, &ViewHeader::detachRequested, this, [this](View *v) {
			if (detachView(v))
				emit viewDetached(v);
		});
		connect(header, &ViewHeader::closeRequested, this, [this](View *v) {
			confirmAndRemoveView(v);
		});
	}

	layout->addWidget(header);
	layout->addWidget(view, 1);

	return slot;
}

View *ViewPanel::viewFromSlot(QWidget *slot)
{
	if (!slot)
		return nullptr;

	// The View is the second widget in the slot's layout (after the header).
	auto *layout = slot->layout();
	if (!layout)
		return nullptr;

	for (int i = 0; i < layout->count(); i++)
	{
		auto *v = qobject_cast<View *>(layout->itemAt(i)->widget());
		if (v)
			return v;
	}
	return nullptr;
}

ViewHeader *ViewPanel::headerFromSlot(QWidget *slot)
{
	if (!slot)
		return nullptr;

	auto *layout = slot->layout();
	if (!layout)
		return nullptr;

	for (int i = 0; i < layout->count(); i++)
	{
		auto *h = qobject_cast<ViewHeader *>(layout->itemAt(i)->widget());
		if (h)
			return h;
	}
	return nullptr;
}

QWidget *ViewPanel::slotForView(View *view) const
{
	for (int i = 0; i < m_splitter->count(); i++)
	{
		if (viewFromSlot(m_splitter->widget(i)) == view)
			return m_splitter->widget(i);
	}
	return nullptr;
}

void ViewPanel::updateHeaders()
{
	bool split = m_splitter->count() > 1;

	for (int i = 0; i < m_splitter->count(); i++)
	{
		auto *header = headerFromSlot(m_splitter->widget(i));
		if (header)
		{
			header->setVisible(split);
			if (split)
				header->updateLabel();
		}
	}
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
