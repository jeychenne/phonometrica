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

#include <cmath>
#include <algorithm>
#include <QPainter>
#include <QMouseEvent>
#include <QKeyEvent>
#include <QMessageBox>
#include <phon/gui/layer_widget.hpp>
#include <phon/gui/event_editor.hpp>

namespace phonometrica {

// ─────────────────────────────────────────────────
//  Colours
// ─────────────────────────────────────────────────

static const QColor ANCHOR_COLOR = QColor(Qt::blue).darker(130);
static const QColor SELECTED_EVENT_COLOR(255, 165, 0, 120);  // semi-transparent orange
static const QColor FOCUSED_BG(255, 255, 0, 20);             // very light yellow
static const QColor MOVING_ANCHOR_COLOR(Qt::green);
static const QColor GHOST_ANCHOR_COLOR("orange");
static const QColor TEMP_ANCHOR_COLOR(Qt::red);
static const QColor SEPARATOR_COLOR(192, 192, 192);


// ─────────────────────────────────────────────────
//  Construction
// ─────────────────────────────────────────────────

LayerWidget::LayerWidget(TimeModel *model, const Handle<Annotation> &annot, intptr_t layer_index, QWidget *parent) :
	QWidget(parent), m_model(model), m_annot(annot), m_layer_index(layer_index),
	m_duration(annot->sound()->duration())
{
	setFixedHeight(60);
	QWidget::setMouseTracking(true);
	setFocusPolicy(Qt::ClickFocus);
	setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);

	connect(m_model, &TimeModel::viewportChanged, this, [this](double, double) {
		update();
	});
	connect(m_model, &TimeModel::selectionChanged, this, [this](double, double) {
		update();
	});
	connect(m_model, &TimeModel::selectionCleared, this, [this]() {
		update();
	});
	connect(m_model, &TimeModel::playbackTimeChanged, this, [this](double) {
		update();
	});
	connect(m_model, &TimeModel::playbackCleared, this, [this]() {
		update();
	});
}


// ─────────────────────────────────────────────────
//  Public interface
// ─────────────────────────────────────────────────

bool LayerWidget::hasInstants() const
{
	return m_annot->layer_has_instants(m_layer_index);
}

void LayerWidget::setFocused(bool focused)
{
	if (m_focused != focused)
	{
		m_focused = focused;
		if (!focused)
			clearSelectedEvent();
		update();
	}
}

void LayerWidget::setEventFocus(double time)
{
	updateEventCache();
	for (int i = 0; i < (int)m_event_cache.size(); i++)
	{
		auto *ev = m_event_cache[i];
		if (ev->contains_time(time) || (ev->is_instant() && anchorHasCursor(ev->start, time)))
		{
			m_focused = true;
			setFocus();
			setSelectedEvent(i);
			return;
		}
	}
	// Time not found in any event — unfocus.
	setFocused(false);
}

void LayerWidget::setAddingAnchor(bool value)
{
	m_adding_anchor = value;
	if (!value)
		clearEditAnchor();
	update();
}

void LayerWidget::setRemovingAnchor(bool value)
{
	m_removing_anchor = value;
	if (!value)
		clearEditAnchor();
	update();
}

void LayerWidget::setAnchorSharing(bool shared)
{
	m_sharing_anchors = shared;
}

void LayerWidget::createAnchor(double time, bool silent)
{
	try
	{
		m_annot->add_anchor(m_layer_index, time);

		clearEditAnchor();
		clearGhostAnchor();
		m_resizing_event = nullptr;
		m_dragged_anchor_time = -1;
		m_dropped_anchor_time = -1;
		m_event_cache.clear();
		update();

		emit anchorSelected(m_layer_index, time);
		if (m_sharing_anchors && !silent)
			emit anchorAdded(m_layer_index, time);
		emit modified();
	}
	catch (std::exception &e)
	{
		QMessageBox::critical(this, tr("Cannot add anchor"), e.what());
	}
}

