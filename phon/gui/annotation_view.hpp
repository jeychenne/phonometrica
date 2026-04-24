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
#include <phon/gui/search_bar.hpp>
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
	void discardChanges() override;
	void escape() override;

	// Find/replace support.
	void find() override;
	void replace() override;
	bool supportsFind() const override { return true; }

	Handle<Annotation> annotation() const { return m_annot; }

	// Open the view focused on a specific event.
	void openSelection(intptr_t layer, double from, double to);

	// Public layer management methods used by undo/redo commands.
	// These do the actual widget creation/destruction and annotation mutation.
	bool addLayer(intptr_t index, const String &name, bool has_instants);
	void removeLayer(intptr_t index);

	// ── Undo/redo helper methods ─────────────────────
	// Called by annotation commands during undo/redo.
	// Each method mutates the annotation model and refreshes the affected layer widget.

	void doAddAnchor(intptr_t layer_index, double time);
	void doRemoveAnchor(intptr_t layer_index, double time);
	void doMoveAnchor(intptr_t layer_index, double from, double to);
	void doSetEventText(intptr_t layer_index, intptr_t event_1based, const String &text);
	void doSetEventText(intptr_t layer_index, double time, const String &text);
	void doRestoreTextsAroundAnchor(intptr_t layer_index, double time,
	                                const String &left_text, const String &right_text);
	void doSetLayerLabel(intptr_t layer_index, const String &name);

protected:

	// SoundView hooks.
	void addAnnotationLayers(QLayout *layout) override;
	void addAnnotationToolbar(QToolBar *toolbar) override;
	void addDisplayMenuEntries(QMenu *menu) override;
	void onAnchorRequested(double time) override;

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

	// Find/replace actions.
	void onAnnotFind();
	void onAnnotReplace();
	void onAnnotReplaceAll();

	// Bookmark creation from the current event selection (toolbar + Ctrl+B).
	// Opens the BookmarkEditor dialog and, on accept, adds a TimeStamp bookmark
	// for the focused layer and current time selection to the project.
	void onCreateBookmark();

	// ── Undo recording slots ─────────────────────────
	// Connected to LayerWidget signals; create and record undo commands.

	void onAnchorCreationDone(intptr_t layer_index, double time);
	void onAnchorRemovalDone(intptr_t layer_index, double time,
	                         bool is_instant, String left_text, String right_text);
	void onAnchorMoveDone(intptr_t layer_index, double from, double to);
	void onEventTextEdited(intptr_t layer_index, intptr_t event_1based,
	                       String old_text, String new_text);

private:

	int focusedLayerIndex() const;
	int layerLayoutOffset() const;
	LayerWidget *createLayerWidget(intptr_t layer_index);
	LayerWidget *findLayerWidget(intptr_t layer_index) const;
	void refreshLayer(intptr_t layer_index);
	void clearGhostAnchors();
	void applyLayerVisibility();
	void populateSearchBarLayers();
	void seedSearchCursor();

	Handle<Annotation> m_annot;

	// Layer widgets in display order (1-based indexing matches annotation layers).
	std::vector<LayerWidget *> m_layers;

	// Layer visibility (1-based: index 0 is unused). True = visible.
	std::vector<bool> m_layer_visibility;

	// Layout that contains the layer widgets (stored so we can insert/remove dynamically).
	QVBoxLayout *m_layer_layout = nullptr;

	// Toolbar actions.
	QAction *m_link_action = nullptr;
	QAction *m_add_anchor_action = nullptr;
	QAction *m_remove_anchor_action = nullptr;
	QAction *m_bookmark_action = nullptr;

	// Find/replace bar.
	SearchBar *m_searchbar = nullptr;

	// Search cursor: layer and event index for "find next" continuation.
	// Both are 1-based. 0 means "start from the beginning".
	intptr_t m_search_layer = 0;
	intptr_t m_search_event = 0;
};

} // namespace phonometrica

#endif // PHONOMETRICA_ANNOTATION_VIEW_HPP
