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

#include <QMenu>
#include <QMessageBox>
#include <QInputDialog>
#include <phon/gui/file_dialog.hpp>
#include <QVBoxLayout>
#include <QShortcut>
#include <optional>
#include <phon/gui/annotation_view.hpp>
#include <phon/gui/annotation_commands.hpp>
#include <phon/gui/new_layer_dialog.hpp>
#include <phon/gui/layer_visibility_dialog.hpp>
#include <phon/application/settings.hpp>
#include <phon/runtime/regex.hpp>

namespace phonometrica {

AnnotationView::AnnotationView(const Handle<Annotation> &annot, QWidget *parent) :
	SoundView(annot->sound(), Deferred, parent), m_annot(annot)
{
	m_annot->open();
	// Initialize layer visibility: all layers visible by default.
	m_layer_visibility.assign(m_annot->size() + 1, true);
	m_layer_visibility[0] = false; // index 0 is unused
	// Now that the AnnotationView vtable is fully constructed, the virtual
	// hooks addAnnotationLayers() and addAnnotationToolbar() will resolve
	// to our overrides.
	initialize();
}

QString AnnotationView::label() const
{
	auto lbl = m_annot->label();
	auto qlbl = tabLabel(QString::fromUtf8(lbl.data(), (int) lbl.size()));
	if (m_annot->content_modified())
		qlbl += QStringLiteral(" *");
	return qlbl;
}

String AnnotationView::path() const
{
	return m_annot->path();
}

bool AnnotationView::isModified() const
{
	return m_annot->content_modified();
}

bool AnnotationView::save()
{
	if (!m_annot->content_modified())
		return true;

	if (!m_annot->has_path())
	{
		auto path = getSaveFileName(this, tr("Save annotation..."),
			tr("Annotation (*.phon-annot)"));
		if (path.isEmpty())
			return false;
		if (!path.endsWith(".phon-annot"))
			path.append(".phon-annot");
		m_annot->set_path(path, true);
	}

	m_annot->save();
	emit titleChanged(label());
	return true;
}

void AnnotationView::discardChanges()
{
	if (m_annot->has_path())
		m_annot->reload();
	else
		m_annot->discard_changes();
}

void AnnotationView::escape()
{
	// If the search bar is visible, close it first.
	if (m_searchbar && m_searchbar->isVisible())
	{
		m_searchbar->hide();
		return;
	}

	// If an inline event editor is open, cancel it.
	for (auto *layer : m_layers)
	{
		if (layer->isEditing())
		{
			layer->cancelEdit();
			return;
		}
	}
}


// ─────────────────────────────────────────────────
//  SoundView hooks
// ─────────────────────────────────────────────────

void AnnotationView::addAnnotationToolbar(QToolBar *toolbar)
{
	toolbar->addSeparator();

	// Save button.
	auto *save_action = toolbar->addAction(QIcon(":/icons/save.svg"), tr("Save annotation"));
	connect(save_action, &QAction::triggered, this, &AnnotationView::onSaveAnnotation);
	toolbar->addSeparator();

	// Layer management menu.
	auto *layer_menu = new QMenu(this);
	layer_menu->addAction(tr("Add new layer..."), this, &AnnotationView::onCreateLayer);
	layer_menu->addAction(tr("Remove selected layer"), this, &AnnotationView::onRemoveLayer);
	layer_menu->addSeparator();
	layer_menu->addAction(tr("Duplicate selected layer"), this, &AnnotationView::onDuplicateLayer);
	layer_menu->addAction(tr("Rename selected layer"), this, &AnnotationView::onRenameLayer);
	layer_menu->addAction(tr("Clear selected layer"), this, &AnnotationView::onClearLayer);

	auto *layer_action = new QAction(QIcon(":/icons/layers.svg"), tr("Manage layers"), this);
	layer_action->setMenu(layer_menu);
	toolbar->addAction(layer_action);
	if (auto *lb = qobject_cast<QToolButton *>(toolbar->widgetForAction(layer_action)))
		lb->setPopupMode(QToolButton::InstantPopup);

	// Anchor sharing toggle (off by default to avoid surprising new users).
	m_link_action = toolbar->addAction(QIcon(":/icons/unlink.svg"), tr("Share/unshare anchors"));
	m_link_action->setCheckable(true);
	m_link_action->setChecked(true);
	connect(m_link_action, &QAction::toggled, this, &AnnotationView::onToggleAnchorSharing);

	// Add anchor.
	m_add_anchor_action = toolbar->addAction(QIcon(":/icons/anchor.svg"), tr("Add anchors"));
	m_add_anchor_action->setCheckable(true);
	connect(m_add_anchor_action, &QAction::toggled, this, &AnnotationView::onToggleAddAnchor);

	// Remove anchor.
	m_remove_anchor_action = toolbar->addAction(QIcon(":/icons/eraser.svg"), tr("Remove anchors"));
	m_remove_anchor_action->setCheckable(true);
	connect(m_remove_anchor_action, &QAction::toggled, this, &AnnotationView::onToggleRemoveAnchor);

	toolbar->addSeparator();
}

void AnnotationView::addAnnotationLayers(QLayout *layout)
{
	auto *vbox = qobject_cast<QVBoxLayout *>(layout);
	if (!vbox)
		return;

	// Store a reference to the layout so we can insert/remove layers dynamically.
	m_layer_layout = vbox;

	intptr_t count = m_annot->size();
	for (intptr_t i = 1; i <= count; i++)
	{
		auto *widget = createLayerWidget(i);
		vbox->addWidget(widget);
	}

	// Find/replace bar (hidden by default).
	m_searchbar = new SearchBar(this);
	populateSearchBarLayers();
	vbox->addWidget(m_searchbar);
	connect(m_searchbar, &SearchBar::findRequested, this, &AnnotationView::onAnnotFind);
	connect(m_searchbar, &SearchBar::replaceRequested, this, &AnnotationView::onAnnotReplace);
	connect(m_searchbar, &SearchBar::replaceAllRequested, this, &AnnotationView::onAnnotReplaceAll);

	// Keyboard shortcuts for find/replace (the main window's action shortcuts
	// are display-only; each view must provide its own).
	auto *findShortcut = new QShortcut(QKeySequence::Find, this);
	findShortcut->setContext(Qt::WidgetWithChildrenShortcut);
	connect(findShortcut, &QShortcut::activated, this, &AnnotationView::find);

	auto *replaceShortcut = new QShortcut(QKeySequence(tr("Ctrl+H")), this);
	replaceShortcut->setContext(Qt::WidgetWithChildrenShortcut);
	connect(replaceShortcut, &QShortcut::activated, this, &AnnotationView::replace);
}

void AnnotationView::addDisplayMenuEntries(QMenu *menu)
{
	menu->addSeparator();
	menu->addAction(tr("Select visible layers..."), this, &AnnotationView::onShowHideLayers);
}

void AnnotationView::onAnchorRequested(double time)
{
	if (time <= 0 || time >= m_annot->sound()->duration())
		return;

	// Find the focused layer; if none, use the first visible one.
	int layer = focusedLayerIndex();
	if (layer < 0)
	{
		for (auto *w : m_layers)
		{
			if (w->isVisible())
			{
				layer = (int)w->layerIndex();
				break;
			}
		}
	}

	if (layer > 0)
	{
		for (auto *w : m_layers)
		{
			if ((int)w->layerIndex() == layer)
			{
				w->createAnchor(time, false);
				break;
			}
		}
	}
}


// ─────────────────────────────────────────────────
//  Layer widget creation and signal wiring
// ─────────────────────────────────────────────────

LayerWidget *AnnotationView::createLayerWidget(intptr_t layer_index)
{
	auto *layer = new LayerWidget(timeModel(), m_annot, layer_index, this);

	// Insert into m_layers at the correct position to keep them ordered by layer index.
	auto it = m_layers.begin();
	while (it != m_layers.end() && (*it)->layerIndex() < layer_index)
		++it;
	m_layers.insert(it, layer);

	connect(layer, &LayerWidget::gotFocus, this, &AnnotationView::onLayerGotFocus);
	connect(layer, &LayerWidget::focusEvent, this, &AnnotationView::onFocusEvent);
	connect(layer, &LayerWidget::eventSelected, this, &AnnotationView::onEventSelected);
	connect(layer, &LayerWidget::modified, this, &AnnotationView::onLayerModified);
	connect(layer, &LayerWidget::anchorMoving, this, &AnnotationView::onAnchorMoving);
	connect(layer, &LayerWidget::anchorHasMoved, this, &AnnotationView::onAnchorHasMoved);
	connect(layer, &LayerWidget::anchorAdded, this, &AnnotationView::onAnchorAdded);
	connect(layer, &LayerWidget::anchorRemoved, this, &AnnotationView::onAnchorRemoved);
	connect(layer, &LayerWidget::anchorMoved, this, &AnnotationView::onAnchorMoved);
	connect(layer, &LayerWidget::anchorSelected, this, &AnnotationView::onAnchorSelected);
	connect(layer, &LayerWidget::editingSharedAnchor, this, &AnnotationView::onEditingSharedAnchor);
	connect(layer, &LayerWidget::temporaryAnchor, this, &AnnotationView::onTemporaryAnchor);

	// Undo recording signals.
	connect(layer, &LayerWidget::anchorCreationDone, this, &AnnotationView::onAnchorCreationDone);
	connect(layer, &LayerWidget::anchorRemovalDone, this, &AnnotationView::onAnchorRemovalDone);
	connect(layer, &LayerWidget::anchorMoveDone, this, &AnnotationView::onAnchorMoveDone);
	connect(layer, &LayerWidget::eventTextEdited, this, &AnnotationView::onEventTextEdited);

	// Show the layer number in the Y-axis.
	yAxis()->addLayer(layer);

	return layer;
}


// ─────────────────────────────────────────────────
//  Focus management
// ─────────────────────────────────────────────────

void AnnotationView::onLayerGotFocus(intptr_t layer_index)
{
	for (auto *layer : m_layers)
	{
		if (layer->layerIndex() != layer_index)
			layer->setFocused(false);
	}
}

void AnnotationView::onFocusEvent(intptr_t target_layer, double time, bool forward)
{
	// Find the next visible layer in the given direction.
	intptr_t idx = target_layer;
	intptr_t limit = forward ? m_annot->size() : 1;
	int step = forward ? 1 : -1;

	while (idx >= 1 && idx <= m_annot->size())
	{
		// Find the widget for this layer.
		for (auto *w : m_layers)
		{
			if (w->layerIndex() == idx && w->isVisible())
			{
				// Unfocus all other layers.
				for (auto *other : m_layers)
				{
					if (other != w)
						other->setFocused(false);
				}
				w->setEventFocus(time);
				return;
			}
		}
		if (idx == limit)
			break;
		idx += step;
	}
}


// ─────────────────────────────────────────────────
//  Layer management — public methods (used by commands)
// ─────────────────────────────────────────────────

bool AnnotationView::addLayer(intptr_t index, const String &name, bool has_instants)
{
	try
	{
		m_annot->create_layer(index, name, has_instants);
	}
	catch (std::exception &e)
	{
		QMessageBox::critical(this, tr("Cannot add layer"), e.what());
		return false;
	}

	auto *widget = createLayerWidget(index);
	int insert_pos = layerLayoutOffset() + (int)(index - 1);
	m_layer_layout->insertWidget(insert_pos, widget);

	// Grow the visibility vector to accommodate the new layer.
	if (index >= (intptr_t)m_layer_visibility.size())
		m_layer_visibility.resize(index + 1, true);
	else
		m_layer_visibility.insert(m_layer_visibility.begin() + index, true);

	widget->show();
	onLayerModified();
	return true;
}

void AnnotationView::removeLayer(intptr_t index)
{
	m_annot->remove_layer(index);

	// Find and remove the widget.
	for (auto it = m_layers.begin(); it != m_layers.end(); ++it)
	{
		if ((*it)->layerIndex() == index)
		{
			m_layer_layout->removeWidget(*it);
			delete *it;
			m_layers.erase(it);
			break;
		}
	}

	// Shrink the visibility vector.
	if (index < (intptr_t)m_layer_visibility.size())
		m_layer_visibility.erase(m_layer_visibility.begin() + index);

	onLayerModified();
}


// ─────────────────────────────────────────────────
//  Layer management — slot handlers
// ─────────────────────────────────────────────────

void AnnotationView::onCreateLayer()
{
	NewLayerDialog dlg(this, m_annot->size());

	if (dlg.exec() == QDialog::Accepted)
	{
		String name = dlg.layerName();
		intptr_t index = dlg.layerIndex();
		bool has_instants = dlg.hasInstants();

		auto cmd = std::make_unique<AddLayerCommand>(this, index, name, has_instants);
		if (!submit(std::move(cmd)))
			QMessageBox::warning(this, tr("Error"), tr("Could not create layer"));
	}
}

void AnnotationView::onRemoveLayer()
{
	int index = focusedLayerIndex();
	if (index < 0)
	{
		QMessageBox::warning(this, tr("Cannot remove layer"), tr("No selected layer!"));
		return;
	}

	auto cmd = std::make_unique<RemoveLayerCommand>(this, index);
	if (!submit(std::move(cmd)))
		QMessageBox::warning(this, tr("Error"), tr("Could not remove layer"));
}

void AnnotationView::onDuplicateLayer()
{
	int index = focusedLayerIndex();
	if (index < 0)
	{
		QMessageBox::warning(this, tr("Cannot duplicate layer"), tr("No selected layer!"));
		return;
	}

	bool ok;
	int new_size = (int)m_annot->size() + 1;
	int new_index = QInputDialog::getInt(this, tr("Duplicate layer..."), tr("Position:"),
		new_size, 1, new_size, 1, &ok);

	if (ok)
	{
		m_annot->duplicate_layer(index, new_index);
		auto *widget = createLayerWidget(new_index);
		int insert_pos = layerLayoutOffset() + new_index - 1;
		m_layer_layout->insertWidget(insert_pos, widget);

		// Grow visibility.
		if (new_index >= (intptr_t)m_layer_visibility.size())
			m_layer_visibility.resize(new_index + 1, true);
		else
			m_layer_visibility.insert(m_layer_visibility.begin() + new_index, true);

		widget->show();
		onLayerModified();
	}
}

void AnnotationView::onRenameLayer()
{
	int index = focusedLayerIndex();
	if (index < 0)
	{
		QMessageBox::warning(this, tr("Cannot rename layer"), tr("No selected layer!"));
		return;
	}

	bool ok;
	auto current = m_annot->get_layer_label(index);
	QString name = QInputDialog::getText(this, tr("Rename layer..."), tr("New name:"),
		QLineEdit::Normal, current, &ok);

	if (ok)
	{
		auto cmd = std::make_unique<RenameLayerCommand>(this, index,
			current, String(name.toUtf8().constData()));
		submit(std::move(cmd));
	}
}

void AnnotationView::onClearLayer()
{
	int index = focusedLayerIndex();
	if (index < 0)
	{
		QMessageBox::warning(this, tr("Cannot clear layer"), tr("No selected layer!"));
		return;
	}

	m_annot->clear_layer(index);
	// Find and repaint the widget.
	for (auto *w : m_layers)
	{
		if (w->layerIndex() == index)
		{
			w->update();
			break;
		}
	}
	onLayerModified();
}

void AnnotationView::onShowHideLayers()
{
	LayerVisibilityDialog dlg(this, m_annot, m_layer_visibility);

	if (dlg.exec() == QDialog::Accepted)
	{
		m_layer_visibility = dlg.visibility();
		applyLayerVisibility();
	}
}

void AnnotationView::applyLayerVisibility()
{
	for (auto *w : m_layers)
	{
		intptr_t idx = w->layerIndex();
		bool visible = (idx < (intptr_t)m_layer_visibility.size()) ? m_layer_visibility[idx] : true;
		w->setVisible(visible);
	}
}

int AnnotationView::focusedLayerIndex() const
{
	for (auto *layer : m_layers)
	{
		if (layer->isFocused())
			return (int)layer->layerIndex();
	}
	return -1;
}

int AnnotationView::layerLayoutOffset() const
{
	// Find the index of the first LayerWidget in the shared plot layout.
	if (!m_layer_layout)
		return 0;
	for (int i = 0; i < m_layer_layout->count(); i++)
	{
		auto *item = m_layer_layout->itemAt(i);
		if (item && item->widget() && qobject_cast<LayerWidget *>(item->widget()))
			return i;
	}
	// No layer found yet — return the end of the layout.
	return m_layer_layout->count();
}


// ─────────────────────────────────────────────────
//  Anchor mode toggles
// ─────────────────────────────────────────────────

void AnnotationView::onToggleAddAnchor(bool checked)
{
	if (checked)
	{
		m_remove_anchor_action->setChecked(false);
	}
	else
	{
		clearGhostAnchors();
	}

	for (auto *layer : m_layers)
		layer->setAddingAnchor(checked);
}

void AnnotationView::onToggleRemoveAnchor(bool checked)
{
	if (checked)
	{
		m_add_anchor_action->setChecked(false);
		clearGhostAnchors();
	}

	for (auto *layer : m_layers)
		layer->setRemovingAnchor(checked);
}

void AnnotationView::onToggleAnchorSharing(bool checked)
{
	// checked == true means "unshared" (broken link icon).
	if (checked)
		m_link_action->setIcon(QIcon(":/icons/unlink.svg"));
	else
		m_link_action->setIcon(QIcon(":/icons/link.svg"));

	for (auto *layer : m_layers)
		layer->setAnchorSharing(!checked);
}


// ─────────────────────────────────────────────────
//  Anchor sharing
// ─────────────────────────────────────────────────

void AnnotationView::onAnchorAdded(intptr_t layer_index, double time)
{
	bool instants = false;
	for (auto *w : m_layers)
	{
		if (w->layerIndex() == layer_index)
		{
			instants = w->hasInstants();
			break;
		}
	}

	for (auto *w : m_layers)
	{
		if (w->layerIndex() != layer_index && w->isVisible() && w->hasInstants() == instants)
			w->createAnchor(time, true);
	}
}

void AnnotationView::onAnchorRemoved(intptr_t layer_index, double time)
{
	bool instants = false;
	for (auto *w : m_layers)
	{
		if (w->layerIndex() == layer_index)
		{
			instants = w->hasInstants();
			break;
		}
	}

	for (auto *w : m_layers)
	{
		if (w->layerIndex() != layer_index && w->isVisible() && w->hasInstants() == instants)
			w->removeAnchor(time, true);
	}
}

void AnnotationView::onAnchorMoved(intptr_t layer_index, double from, double to)
{
	bool instants = false;
	for (auto *w : m_layers)
	{
		if (w->layerIndex() == layer_index)
		{
			instants = w->hasInstants();
			break;
		}
	}

	for (auto *w : m_layers)
	{
		if (w->layerIndex() != layer_index && w->isVisible() && w->hasInstants() == instants)
			w->moveAnchor(from, to);
	}
}

void AnnotationView::onAnchorMoving(intptr_t layer_index, double time)
{
	for (auto *w : m_layers)
	{
		if (w->layerIndex() != layer_index)
			w->followMovingAnchor(time);
	}
}

void AnnotationView::onAnchorHasMoved(intptr_t layer_index)
{
	for (auto *w : m_layers)
	{
		if (w->layerIndex() != layer_index)
			w->clearMovingAnchor();
	}
}

void AnnotationView::onAnchorSelected(intptr_t layer_index, double time)
{
	if (time < 0)
	{
		clearGhostAnchors();
		return;
	}

	for (auto *w : m_layers)
	{
		if (w->layerIndex() != layer_index)
			w->setGhostAnchorTime(time);
	}

	// Show a visual reference on all sound plots by setting a point selection.
	timeModel()->setSelection(time, time);
}

void AnnotationView::onEditingSharedAnchor(intptr_t layer_index, double time)
{
	for (auto *w : m_layers)
	{
		if (w->layerIndex() != layer_index)
		{
			if (time >= 0)
				w->hideAnchor(time);
			else
				w->hideAnchor(-1);
		}
	}
}

void AnnotationView::onTemporaryAnchor(intptr_t layer_index, double time)
{
	// Find the source layer's type.
	bool source_instants = false;
	for (auto *w : m_layers)
	{
		if (w->layerIndex() == layer_index)
		{
			source_instants = w->hasInstants();
			break;
		}
	}

	for (auto *w : m_layers)
	{
		if (w->layerIndex() != layer_index && w->hasInstants() == source_instants)
			w->setEditAnchorTime(time);
	}

	// Show a tracking cursor on all sound widgets so the user can see
	// exactly where the anchor would land relative to the waveform/spectrogram.
	if (time >= 0)
		timeModel()->setCursor(time);
	else
		timeModel()->clearCursor();
}

void AnnotationView::clearGhostAnchors()
{
	for (auto *w : m_layers)
	{
		w->clearGhostAnchor();
		w->update();
	}

	// Clear the visual reference on sound plots.
	timeModel()->clearSelection();
}


// ─────────────────────────────────────────────────
//  Event selection
// ─────────────────────────────────────────────────

void AnnotationView::onEventSelected(double start, double end)
{
	timeModel()->setSelection(start, end);

	// Show layer/event info in the status bar.
	for (auto *w : m_layers)
	{
		if (w->isFocused())
		{
			double mid = (start + end) / 2;
			intptr_t ev_idx = m_annot->get_event_at_time(w->layerIndex(), mid);
			if (ev_idx > 0)
				setAnnotationStatus(tr("Layer %1 / Event %2").arg(w->layerIndex()).arg(ev_idx));
			break;
		}
	}
}

void AnnotationView::openSelection(intptr_t layer, double from, double to)
{
	auto *model = timeModel();
	double dur = model->duration();
	double match_dur = to - from;
	double mid = from + match_dur / 2;

	// Ensure the window is at least 1 second (or the whole file if shorter).
	static constexpr double MIN_WINDOW = 1.0;
	double win = std::max(match_dur * 1.5, MIN_WINDOW);
	if (win > dur) win = dur;

	double win_start = mid - win / 2;
	double win_end = mid + win / 2;

	// Clamp to file boundaries.
	if (win_start < 0) {
		win_end -= win_start; // shift right
		win_start = 0;
	}
	if (win_end > dur) {
		win_start -= (win_end - dur); // shift left
		win_end = dur;
	}
	if (win_start < 0) win_start = 0;

	model->setViewport(win_start, win_end);
	model->setSelection(from, to);
	onFocusEvent(layer, mid, false);
}


// ─────────────────────────────────────────────────
//  Save
// ─────────────────────────────────────────────────

void AnnotationView::onSaveAnnotation()
{
	save();
}

void AnnotationView::onLayerModified()
{
	emit modificationChanged(true);
	emit titleChanged(label());
}


// ─────────────────────────────────────────────────
//  Find / Replace
// ─────────────────────────────────────────────────

void AnnotationView::populateSearchBarLayers()
{
	QStringList names;
	intptr_t count = m_annot->size();
	for (intptr_t i = 1; i <= count; i++)
	{
		auto lbl = m_annot->get_layer_label(i);
		QString display = QString::number(i);
		if (!lbl.empty())
			display += QStringLiteral(": ") + QString::fromUtf8(lbl.data(), (int)lbl.size());
		names.append(display);
	}
	m_searchbar->setLayerChoices(names);
}

void AnnotationView::seedSearchCursor()
{
	m_search_layer = 0;
	m_search_event = 0;

	// If an event is currently selected on the focused layer, start
	// searching from that position so the next Find advances past it.
	int layer = focusedLayerIndex();
	if (layer < 1) return;

	auto *model = timeModel();
	if (!model->hasSelection()) return;

	double mid = (model->selectionStart() + model->selectionEnd()) / 2;
	intptr_t ev = m_annot->get_event_at_time(layer, mid);
	if (ev > 0)
	{
		m_search_layer = layer;
		m_search_event = ev;
	}
}

void AnnotationView::find()
{
	m_searchbar->setSearch();
	seedSearchCursor();
}

void AnnotationView::replace()
{
	m_searchbar->setSearchAndReplace();
	seedSearchCursor();
}

void AnnotationView::onAnnotFind()
{
	auto needle_q = m_searchbar->searchText();
	if (needle_q.isEmpty()) return;

	String needle(needle_q.toUtf8().constData());
	bool use_regex = m_searchbar->usesRegex();
	bool case_sensitive = m_searchbar->isCaseSensitive();
	int target_layer = m_searchbar->selectedLayer(); // 0 = all

	// Build the regex if requested.
	std::optional<Regex> re;
	if (use_regex)
	{
		try {
			int flags = case_sensitive ? Regex::None : Regex::Caseless;
			re.emplace(needle, flags);
		}
		catch (std::exception &e) {
			QMessageBox::warning(this, tr("Find"),
				tr("Invalid regular expression: %1").arg(e.what()));
			return;
		}
	}

	intptr_t nlayers = m_annot->size();

	// Build the list of layers to search.
	intptr_t first_layer = (target_layer > 0) ? target_layer : 1;
	intptr_t last_layer = (target_layer > 0) ? target_layer : nlayers;

	// Starting position for continuation. On the first call (or after reset)
	// m_search_layer == 0, so we start from the beginning.
	intptr_t start_l = (m_search_layer > 0) ? m_search_layer : first_layer;
	intptr_t start_e = m_search_event; // 0 means "start from event 1"

	// We wrap around once: go from (start_l, start_e) to (start_l, start_e - 1).
	bool wrapped = false;
	intptr_t l = start_l;

	while (true)
	{
		if (l >= first_layer && l <= last_layer)
		{
			auto &layer = m_annot->layers()[l];
			intptr_t nevents = layer.count();
			intptr_t e_begin = (l == start_l && !wrapped) ? (start_e + 1) : 1;

			for (intptr_t e = e_begin; e <= nevents; e++)
			{
				// If we've wrapped and returned to the start position, stop.
				if (wrapped && l == start_l && e > start_e)
					goto not_found;

				auto &ev = layer.events[e];

				bool match;
				if (use_regex)
					match = re->match(ev.text);
				else
					match = case_sensitive ? ev.text.contains(needle) : ev.text.icontains(needle);

				if (match)
				{
					// Update search cursor for next invocation.
					m_search_layer = l;
					m_search_event = e;

					// Scroll to the event if it's outside the viewport.
					auto *model = timeModel();
					double ws = model->windowStart();
					double we = model->windowEnd();
					if (ev.start < ws || ev.end > we || ev.start > we || ev.end < ws)
					{
						openSelection(l, ev.start, ev.end);
					}
					else
					{
						model->setSelection(ev.start, ev.end);
						onFocusEvent(l, ev.center(), false);
					}
					return;
				}
			}
		}

		// Advance to the next layer.
		l++;
		if (l > last_layer)
		{
			if (wrapped)
				goto not_found;
			wrapped = true;
			l = first_layer;
		}
		if (wrapped && l > start_l)
			goto not_found;
	}

not_found:
	QMessageBox::information(this, tr("Find"), tr("Text not found."));
	m_search_layer = 0;
	m_search_event = 0;
}

void AnnotationView::onAnnotReplace()
{
	auto needle_q = m_searchbar->searchText();
	auto replacement_q = m_searchbar->replacementText();
	if (needle_q.isEmpty()) return;

	String needle(needle_q.toUtf8().constData());
	String replacement(replacement_q.toUtf8().constData());
	bool use_regex = m_searchbar->usesRegex();
	bool case_sensitive = m_searchbar->isCaseSensitive();

	// If we have a current match, replace it first.
	if (m_search_layer > 0 && m_search_event > 0)
	{
		auto &layer = m_annot->layers()[m_search_layer];
		if (m_search_event <= layer.count())
		{
			auto &ev = layer.events[m_search_event];
			String new_text(ev.text);

			if (use_regex)
			{
				try {
					int flags = case_sensitive ? Regex::None : Regex::Caseless;
					Regex re(needle, flags);
					new_text.replace(re, replacement, 1);
				}
				catch (std::exception &) {
					// Invalid regex — skip replacement silently, onAnnotFind will report.
				}
			}
			else
			{
				if (case_sensitive)
				{
					new_text.replace(needle, replacement, 1);
				}
				else
				{
					String::const_iterator match_end;
					auto it = new_text.ifind(needle, new_text.begin(), &match_end);
					if (it != new_text.end())
						new_text.replace(it, match_end, replacement);
				}
			}

			if (new_text != ev.text)
			{
				m_annot->set_event_text(m_search_layer, m_search_event, new_text);
				// Refresh the affected layer widget.
				for (auto *w : m_layers) {
					if (w->layerIndex() == m_search_layer) {
						w->update();
						break;
					}
				}
				onLayerModified();
			}
		}
	}

	// Advance to the next match.
	onAnnotFind();
}

void AnnotationView::onAnnotReplaceAll()
{
	auto needle_q = m_searchbar->searchText();
	auto replacement_q = m_searchbar->replacementText();
	if (needle_q.isEmpty()) return;

	String needle(needle_q.toUtf8().constData());
	String replacement(replacement_q.toUtf8().constData());
	bool use_regex = m_searchbar->usesRegex();
	bool case_sensitive = m_searchbar->isCaseSensitive();
	int target_layer = m_searchbar->selectedLayer();

	std::optional<Regex> re;
	if (use_regex)
	{
		try {
			int flags = case_sensitive ? Regex::None : Regex::Caseless;
			re.emplace(needle, flags);
		}
		catch (std::exception &e) {
			QMessageBox::warning(this, tr("Replace"),
				tr("Invalid regular expression: %1").arg(e.what()));
			return;
		}
	}

	intptr_t nlayers = m_annot->size();
	intptr_t first_layer = (target_layer > 0) ? target_layer : 1;
	intptr_t last_layer = (target_layer > 0) ? target_layer : nlayers;

	int count = 0;

	for (intptr_t l = first_layer; l <= last_layer; l++)
	{
		auto &layer = m_annot->layers()[l];
		intptr_t nevents = layer.count();

		for (intptr_t e = 1; e <= nevents; e++)
		{
			auto &ev = layer.events[e];
			String new_text(ev.text);

			if (use_regex)
			{
				// String::replace(Regex&, ...) only replaces the first match.
				// Loop until no more matches.
				while (re->match(new_text))
					new_text.replace(*re, replacement);
			}
			else if (case_sensitive)
				new_text.replace(needle, replacement); // ntimes = -1 → all
			else
			{
				// Case-insensitive plain text: use ifind + positional replace.
				String::const_iterator match_end;
				auto it = new_text.ifind(needle, new_text.begin(), &match_end);
				while (it != new_text.end())
				{
					new_text.replace(it, match_end, replacement);
					// Restart search after the replacement.
					auto offset = std::distance(new_text.cbegin(), it) + replacement.size();
					auto restart = new_text.begin();
					std::advance(restart, offset);
					it = new_text.ifind(needle, restart, &match_end);
				}
			}

			if (new_text != ev.text)
			{
				m_annot->set_event_text(l, e, new_text);
				count++;
			}
		}
	}

	if (count == 0)
	{
		QMessageBox::information(this, tr("Replace"), tr("Text not found."));
	}
	else
	{
		// Refresh all affected layer widgets.
		for (auto *w : m_layers) {
			intptr_t li = w->layerIndex();
			if (li >= first_layer && li <= last_layer)
				w->update();
		}
		onLayerModified();
		QMessageBox::information(this, tr("Replace"),
			tr("%1 replacement(s) made.").arg(count));
	}

	m_search_layer = 0;
	m_search_event = 0;
}

// ─────────────────────────────────────────────────
//  Undo/redo helper methods
// ─────────────────────────────────────────────────

LayerWidget *AnnotationView::findLayerWidget(intptr_t layer_index) const
{
	for (auto *w : m_layers)
	{
		if (w->layerIndex() == layer_index)
			return w;
	}
	return nullptr;
}

void AnnotationView::refreshLayer(intptr_t layer_index)
{
	if (auto *w = findLayerWidget(layer_index))
		w->invalidateCache();
}

void AnnotationView::doAddAnchor(intptr_t layer_index, double time)
{
	m_annot->add_anchor(layer_index, time);
	refreshLayer(layer_index);
	onLayerModified();
}

void AnnotationView::doRemoveAnchor(intptr_t layer_index, double time)
{
	m_annot->remove_anchor(layer_index, time);
	refreshLayer(layer_index);
	onLayerModified();
}

void AnnotationView::doMoveAnchor(intptr_t layer_index, double from, double to)
{
	auto &layer = m_annot->mutable_layer(layer_index);

	for (intptr_t i = 1; i <= layer.count(); i++)
	{
		auto &ev = layer.events[i];
		if (ev.start == from && from != 0)
		{
			ev.start = to;
			if (i > 1)
				layer.events[i - 1].end = to;
			break;
		}
		else if (ev.end == from)
		{
			ev.end = to;
			if (i < layer.count())
				layer.events[i + 1].start = to;
			break;
		}
	}

	m_annot->set_graph_modified(true);
	refreshLayer(layer_index);
	onLayerModified();
}

void AnnotationView::doSetEventText(intptr_t layer_index, intptr_t event_1based, const String &text)
{
	m_annot->set_event_text(layer_index, event_1based, text);
	refreshLayer(layer_index);
	onLayerModified();
}

void AnnotationView::doSetEventText(intptr_t layer_index, double time, const String &text)
{
	// Find the event at the given time and set its text.
	auto &layer = m_annot->mutable_layer(layer_index);
	for (intptr_t i = 1; i <= layer.count(); i++)
	{
		if (layer.events[i].start == time)
		{
			layer.events[i].text = text;
			break;
		}
	}
	m_annot->set_graph_modified(true);
	refreshLayer(layer_index);
	onLayerModified();
}

void AnnotationView::doRestoreTextsAroundAnchor(intptr_t layer_index, double time,
                                                 const String &left_text, const String &right_text)
{
	// After add_anchor split an interval at `time`, restore the original texts
	// to both halves.
	auto &layer = m_annot->mutable_layer(layer_index);
	for (intptr_t i = 1; i <= layer.count(); i++)
	{
		if (layer.events[i].end == time)
		{
			layer.events[i].text = left_text;
			if (i < layer.count() && layer.events[i + 1].start == time)
				layer.events[i + 1].text = right_text;
			break;
		}
	}
	m_annot->set_graph_modified(true);
	refreshLayer(layer_index);
}

void AnnotationView::doSetLayerLabel(intptr_t layer_index, const String &name)
{
	m_annot->set_layer_label(layer_index, name);
	onLayerModified();
}


// ─────────────────────────────────────────────────
//  Undo recording slots
// ─────────────────────────────────────────────────

void AnnotationView::onAnchorCreationDone(intptr_t layer_index, double time)
{
	auto cmd = std::make_unique<AddAnchorCommand>(this, layer_index, time);
	record(std::move(cmd));
}

void AnnotationView::onAnchorRemovalDone(intptr_t layer_index, double time,
                                          bool is_instant, String left_text, String right_text)
{
	auto cmd = std::make_unique<RemoveAnchorCommand>(this, layer_index, time,
		is_instant, std::move(left_text), std::move(right_text));
	record(std::move(cmd));
}

void AnnotationView::onAnchorMoveDone(intptr_t layer_index, double from, double to)
{
	auto cmd = std::make_unique<MoveAnchorCommand>(this, layer_index, from, to);
	record(std::move(cmd));
}

void AnnotationView::onEventTextEdited(intptr_t layer_index, intptr_t event_1based,
                                        String old_text, String new_text)
{
	auto cmd = std::make_unique<EditEventTextCommand>(this, layer_index, event_1based,
		std::move(old_text), std::move(new_text));
	record(std::move(cmd));
}

} // namespace phonometrica