bool LayerWidget::removeAnchor(double time, bool silent)
{
	try
	{
		auto *ev = findEvent(time);
		if (!ev)
			return false;

		// Check if cursor is near the end anchor of the found event.
		double target = -1;
		if (anchorHasCursor(ev->end, time))
			target = ev->end;
		else if (ev->is_interval() && anchorHasCursor(ev->start, time))
			target = ev->start;

		if (target < 0)
			return false;

		bool removed = m_annot->remove_anchor(m_layer_index, target);

		if (removed)
		{
			m_event_cache.clear();
			update();
			if (m_sharing_anchors && !silent)
				emit anchorRemoved(m_layer_index, target);
			emit modified();
			return true;
		}
	}
	catch (std::exception &e)
	{
		QMessageBox::critical(this, tr("Cannot remove anchor"), e.what());
	}

	return false;
}

bool LayerWidget::moveAnchor(double from, double to)
{
	auto &layer = m_annot->mutable_layer(m_layer_index);

	for (intptr_t i = 1; i <= layer.count(); i++)
	{
		auto &ev = layer.events[i];
		if (ev.start == from || ev.end == from)
		{
			if (ev.start == from && from != 0)
			{
				ev.start = to;
				if (i > 1)
					layer.events[i - 1].end = to;
			}
			else if (ev.end == from && from != m_duration)
			{
				ev.end = to;
				if (i < layer.count())
					layer.events[i + 1].start = to;
			}

			m_annot->set_graph_modified(true);
			m_event_cache.clear();
			emit modified();
			return true;
		}
	}

	return false;
}

void LayerWidget::setGhostAnchorTime(double time)
{
	// Don't show a ghost if a real anchor already exists at this time.
	auto &layer = m_annot->layers()[m_layer_index];
	for (intptr_t i = 1; i <= layer.count(); i++)
	{
		if (layer.events[i].start == time || layer.events[i].end == time)
		{
			clearGhostAnchor();
			return;
		}
	}
	m_ghost_anchor_time = time;
	update();
}

void LayerWidget::clearGhostAnchor()
{
	m_ghost_anchor_time = -1;
	update();
}

void LayerWidget::setEditAnchorTime(double time)
{
	m_edit_anchor_time = time;
	update();
}

void LayerWidget::clearEditAnchor()
{
	m_edit_anchor_time = -1;
}

void LayerWidget::followMovingAnchor(double time)
{
	m_moving_anchor_time = time;
	update();
}

void LayerWidget::clearMovingAnchor()
{
	followMovingAnchor(-1);
}

void LayerWidget::hideAnchor(double time)
{
	auto &layer = m_annot->layers()[m_layer_index];
	m_hidden_anchor_time = -1;
	for (intptr_t i = 1; i <= layer.count(); i++)
	{
		if (layer.events[i].start == time || layer.events[i].end == time)
		{
			m_hidden_anchor_time = time;
			break;
		}
	}
}


// ─────────────────────────────────────────────────
//  Coordinate mapping
// ─────────────────────────────────────────────────

double LayerWidget::timeToX(double t) const
{
	auto start = m_model->windowStart();
	auto dur = m_model->windowDuration();
	if (dur <= 0) return 0;
	return (t - start) / dur * width();
}

double LayerWidget::xToTime(double x) const
{
	auto start = m_model->windowStart();
	auto dur = m_model->windowDuration();
	return start + x / width() * dur;
}

double LayerWidget::timeAtCursor(QMouseEvent *e) const
{
	return xToTime(e->position().x());
}


// ─────────────────────────────────────────────────
//  Event cache
// ─────────────────────────────────────────────────

void LayerWidget::updateEventCache()
{
	double start = m_model->windowStart();
	double end = m_model->windowEnd();

	auto slice = m_annot->get_slice(m_layer_index, start, end);
	m_event_cache.clear();
	m_event_cache.reserve(slice.size());
	for (auto &ev : slice)
		m_event_cache.push_back(&ev);

	m_cached_start = start;
	m_cached_end = end;
}

bool LayerWidget::needsCacheRefresh() const
{
	return m_event_cache.empty()
		|| m_cached_start != m_model->windowStart()
		|| m_cached_end != m_model->windowEnd();
}


// ─────────────────────────────────────────────────
//  Hit testing
// ─────────────────────────────────────────────────

bool LayerWidget::anchorHasCursor(double anchor_time, double cursor_time) const
{
	// ~4 pixels tolerance.
	double delta = m_model->windowDuration() * 4.0 / width();
	return (cursor_time >= anchor_time - delta) && (cursor_time <= anchor_time + delta);
}

