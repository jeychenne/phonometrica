/***********************************************************************************************************************
 *                                                                                                                     *
 * Copyright (C) 2019-2026 Julien Eychenne                                                                             *
 *                                                                                                                     *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0. If a copy of the MPL was not   *
 * distributed with this file, You can obtain one at https://mozilla.org/MPL/2.0/.                                     *
 *                                                                                                                     *
 * Created: 22/03/2026                                                                                                 *
 *                                                                                                                     *
 * Purpose: Container widget that sits in the QTabWidget and manages one or more Views.                                *
 *          Supports splitting (via QSplitter) to show multiple views side by side.                                    *
 *          Each view is wrapped in a "slot" widget containing an optional ViewHeader.                                 *
 *                                                                                                                     *
 ***********************************************************************************************************************/

#ifndef PHONOMETRICA_VIEW_PANEL_HPP
#define PHONOMETRICA_VIEW_PANEL_HPP

#include <QWidget>
#include <QSplitter>
#include <phon/gui/view.hpp>

namespace phonometrica {

class ViewHeader;

class ViewPanel : public QWidget
{
	Q_OBJECT

public:

	// Creates a panel containing a single view.
	explicit ViewPanel(View *view, QWidget *parent = nullptr);

	~ViewPanel() override = default;

	// ── View access ────────────────────────────────────

	// The first view added to the panel (defines the tab label).
	View *primaryView() const;

	// The view that currently has keyboard focus (or primary if none focused).
	View *activeView() const;

	// All views in the panel, in order.
	QList<View *> views() const;

	// Number of views.
	int viewCount() const;

	// ── Splitting ──────────────────────────────────────

	// Add a view to the panel (placed to the right of existing views).
	void addView(View *view, Qt::Orientation orientation = Qt::Horizontal);

	// Remove a view from the panel and delete it.
	// If this was the last view, the panel emits lastViewClosed().
	void removeView(View *view);

	// Remove a view from the panel without deleting it; the caller takes ownership.
	// Returns the detached view, or nullptr if not found.
	View *detachView(View *view);

	bool isSplit() const;

	// ── Aggregate queries ──────────────────────────────

	// Tab label: uses the primary view's label.
	QString label() const;

	// True if any contained view has unsaved modifications.
	bool isModified() const;

	// Save all modified views. Returns true if all succeeded.
	bool saveAll();

signals:

	// Emitted when any contained view's title changes (so the tab can update).
	void titleChanged(const QString &title);

	// Emitted when a view is detached so MainWindow can create a new tab for it.
	void viewDetached(View *view);

	// Emitted when the last view is removed from the panel.
	void lastViewClosed();

protected:

	bool eventFilter(QObject *watched, QEvent *event) override;

private:

	// Create a slot widget (QWidget with VBoxLayout: ViewHeader + View).
	QWidget *createSlot(View *view, bool isPrimary);

	// Extract the View from a slot widget.
	static View *viewFromSlot(QWidget *slot);

	// Extract the ViewHeader from a slot widget.
	static ViewHeader *headerFromSlot(QWidget *slot);

	// Find the slot widget that contains a given view.
	QWidget *slotForView(View *view) const;

	// Show or hide all headers based on whether the panel is split.
	void updateHeaders();

	void trackView(View *view);
	void untrackView(View *view);

	QSplitter *m_splitter = nullptr;
	View *m_active_view = nullptr;
};

} // namespace phonometrica

#endif // PHONOMETRICA_VIEW_PANEL_HPP
