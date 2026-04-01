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
 * Created: 25/03/2026                                                                                                 *
 *                                                                                                                     *
 * Purpose: Widget to display and edit a single annotation layer. Observes TimeModel for viewport, selection, cursor   *
 *          and playback. Supports interval and instant layers, anchor manipulation (add/remove/move/share), event      *
 *          selection and keyboard navigation, and inline event editing via a popup dialog.                             *
 *                                                                                                                     *
 ***********************************************************************************************************************/

#ifndef PHONOMETRICA_LAYER_WIDGET_HPP
#define PHONOMETRICA_LAYER_WIDGET_HPP

#include <vector>
#include <QWidget>
#include <phon/application/annotation.hpp>
#include <phon/gui/time_model.hpp>

namespace phonometrica {

class LayerWidget final : public QWidget
{
	Q_OBJECT

public:

	LayerWidget(TimeModel *model, const Handle<Annotation> &annot, intptr_t layer_index, QWidget *parent = nullptr);

	// 1-based layer index within the annotation.
	intptr_t layerIndex() const { return m_layer_index; }

	bool hasInstants() const;
	bool hasIntervals() const { return !hasInstants(); }

	// Focus management: only the focused layer highlights the selected event.
	bool isFocused() const { return m_focused; }
	void setFocused(bool focused);

	// Select the event at the given time and give focus to this layer.
	void setEventFocus(double time);

	// ── Anchor editing modes ─────────────────────────

	void setAddingAnchor(bool value);
	void setRemovingAnchor(bool value);

	bool isAddingAnchor() const { return m_adding_anchor; }
	bool isRemovingAnchor() const { return m_removing_anchor; }

	// ── Anchor sharing ───────────────────────────────

	void setAnchorSharing(bool shared);
	bool isSharingAnchors() const { return m_sharing_anchors; }

	// Create/remove/move an anchor on this layer. If silent is true, no signal is emitted
	// (used when the operation was initiated by another layer's signal).
	void createAnchor(double time, bool silent);
	bool removeAnchor(double time, bool silent);
	bool moveAnchor(double from, double to);

	// ── Ghost/temporary anchor display ───────────────

	void setGhostAnchorTime(double time);
	void clearGhostAnchor();
	void setEditAnchorTime(double time);
	void clearEditAnchor();

	// When another layer is dragging a shared anchor, show a tracking line here.
	void followMovingAnchor(double time);
	void clearMovingAnchor();

	// When a shared anchor is being edited on another layer, hide it here to avoid
	// painting it in the old position while the user drags.
	void hideAnchor(double time);

	// ── Inline event editing ────────────────────────

	bool isEditing() const { return m_editing_event >= 0; }
	void cancelEdit();

signals:

	// Emitted when this layer gains focus (so the view can unfocus others).
	void gotFocus(intptr_t layer_index);

	// Emitted to navigate to the previous/next layer.
	// forward == true means "go to the next layer" (Down arrow).
	void focusEvent(intptr_t target_layer, double time, bool forward);

	// Emitted when an event is selected (so the view can update the model's selection).
	void eventSelected(double start, double end);

	// Emitted when the annotation has been modified.
	void modified();

	// Emitted when an anchor is being dragged.
	void anchorMoving(intptr_t layer_index, double time);
	void anchorHasMoved(intptr_t layer_index);

	// Emitted when an anchor is added or removed (for cross-layer sharing).
	void anchorAdded(intptr_t layer_index, double time);
	void anchorRemoved(intptr_t layer_index, double time);
	void anchorMoved(intptr_t layer_index, double from, double to);

	// Emitted when an existing anchor is clicked in add-anchor mode (to show ghost anchors on other layers).
	void anchorSelected(intptr_t layer_index, double time);

	// Emitted when a shared anchor drag begins/ends on this layer (so other layers can hide/show theirs).
	void editingSharedAnchor(intptr_t layer_index, double time);

	// Emitted when the mouse moves during anchor editing, so other layers can show a temporary line.
	void temporaryAnchor(intptr_t layer_index, double time);

protected:

	void paintEvent(QPaintEvent *event) override;
	void mousePressEvent(QMouseEvent *event) override;
	void mouseReleaseEvent(QMouseEvent *event) override;
	void mouseMoveEvent(QMouseEvent *event) override;
	void mouseDoubleClickEvent(QMouseEvent *event) override;
	void keyPressEvent(QKeyEvent *event) override;
	void leaveEvent(QEvent *event) override;
	void resizeEvent(QResizeEvent *event) override;
	bool eventFilter(QObject *obj, QEvent *event) override;

private:

	// ── Coordinate mapping (same formulas as WaveformWidget) ──

	double timeToX(double t) const;
	double xToTime(double x) const;

	// ── Painting helpers ─────────────────────────────

	void drawAnchor(QPainter &painter, double time, bool is_instant);

	// ── Event cache ──────────────────────────────────

	void updateEventCache();
	bool needsCacheRefresh() const;

	// ── Hit testing ──────────────────────────────────

	// Returns true if the cursor is within ~4px of the given anchor time.
	bool anchorHasCursor(double anchor_time, double cursor_time) const;

	// Check whether the cursor is near the start or end anchor of an event.
	// If so, sets out_time and event_start_selected accordingly.
	bool eventHasCursor(const Event &event, double time, double *out_time);

	// Find the closest anchor time near the cursor, or -1 if none.
	double findClosestAnchorTime(double time) const;

	// Find the event containing or closest to the given time.
	const Event *findEvent(double time) const;

	// ── Candidate anchor (from point selection on a sound widget) ──

	// Returns the candidate time if there is a valid candidate anchor
	// (point selection, not in add/remove mode, no existing anchor there).
	// Returns -1 otherwise.
	double candidateAnchorTime() const;

	// ── Anchor tracking ──────────────────────────────

	void trackAnchor(double time);
	void setSelectedAnchor(const Event *event, double time, bool selected = true);

	// ── Event selection and navigation ───────────────

	void setSelectedEvent(int index);
	void clearSelectedEvent();
	void editEvent(int event_index);
	void focusPreviousEvent();
	void focusNextEvent();

	// ── Inline event editor ──────────────────────────

	void beginEditing(int event_index);
	void commitEdit();
	void advanceEdit(); // Commit current, open next event.

	// ── Helpers ───────────────────────────────────────

	double clipLeft(double time) const { return (std::max)(0.0, time); }
	double clipRight(double time) const { return (std::min)(m_duration, time); }
	double timeAtCursor(QMouseEvent *e) const;

	// ── Data ─────────────────────────────────────────

	TimeModel *m_model;
	Handle<Annotation> m_annot;
	intptr_t m_layer_index; // 1-based

	double m_duration; // Sound file duration.

	// Cached slice of events visible in the current viewport.
	std::vector<const Event *> m_event_cache;
	double m_cached_start = -1;
	double m_cached_end = -1;

	// Index into m_event_cache of the currently selected event, or -1.
	int m_selected_event = -1;

	// The event whose anchor is being dragged.
	const Event *m_resizing_event = nullptr;
	double m_dragged_anchor_time = -1;
	double m_dropped_anchor_time = -1;

	// When an anchor is moved on another layer, we track it with a green line.
	double m_moving_anchor_time = -1;

	// Temporary anchor shown under the cursor during add-anchor mode.
	double m_edit_anchor_time = -1;

	// Ghost anchor shown on layers that don't have a real anchor at the time point
	// where another layer has just added/clicked an anchor.
	double m_ghost_anchor_time = -1;

	// When a shared anchor is being dragged on another layer, hide it here.
	double m_hidden_anchor_time = -1;

	// Which edge of the selected event was clicked?
	bool m_event_start_selected = false;

	// Anchor explicitly selected by clicking on it (for Delete to remove).
	double m_selected_anchor_time = -1;

	bool m_dragging_anchor = false;
	bool m_focused = false;
	bool m_adding_anchor = false;
	bool m_removing_anchor = false;
	bool m_sharing_anchors = false;

	// ── Inline editor ────────────────────────────────

	class QTextEdit *m_inline_edit = nullptr;
	int m_editing_event = -1;  // index into m_event_cache, or -1
};

} // namespace phonometrica

#endif // PHONOMETRICA_LAYER_WIDGET_HPP