bool LayerWidget::eventHasCursor(const Event &event, double time, double *out_time)
{
	if (anchorHasCursor(event.end, time))
	{
		*out_time = event.end;
		m_event_start_selected = false;
		return true;
	}
	if (event.is_interval() && anchorHasCursor(event.start, time))
	{
		*out_time = event.start;
		m_event_start_selected = true;
		return true;
	}
	*out_time = -1;
	return false;
}

double LayerWidget::findClosestAnchorTime(double time) const
{
	for (auto *ev : m_event_cache)
	{
		if (anchorHasCursor(ev->start, time))
			return ev->start;
		if (ev->is_interval() && anchorHasCursor(ev->end, time))
			return ev->end;
	}
	return -1;
}

const Event *LayerWidget::findEvent(double time) const
{
	if (hasInstants())
	{
		for (auto *ev : m_event_cache)
		{
			if (anchorHasCursor(ev->start, time))
				return ev;
		}
	}
	else
	{
		for (auto *ev : m_event_cache)
		{
			if (ev->contains_time(time))
				return ev;
		}
	}
	return nullptr;
}


// ─────────────────────────────────────────────────
//  Anchor tracking (cursor changes near anchors)
// ─────────────────────────────────────────────────

void LayerWidget::trackAnchor(double t)
{
	for (auto *ev : m_event_cache)
	{
		double target;
		if (eventHasCursor(*ev, t, &target))
		{
			setSelectedAnchor(ev, target);
			return;
		}
	}
	setSelectedAnchor(nullptr, -1, false);
}

void LayerWidget::setSelectedAnchor(const Event *event, double time, bool selected)
{
	if (selected && time != 0 && time != m_duration)
	{
		setCursor(Qt::PointingHandCursor);
		m_dragged_anchor_time = time;
		m_resizing_event = event;
	}
	else
	{
		setCursor(Qt::ArrowCursor);
		m_dragged_anchor_time = -1;
		m_resizing_event = nullptr;
	}
}


// ─────────────────────────────────────────────────
//  Event selection and navigation
// ─────────────────────────────────────────────────

void LayerWidget::setSelectedEvent(int index)
{
	if (index < 0 || index >= (int)m_event_cache.size())
		return;
	m_selected_event = index;
	auto *ev = m_event_cache[index];
	emit eventSelected(ev->start, ev->end);
	update();
}

void LayerWidget::clearSelectedEvent()
{
	m_selected_event = -1;
	update();
}

void LayerWidget::editEvent(int event_index)
{
	if (event_index < 0 || event_index >= (int)m_event_cache.size())
		return;

	auto *ev = m_event_cache[event_index];

	// Position the editor at the center of the event.
	double x1 = timeToX(ev->start);
	double x2 = timeToX(ev->end);
	int cx = int(x1 + (x2 - x1) / 2);
	QPoint pos(cx, 0);
	pos = mapToGlobal(pos);
	pos.setY(pos.y() + m_edit_y_shift);

	QString text = ev->text;
	EventEditor editor(text, pos, this);

	if (editor.exec() == QDialog::Accepted)
	{
		auto new_text = editor.text();
		if (new_text != text)
		{
			m_annot->set_event_text(m_layer_index, event_index + 1, new_text);
			// The event pointer may be invalidated; refresh cache.
			m_event_cache.clear();
			emit modified();
		}
	}
	int shift = editor.yShift();
	if (shift != 0)
		m_edit_y_shift = shift;

	update();
}

void LayerWidget::focusPreviousEvent()
{
	if (m_selected_event < 0)
		return;

	if (m_selected_event == 0)
	{
		// Need to scroll backward.
		auto *prev = m_annot->find_previous_event(m_layer_index, m_event_cache[0]->start);
		if (!prev)
			return;
		double duration = m_model->windowDuration();
		double delta = duration / 20; // 5%
		double new_end = clipRight(prev->end + delta);
		double new_start = clipLeft(new_end - duration);
		m_model->setViewport(new_start, new_end);
		updateEventCache();
		// Find the event we scrolled to.
		for (int i = 0; i < (int)m_event_cache.size(); i++)
		{
			if (m_event_cache[i]->start == prev->start && m_event_cache[i]->end == prev->end)
			{
				setSelectedEvent(i);
				return;
			}
		}
	}
	else
	{
		setSelectedEvent(m_selected_event - 1);
	}
}

