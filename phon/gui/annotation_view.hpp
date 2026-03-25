/***********************************************************************************************************************
 *                                                                                                                     *
 * Copyright (C) 2019-2026 Julien Eychenne                                                                             *
 *                                                                                                                     *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0. If a copy of the MPL was not   *
 * distributed with this file, You can obtain one at https://mozilla.org/MPL/2.0/.                                     *
 *                                                                                                                     *
 * Created: 25/03/2026                                                                                                 *
 *                                                                                                                     *
 * Purpose: View for annotation files. Extends SoundView with annotation layers, layer management, and anchor          *
 *          operations. Each annotation layer is displayed as a LayerWidget below the sound plots.                     *
 *                                                                                                                     *
 ***********************************************************************************************************************/

#ifndef PHONOMETRICA_ANNOTATION_VIEW_HPP
#define PHONOMETRICA_ANNOTATION_VIEW_HPP

#include <vector>
#include <QAction>
#include <QToolButton>
#include <phon/gui/sound_view.hpp>
#include <phon/gui/layer_widget.hpp>
#include <phon/application/annotation.hpp>

namespace phonometrica {

class AnnotationView final : public SoundView
{
	Q_OBJECT

public:

	AnnotationView(const Handle<Annotation> &annot, QWidget *parent = nullptr);

	QString label() const override;
	String path() const override;
	QString helpAnchor() const override { return QStringLiteral("annotation"); }

	bool isModified() const override;
	bool save() override;

	Handle<Annotation> annotation() const { return m_annot; }

	// Open the view focused on a specific event.
	void openSelection(intptr_t layer, double from, double to);

protected:

	// SoundView hooks.
	void addAnnotationLayers(QLayout *layout) override;
	void addAnnotationToolbar(QToolBar *toolbar) override;

private slots:

	// Focus management.
	void onLayerGotFocus(intptr_t layer_index);
	void onFocusEvent(intptr_t target_layer, double time, bool forward);

	// Layer management actions.
	void onCreateLayer();
	void onRemoveLayer();
	void onDuplicateLayer();
	void onRenameLayer();
	void onClearLayer();
	void onShowHideLayers();

	// Anchor mode toggles.
	void onToggleAddAnchor(bool checked);
	void onToggleRemoveAnchor(bool checked);
	void onToggleAnchorSharing(bool checked);

	// Anchor sharing signals from layers.
	void onAnchorAdded(intptr_t layer_index, double time);
	void onAnchorRemoved(intptr_t layer_index, double time);
	void onAnchorMoved(intptr_t layer_index, double from, double to);
	void onAnchorMoving(intptr_t layer_index, double time);
	void onAnchorHasMoved(intptr_t layer_index);
	void onAnchorSelected(intptr_t layer_index, double time);
	void onEditingSharedAnchor(intptr_t layer_index, double time);
	void onTemporaryAnchor(intptr_t layer_index, double time);

	// Event selection from layer.
	void onEventSelected(double start, double end);

	// Save action.
	void onSaveAnnotation();

	// Modification tracking.
	void onLayerModified();

private:

	int focusedLayerIndex() const;
	int layerLayoutOffset() const;
	LayerWidget *createLayerWidget(intptr_t layer_index);
	void clearGhostAnchors();

	Handle<Annotation> m_annot;

	// Layer widgets in display order (1-based indexing matches annotation layers).
	std::vector<LayerWidget *> m_layers;

	// Layout that contains the layer widgets (stored so we can insert/remove dynamically).
	QVBoxLayout *m_layer_layout = nullptr;

	// Toolbar actions.
	QToolButton *m_link_button = nullptr;
	QAction *m_add_anchor_action = nullptr;
	QAction *m_remove_anchor_action = nullptr;
};

} // namespace phonometrica

#endif // PHONOMETRICA_ANNOTATION_VIEW_HPP
