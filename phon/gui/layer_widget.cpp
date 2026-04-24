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
 * Purpose: see header.                                                                                                *
 *                                                                                                                     *
 ***********************************************************************************************************************/

#include <cmath>
#include <algorithm>
#include <QPainter>
#include <QMouseEvent>
#include <QKeyEvent>
#include <QTextEdit>
#include <QPixmap>
#include <QMessageBox>
#include <phon/gui/layer_widget.hpp>

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
static const QColor CANDIDATE_ANCHOR_COLOR(0, 0, 204, 140);  // translucent blue
static const QColor SELECTED_ANCHOR_COLOR(255, 0, 0);        // red highlight for anchor to be deleted
static const QColor SEPARATOR_COLOR(192, 192, 192);

// Lazily-built eraser cursor for remove-anchor mode.
static const QCursor &eraserCursor()
{
	static QCursor cursor;
	static bool built = false;
	if (!built)
	{
		QPixmap pix(":/icons/eraser.svg");
		if (!pix.isNull())
		{
			pix = pix.scaled(24, 24, Qt::KeepAspectRatio, Qt::SmoothTransformation);
			cursor = QCursor(pix, 4, 20); // hot spot near bottom-left
		}
		else
		{
			cursor = QCursor(Qt::ForbiddenCursor);
		}
		built = true;
	}
	return cursor;
}


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
		if (m_editing_event >= 0) cancelEdit();
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
		{
			clearSelectedEvent();
			m_selected_anchor_time = -1;
		}
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
	if (value)
		setCursor(eraserCursor());
	else
	{
		setCursor(Qt::ArrowCursor);
		clearEditAnchor();
	}
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
		// If an anchor already exists at this exact time, nothing to do.
		// This can happen when anchor sharing propagates back to a layer that
		// already owns the anchor (e.g. clicking a ghost anchor on a peer layer
		// causes onAnchorAdded → createAnchor(time, true) on the source layer).
		auto &layer = m_annot->layers()[m_layer_index];
		for (intptr_t i = 1; i <= layer.count(); i++)
		{
			if (layer.events[i].start == time || layer.events[i].end == time)
				return;
		}

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

		// Record for undo (user-initiated adds only).
		if (!silent)
			emit anchorCreationDone(m_layer_index, time);
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

		// Capture state before removal for undo.
		bool is_instant = hasInstants();
		String saved_left, saved_right;
		auto &layer = m_annot->layers()[m_layer_index];
		if (is_instant)
		{
			// Find the instant at target and save its text.
			for (intptr_t i = 1; i <= layer.count(); i++)
			{
				if (layer.events[i].start == target)
				{
					saved_left = layer.events[i].text;
					break;
				}
			}
		}
		else
		{
			// Find the two adjacent intervals sharing the boundary at target.
			for (intptr_t i = 1; i <= layer.count(); i++)
			{
				if (layer.events[i].end == target)
				{
					saved_left = layer.events[i].text;
					if (i < layer.count())
						saved_right = layer.events[i + 1].text;
					break;
				}
			}
		}

		bool removed = m_annot->remove_anchor(m_layer_index, target);

		if (removed)
		{
			m_event_cache.clear();
			update();
			if (m_sharing_anchors && !silent)
				emit anchorRemoved(m_layer_index, target);
			emit modified();

			// Record for undo (user-initiated removes only).
			if (!silent)
				emit anchorRemovalDone(m_layer_index, target, is_instant,
				                       std::move(saved_left), std::move(saved_right));
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

double LayerWidget::candidateAnchorTime() const
{
	if (!m_model->hasPointSelection() || m_adding_anchor || m_removing_anchor)
		return -1;

	double t = m_model->selectionStart();
	if (t <= 0 || t >= m_duration)
		return -1;

	// No candidate if an anchor already exists there.
	for (auto *ev : m_event_cache)
	{
		if (anchorHasCursor(ev->start, t) || anchorHasCursor(ev->end, t))
			return -1;
	}

	return t;
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
	beginEditing(event_index);
}


// ─────────────────────────────────────────────────
//  Inline event editor
// ─────────────────────────────────────────────────

void LayerWidget::beginEditing(int event_index)
{
	if (event_index < 0 || event_index >= (int)m_event_cache.size())
		return;

	// Commit any ongoing edit first.
	if (m_editing_event >= 0)
		commitEdit();

	m_editing_event = event_index;
	auto *ev = m_event_cache[event_index];

	// Create the editor on first use.
	if (!m_inline_edit)
	{
		m_inline_edit = new QTextEdit(this);
		m_inline_edit->setAcceptRichText(false);
		m_inline_edit->setTabChangesFocus(false);
		m_inline_edit->installEventFilter(this);
		m_inline_edit->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
		m_inline_edit->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
		m_inline_edit->setWordWrapMode(QTextOption::WrapAtWordBoundaryOrAnywhere);

		// Use a slightly larger font for comfortable editing.
		QFont f = font();
		f.setPointSizeF(f.pointSizeF() * 1.1);
		m_inline_edit->setFont(f);

		m_inline_edit->setStyleSheet(
			QStringLiteral("QTextEdit { background: palette(base); "
			               "border: 2px solid palette(highlight); border-radius: 3px; "
			               "padding: 4px; }"));
	}

	// Compute geometry: overlay the event area, extending upward if needed.
	// For intervals, match the event width; for instants, use a reasonable width.
	int ww = width();
	int x1, ew;

	if (ev->is_interval())
	{
		x1 = qMax(0, (int)timeToX(ev->start));
		int x2 = qMin(ww, (int)timeToX(ev->end));
		ew = qMax(120, x2 - x1);
		// Don't overflow the widget boundary.
		if (x1 + ew > ww) x1 = qMax(0, ww - ew);
	}
	else
	{
		int cx = (int)timeToX(ev->start);
		ew = 160;
		x1 = qMax(0, cx - ew / 2);
		if (x1 + ew > ww) x1 = qMax(0, ww - ew);
	}

	// Height: use the full layer height plus extra space above.
	// The editor sits over the layer, growing upward if the text is long.
	int layer_h = height();
	int editor_h = qMax(layer_h, 60);

	// Position: bottom-aligned to the layer widget so it overlaps the event.
	int y = layer_h - editor_h;

	m_inline_edit->setGeometry(x1, y, ew, editor_h);
	m_inline_edit->setPlainText(QString(ev->text));
	m_inline_edit->selectAll();
	m_inline_edit->show();
	m_inline_edit->raise();
	m_inline_edit->setFocus();
}

void LayerWidget::commitEdit()
{
	if (m_editing_event < 0 || !m_inline_edit)
		return;

	QString new_text = m_inline_edit->toPlainText();
	m_inline_edit->hide();

	// Check bounds — the event cache may have been invalidated.
	if (m_editing_event < (int)m_event_cache.size())
	{
		auto *ev = m_event_cache[m_editing_event];
		if (new_text != QString(ev->text))
		{
			// Save old text for undo.
			String old_text = ev->text;
			intptr_t event_1based = m_annot->get_event_index(m_layer_index, ev->start);
			if (event_1based == 0)
			{
				m_editing_event = -1;
				setFocus();
				update();
				return;
			}

			// event_index in cache is 0-based; annotation API is 1-based.
			m_annot->set_event_text(m_layer_index, event_1based, new_text);
			m_event_cache.clear();
			emit modified();

			// Record for undo.
			emit eventTextEdited(m_layer_index, event_1based,
			                     std::move(old_text),
			                     String(new_text.toUtf8().constData()));
		}
	}

	m_editing_event = -1;
	setFocus();
	update();
}

void LayerWidget::cancelEdit()
{
	if (m_inline_edit)
		m_inline_edit->hide();
	m_editing_event = -1;
	setFocus();
	update();
}

void LayerWidget::advanceEdit()
{
	int next = m_editing_event + 1;
	commitEdit();

	// Refresh cache in case commitEdit invalidated it.
	if (needsCacheRefresh())
		updateEventCache();

	if (next < (int)m_event_cache.size())
	{
		setSelectedEvent(next);
		beginEditing(next);
	}
	else
	{
		// At the last event — scroll forward and try again.
		focusNextEvent();
		if (m_selected_event >= 0)
			beginEditing(m_selected_event);
	}
}

bool LayerWidget::eventFilter(QObject *obj, QEvent *event)
{
	if (obj == m_inline_edit)
	{
		// Claim keys we handle so that QAction shortcuts on the MainWindow
		// (e.g. Escape, Ctrl+Return) don't steal them from the inline editor.
		if (event->type() == QEvent::ShortcutOverride)
		{
			auto *ke = static_cast<QKeyEvent *>(event);
			if (ke->key() == Qt::Key_Escape
				|| ke->key() == Qt::Key_Return
				|| ke->key() == Qt::Key_Enter
				|| ke->key() == Qt::Key_Tab)
			{
				ke->accept();
				return true;
			}
		}

		if (event->type() == QEvent::KeyPress)
		{
			auto *ke = static_cast<QKeyEvent *>(event);

			if (ke->key() == Qt::Key_Return || ke->key() == Qt::Key_Enter)
			{
				if (ke->modifiers() & Qt::ShiftModifier)
				{
					// Shift+Enter: insert a newline in the text.
					return false; // let QTextEdit handle it
				}
				// Plain Enter: commit the edit.
				commitEdit();
				return true;
			}
			if (ke->key() == Qt::Key_Escape)
			{
				cancelEdit();
				return true;
			}
			if (ke->key() == Qt::Key_Tab)
			{
				advanceEdit();
				return true;
			}
		}
	}
	return QWidget::eventFilter(obj, event);
}

void LayerWidget::resizeEvent(QResizeEvent *event)
{
	QWidget::resizeEvent(event);

	// Reposition the inline editor if it's visible.
	if (m_inline_edit && m_inline_edit->isVisible() && m_editing_event >= 0)
	{
		if (needsCacheRefresh())
			updateEventCache();

		if (m_editing_event < (int)m_event_cache.size())
		{
			auto *ev = m_event_cache[m_editing_event];
			int ww = width();
			int x1, ew;

			if (ev->is_interval())
			{
				x1 = qMax(0, (int)timeToX(ev->start));
				int x2 = qMin(ww, (int)timeToX(ev->end));
				ew = qMax(120, x2 - x1);
				if (x1 + ew > ww) x1 = qMax(0, ww - ew);
			}
			else
			{
				int cx = (int)timeToX(ev->start);
				ew = 160;
				x1 = qMax(0, cx - ew / 2);
				if (x1 + ew > ww) x1 = qMax(0, ww - ew);
			}

			int layer_h = height();
			int editor_h = qMax(layer_h, 60);
			int y = layer_h - editor_h;
			m_inline_edit->setGeometry(x1, y, ew, editor_h);
		}
		else
		{
			cancelEdit();
		}
	}
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

	// ── Draw selected anchor highlight ──────────

	if (m_focused && m_selected_anchor_time >= 0)
	{
		QPen selAnchorPen(SELECTED_ANCHOR_COLOR, 3);
		painter.setPen(selAnchorPen);
		double x = timeToX(m_selected_anchor_time);
		painter.drawLine(QPointF(x, 0), QPointF(x, h));
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
	// In add-anchor mode: orange dashed line (you can click to place an anchor here in sync).
	// In normal mode: blue dashed line (shows where another layer has an anchor; you could add one here).

	if (m_ghost_anchor_time >= 0)
	{
		QColor ghostColor = m_adding_anchor ? GHOST_ANCHOR_COLOR : CANDIDATE_ANCHOR_COLOR;
		QPen ghostPen(ghostColor, 1, Qt::DotLine);
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

	// ── Draw anchor candidate from point selection ──
	// When the user clicks a time point in a sound widget, show a dashed
	// candidate line on all layers unless an anchor already exists there.

	double candidate = candidateAnchorTime();
	if (candidate >= 0)
	{
		QPen candidatePen(CANDIDATE_ANCHOR_COLOR, 1, Qt::DashLine);
		painter.setPen(candidatePen);
		double x = timeToX(candidate);
		painter.drawLine(QPointF(x, 0), QPointF(x, h));
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
	// Commit any active inline editor before handling the click.
	if (m_editing_event >= 0)
		commitEdit();

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
		// Check if clicking on a candidate anchor (dashed line from point selection).
		double candidate = candidateAnchorTime();
		if (candidate >= 0 && anchorHasCursor(candidate, t))
		{
			createAnchor(candidate, false);
			return;
		}

		// Check if clicking on a ghost anchor (shown when another layer's anchor is selected).
		if (m_ghost_anchor_time >= 0 && anchorHasCursor(m_ghost_anchor_time, t))
		{
			createAnchor(m_ghost_anchor_time, false);
			return;
		}

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
	// Remember whether we were mid-drag so we can clear the shared tracking
	// cursor only in that case (otherwise a plain click on an anchor would
	// wipe out a cursor set by hovering over a sound widget).
	const bool was_dragging_anchor = m_dragging_anchor;
	m_dragging_anchor = false;
	setCursor(Qt::ArrowCursor);

	if (was_dragging_anchor)
		m_model->clearCursor();

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

			// Record for undo.
			emit anchorMoveDone(m_layer_index, from, m_dropped_anchor_time);
		}
		else
		{
			QMessageBox::critical(this, tr("Error"), tr("Cannot move anchor"));
		}

		m_selected_anchor_time = -1;
		m_resizing_event = nullptr;
		m_dragged_anchor_time = -1;
		m_dropped_anchor_time = -1;
		emit anchorHasMoved(m_layer_index);
		update();
	}
	else if (m_dragged_anchor_time >= 0 && m_dropped_anchor_time < 0)
	{
		// Click on an anchor without dragging → select the anchor.
		m_selected_anchor_time = m_dragged_anchor_time;
		m_dragged_anchor_time = -1;
		// Notify other layers so they show candidate anchors at the same time.
		emit anchorSelected(m_layer_index, m_selected_anchor_time);
		update();
	}
	else
	{
		// Click elsewhere → clear selected anchor, and tell other layers to drop their ghosts.
		m_selected_anchor_time = -1;
		emit anchorSelected(m_layer_index, -1);
	}

	// Give focus to this layer (unless the inline editor is active).
	if (m_editing_event < 0)
	{
		if (e->modifiers() == Qt::NoModifier)
		{
			m_focused = true;
			emit gotFocus(m_layer_index);
		}
		setFocus();
	}

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
		// Drive the shared tracking cursor so all sound plots (waveform,
		// spectrogram, pitch, intensity) display a vertical line at the
		// prospective boundary position while the anchor is being dragged.
		// TimeModel::setCursor clamps to [0, duration] internally.
		m_model->setCursor(t);
		update();
	}
	else if (m_adding_anchor)
	{
		setEditAnchorTime(t);
		update();
	}
	else if (m_removing_anchor)
	{
		// Keep eraser cursor; highlight nearest anchor if close.
		if (needsCacheRefresh())
			updateEventCache();
		double closest = findClosestAnchorTime(t);
		setCursor(closest >= 0 ? eraserCursor() : Qt::ArrowCursor);
	}
	else
	{
		if (needsCacheRefresh())
			updateEventCache();
		trackAnchor(t);

		// Show pointing hand when hovering over a candidate anchor.
		if (m_dragged_anchor_time < 0)
		{
			double candidate = candidateAnchorTime();
			if (candidate >= 0 && anchorHasCursor(candidate, t))
				setCursor(Qt::PointingHandCursor);
			else if (m_ghost_anchor_time >= 0 && anchorHasCursor(m_ghost_anchor_time, t))
				setCursor(Qt::PointingHandCursor);
		}
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
			beginEditing(i);
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
		{
			// Open inline editor on the selected event.
			beginEditing(m_selected_event);
		}
		else
		{
			// Create anchor from candidate if available.
			double candidate = candidateAnchorTime();
			if (candidate >= 0)
				createAnchor(candidate, false);
		}
		break;
	case Qt::Key_Delete:
		if (m_selected_anchor_time >= 0)
		{
			// Remove the selected anchor.
			double t = m_selected_anchor_time;
			m_selected_anchor_time = -1;
			removeAnchor(t, false);
		}
		else if (m_selected_event >= 0 && m_selected_event < (int)m_event_cache.size())
		{
			// Clear the selected event's label.
			auto *ev = m_event_cache[m_selected_event];
			if (!ev->text.empty())
			{
				m_annot->set_event_text(m_layer_index, m_selected_event + 1, String());
				m_event_cache.clear();
				emit modified();
				update();
			}
		}
		break;
	case Qt::Key_Backspace:
		if (m_selected_anchor_time >= 0)
		{
			double t = m_selected_anchor_time;
			m_selected_anchor_time = -1;
			removeAnchor(t, false);
		}
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