void LayerWidget::focusNextEvent()
{
	if (m_selected_event < 0)
		return;

	if (m_selected_event >= (int)m_event_cache.size() - 1)
	{
		// Need to scroll forward.
		auto *last = m_event_cache.back();
		auto *next = m_annot->find_next_event(m_layer_index, last->end);
		if (!next)
			return;
		double duration = m_model->windowDuration();
		double delta = duration / 20; // 5%
		double new_start = clipLeft(next->start - delta);
		double new_end = clipRight(new_start + duration);
		m_model->setViewport(new_start, new_end);
		updateEventCache();
		// Find the event we scrolled to.
		for (int i = 0; i < (int)m_event_cache.size(); i++)
		{
			if (m_event_cache[i]->start == next->start && m_event_cache[i]->end == next->end)
			{
				setSelectedEvent(i);
				return;
			}
		}
	}
	else
	{
		setSelectedEvent(m_selected_event + 1);
	}
}


// ─────────────────────────────────────────────────
//  Painting
// ─────────────────────────────────────────────────

void LayerWidget::paintEvent(QPaintEvent *)
{
	QPainter painter(this);
	int w = width();
	int h = height();

	// Background.
	if (m_focused)
		painter.fillRect(rect(), FOCUSED_BG);
	else
		painter.fillRect(rect(), Qt::white);

	// Refresh event cache if needed.
	if (needsCacheRefresh())
		updateEventCache();

	// ── Draw events ──────────────────────────────

	QPen anchorPen(ANCHOR_COLOR, 3);
	QPen textPen(Qt::black);
	double last_time = -1.0;

	for (auto *ev : m_event_cache)
	{
		// Draw anchors.
		painter.setPen(anchorPen);

		if (ev->start != last_time)
		{
			drawAnchor(painter, ev->start, ev->is_instant());
			if (ev->is_interval())
				drawAnchor(painter, ev->end, false);
			last_time = ev->is_instant() ? ev->start : ev->end;
		}
		else
		{
			drawAnchor(painter, ev->end, false);
			last_time = ev->end;
		}

		// Draw event text.
		painter.setPen(textPen);
		QString label = ev->text;
		QRectF textRect;

		if (ev->is_instant())
		{
			auto x = (std::max)(0.0, timeToX(ev->start));
			textRect = QRectF(x - 20, 0, 40, h);
		}
		else
		{
			auto x1 = (std::max)(0.0, timeToX(ev->start));
			auto x2 = (std::min)(timeToX(ev->end), double(w));
			textRect = QRectF(x1 + 2, 0, x2 - x1 - 2, h);
		}

		painter.drawText(textRect, Qt::AlignVCenter | Qt::AlignHCenter | Qt::TextWordWrap, label);
	}

	// ── Draw selected event highlight ────────────

	if (m_focused && m_selected_event >= 0 && m_selected_event < (int)m_event_cache.size())
	{
		auto *ev = m_event_cache[m_selected_event];

		if (ev->is_instant())
		{
			QPen selPen(SELECTED_EVENT_COLOR, 2);
			painter.setPen(selPen);
			auto x = timeToX(ev->start);
			int third = h / 3;
			painter.drawLine(QPointF(x, 0), QPointF(x, third));
			painter.drawLine(QPointF(x, third * 2), QPointF(x, h));
		}
		else
		{
			auto x1 = timeToX(ev->start);
			auto x2 = timeToX(ev->end);
			painter.fillRect(QRectF(x1, 0, x2 - x1, h), SELECTED_EVENT_COLOR);
		}
	}

	// ── Draw moving anchor (from another layer) ──

	if (m_moving_anchor_time >= 0)
	{
		int lineWidth = (m_sharing_anchors && m_hidden_anchor_time >= 0) ? 3 : 1;
		QPen movPen(MOVING_ANCHOR_COLOR, lineWidth, Qt::DotLine);
		painter.setPen(movPen);
		auto x = timeToX(m_moving_anchor_time);
		painter.drawLine(QPointF(x, 0), QPointF(x, h));
	}

	// ── Draw ghost anchor ────────────────────────

	if (m_adding_anchor && m_ghost_anchor_time >= 0)
	{
		QPen ghostPen(GHOST_ANCHOR_COLOR, 1, Qt::DotLine);
		painter.setPen(ghostPen);
		drawAnchor(painter, m_ghost_anchor_time, hasInstants());
	}

	// ── Draw temporary anchor under cursor ───────

	if (m_adding_anchor && m_edit_anchor_time >= 0)
	{
		double closest = findClosestAnchorTime(m_edit_anchor_time);

		if (closest >= 0 || anchorHasCursor(m_ghost_anchor_time, m_edit_anchor_time))
		{
			setCursor(Qt::PointingHandCursor);
		}
		else
		{
			setCursor(Qt::ArrowCursor);
			QPen tempPen(TEMP_ANCHOR_COLOR, 3);
			painter.setPen(tempPen);
			drawAnchor(painter, m_edit_anchor_time, hasInstants());
		}
	}

	// ── Draw playback tick ───────────────────────

	if (m_model->isPlaying())
	{
		double x = timeToX(m_model->playbackTime());
		if (x >= 0 && x <= w)
		{
			painter.setPen(QPen(Qt::red, 1));
			painter.drawLine(QPointF(x, 0), QPointF(x, h));
		}
	}

	// ── Draw separator between layers ────────────

	if (m_layer_index < m_annot->size())
	{
		QPen sepPen(SEPARATOR_COLOR, 1);
		painter.setPen(sepPen);
		painter.drawLine(0, h - 1, w, h - 1);
	}
}

