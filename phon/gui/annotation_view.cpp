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

#include <QMenu>
#include <QMessageBox>
#include <QInputDialog>
#include <QFileDialog>
#include <QVBoxLayout>
#include <phon/gui/annotation_view.hpp>
#include <phon/gui/new_layer_dialog.hpp>
#include <phon/application/settings.hpp>

namespace phonometrica {

AnnotationView::AnnotationView(const Handle<Annotation> &annot, QWidget *parent) :
	SoundView(annot->sound(), Deferred, parent), m_annot(annot)
{
	m_annot->open();
	// Now that the AnnotationView vtable is fully constructed, the virtual
	// hooks addAnnotationLayers() and addAnnotationToolbar() will resolve
	// to our overrides.
	initialize();
}

QString AnnotationView::label() const
{
	return m_annot->label();
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
		auto path = QFileDialog::getSaveFileName(this, tr("Save annotation..."), QString(),
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


// ─────────────────────────────────────────────────
//  SoundView hooks
// ─────────────────────────────────────────────────

void AnnotationView::addAnnotationToolbar(QToolBar *toolbar)
{
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
	layer_menu->addSeparator();
	layer_menu->addAction(tr("Select visible layers"), this, &AnnotationView::onShowHideLayers);

	auto *layer_button = new QToolButton(this);
	layer_button->setIcon(QIcon(":/icons/layers.svg"));
	layer_button->setToolTip(tr("Manage layers"));
	layer_button->setMenu(layer_menu);
	layer_button->setPopupMode(QToolButton::InstantPopup);
	toolbar->addWidget(layer_button);

	// Anchor sharing toggle.
	m_link_button = new QToolButton(this);
	m_link_button->setIcon(QIcon(":/icons/link.svg"));
	m_link_button->setCheckable(true);
	m_link_button->setChecked(false);
	m_link_button->setToolTip(tr("Share/unshare anchors"));
	toolbar->addWidget(m_link_button);
	connect(m_link_button, &QToolButton::toggled, this, &AnnotationView::onToggleAnchorSharing);

	// Add anchor.
	m_add_anchor_action = toolbar->addAction(QIcon(":/icons/anchor.svg"), tr("Add anchor"));
	m_add_anchor_action->setCheckable(true);
	connect(m_add_anchor_action, &QAction::toggled, this, &AnnotationView::onToggleAddAnchor);

	// Remove anchor.
	m_remove_anchor_action = toolbar->addAction(QIcon(":/icons/eraser.svg"), tr("Remove anchor"));
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
//  Layer management
// ─────────────────────────────────────────────────

void AnnotationView::onCreateLayer()
{
	NewLayerDialog dlg(this, m_annot->size());

	if (dlg.exec() == QDialog::Accepted)
	{
		String name = dlg.layerName();
		intptr_t index = dlg.layerIndex();
		bool has_instants = dlg.hasInstants();

		m_annot->create_layer(index, name, has_instants);
		auto *widget = createLayerWidget(index);
		// Find the position of the first existing layer in the layout,
		// then insert relative to that.
		int insert_pos = layerLayoutOffset() + (int)(index - 1);
		m_layer_layout->insertWidget(insert_pos, widget);
		widget->show();
		onLayerModified();
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

	onLayerModified();
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
		m_annot->set_layer_label(index, name);
		onLayerModified();
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
	// TODO: implement a dialog to show/hide individual layers.
	// For now, all layers are visible.
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
		m_link_button->setIcon(QIcon(":/icons/toggle-right.svg"));
	else
		m_link_button->setIcon(QIcon(":/icons/link.svg"));

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
	for (auto *w : m_layers)
	{
		if (w->layerIndex() != layer_index)
			w->setGhostAnchorTime(time);
	}
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
}

void AnnotationView::clearGhostAnchors()
{
	for (auto *w : m_layers)
	{
		w->clearGhostAnchor();
		w->update();
	}
}


// ─────────────────────────────────────────────────
//  Event selection
// ─────────────────────────────────────────────────

void AnnotationView::onEventSelected(double start, double end)
{
	timeModel()->setSelection(start, end);
}

void AnnotationView::openSelection(intptr_t layer, double from, double to)
{
	timeModel()->setViewport(from, to);
	double mid = from + (to - from) / 2;
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

} // namespace phonometrica