void LayerWidget::drawAnchor(QPainter &painter, double time, bool is_instant)
{
	int h = height();

	// If this anchor is being dragged, draw the drop target instead.
	if (m_dropped_anchor_time >= 0 && m_dragged_anchor_time == time)
	{
		auto x = timeToX(m_dropped_anchor_time);
		QPen dropPen(Qt::green, 3);
		painter.setPen(dropPen);
		painter.drawLine(QPointF(x, 0), QPointF(x, h));
		return;
	}

	// Don't draw hidden anchors (being edited on another shared layer).
	if (time == m_hidden_anchor_time)
		return;

	auto x = timeToX(time);

	if (is_instant)
	{
		int third = h / 3;
		painter.drawLine(QPointF(x, 0), QPointF(x, third));
		painter.drawLine(QPointF(x, third * 2), QPointF(x, h));
	}
	else
	{
		painter.drawLine(QPointF(x, 0), QPointF(x, h));
	}
}


// ─────────────────────────────────────────────────
//  Mouse interaction
// ─────────────────────────────────────────────────

void LayerWidget::mousePressEvent(QMouseEvent *e)
{
	auto t = timeAtCursor(e);

	if (m_adding_anchor)
	{
		if (m_ghost_anchor_time >= 0 && anchorHasCursor(m_ghost_anchor_time, t))
		{
			createAnchor(m_ghost_anchor_time, false);
		}
		else
		{
			double closest = findClosestAnchorTime(t);
			if (closest >= 0)
			{
				// Clicked on an existing anchor → show ghost on other layers.
				emit anchorSelected(m_layer_index, closest);
			}
			else
			{
				createAnchor(t, false);
			}
		}
	}
	else if (m_removing_anchor)
	{
		if (removeAnchor(t, false))
		{
			clearGhostAnchor();
			clearEditAnchor();
			m_resizing_event = nullptr;
			m_dragged_anchor_time = -1;
			m_dropped_anchor_time = -1;
		}
	}
	else
	{
		if (m_sharing_anchors)
		{
			double closest = findClosestAnchorTime(t);
			if (closest >= 0)
				emit editingSharedAnchor(m_layer_index, closest);
		}
		m_dragging_anchor = true;
	}
}

void LayerWidget::mouseReleaseEvent(QMouseEvent *e)
{
	m_dragging_anchor = false;
	setCursor(Qt::ArrowCursor);

	if (m_sharing_anchors)
		emit editingSharedAnchor(m_layer_index, -1);

	// Finalize anchor drag.
	if (m_dropped_anchor_time >= 0 && m_resizing_event)
	{
		double from = m_event_start_selected ? m_resizing_event->start : m_resizing_event->end;

		// Apply the move through the annotation layer.
		auto &layer = m_annot->mutable_layer(m_layer_index);
		bool ok = false;

		if (m_event_start_selected)
		{
			for (intptr_t i = 1; i <= layer.count(); i++)
			{
				if (layer.events[i].start == from)
				{
					layer.events[i].start = m_dropped_anchor_time;
					if (i > 1)
						layer.events[i - 1].end = m_dropped_anchor_time;
					ok = true;
					break;
				}
			}
		}
		else
		{
			for (intptr_t i = 1; i <= layer.count(); i++)
			{
				if (layer.events[i].end == from)
				{
					layer.events[i].end = m_dropped_anchor_time;
					if (i < layer.count())
						layer.events[i + 1].start = m_dropped_anchor_time;
					ok = true;
					break;
				}
			}
		}

		if (ok)
		{
			m_annot->set_graph_modified(true);
			m_event_cache.clear();
			if (m_sharing_anchors)
				emit anchorMoved(m_layer_index, from, m_dropped_anchor_time);
			emit modified();
		}
		else
		{
			QMessageBox::critical(this, tr("Error"), tr("Cannot move anchor"));
		}

		m_resizing_event = nullptr;
		m_dragged_anchor_time = -1;
		m_dropped_anchor_time = -1;
		emit anchorHasMoved(m_layer_index);
		update();
	}

	// Give focus to this layer.
	if (e->modifiers() == Qt::NoModifier)
	{
		m_focused = true;
		emit gotFocus(m_layer_index);
	}
	setFocus();

	// Select the event under the cursor.
	auto t = xToTime(e->position().x());
	updateEventCache();
	for (int i = 0; i < (int)m_event_cache.size(); i++)
	{
		auto *ev = m_event_cache[i];
		if (ev->is_interval() && ev->contains_time(t))
		{
			setSelectedEvent(i);
			break;
		}
		else if (ev->is_instant() && anchorHasCursor(ev->start, t))
		{
			setSelectedEvent(i);
			break;
		}
	}
}

void LayerWidget::mouseMoveEvent(QMouseEvent *e)
{
	auto t = timeAtCursor(e);

	if (m_dragging_anchor && m_dragged_anchor_time >= 0)
	{
		setCursor(Qt::SizeHorCursor);
		m_dropped_anchor_time = t;
		emit anchorMoving(m_layer_index, t);
		update();
	}
	else if (m_adding_anchor)
	{
		setEditAnchorTime(t);
		update();
	}
	else
	{
		setCursor(Qt::ArrowCursor);
		if (needsCacheRefresh())
			updateEventCache();
		trackAnchor(t);
	}

	// Update cursor on other layers if sharing.
	if (m_dragging_anchor || m_adding_anchor)
	{
		if (m_sharing_anchors)
			emit temporaryAnchor(m_layer_index, t);
	}
}

void LayerWidget::mouseDoubleClickEvent(QMouseEvent *e)
{
	auto t = timeAtCursor(e);
	if (needsCacheRefresh())
		updateEventCache();

	for (int i = 0; i < (int)m_event_cache.size(); i++)
	{
		auto *ev = m_event_cache[i];
		if ((ev->is_interval() && ev->contains_time(t)) ||
			(ev->is_instant() && anchorHasCursor(ev->start, t)))
		{
			editEvent(i);
			return;
		}
	}
}

void LayerWidget::keyPressEvent(QKeyEvent *e)
{
	switch (e->key())
	{
	case Qt::Key_Left:
		focusPreviousEvent();
		break;
	case Qt::Key_Right:
		focusNextEvent();
		break;
	case Qt::Key_Up:
		if (m_selected_event >= 0 && m_layer_index > 1)
		{
			auto *ev = m_event_cache[m_selected_event];
			emit focusEvent(m_layer_index - 1, ev->center(m_model->windowStart(), m_model->windowEnd()), false);
		}
		break;
	case Qt::Key_Down:
		if (m_selected_event >= 0 && m_layer_index < m_annot->size())
		{
			auto *ev = m_event_cache[m_selected_event];
			emit focusEvent(m_layer_index + 1, ev->center(m_model->windowStart(), m_model->windowEnd()), true);
		}
		break;
	case Qt::Key_Return:
	case Qt::Key_Enter:
		if (m_selected_event >= 0)
			editEvent(m_selected_event);
		break;
	default:
		QWidget::keyPressEvent(e);
		break;
	}
}

void LayerWidget::leaveEvent(QEvent *event)
{
	clearEditAnchor();
	if (m_sharing_anchors)
		emit temporaryAnchor(m_layer_index, -1);
	update();
	QWidget::leaveEvent(event);
}

} // namespace phonometrica
