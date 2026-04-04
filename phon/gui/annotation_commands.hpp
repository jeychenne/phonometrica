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
 * Created: 26/03/2026                                                                                                 *
 *                                                                                                                     *
 * Purpose: Undoable commands for annotation editing: layer management, anchor operations, and event text editing.      *
 *                                                                                                                     *
 ***********************************************************************************************************************/

#ifndef PHONOMETRICA_ANNOTATION_COMMANDS_HPP
#define PHONOMETRICA_ANNOTATION_COMMANDS_HPP

#include <phon/gui/command.hpp>
#include <phon/string.hpp>

namespace phonometrica {

class AnnotationView;

// ─────────────────────────────────────────────────
//  AddLayerCommand
// ─────────────────────────────────────────────────

class AddLayerCommand : public Command
{
public:

	AddLayerCommand(AnnotationView *view, intptr_t index, String name, bool has_instants) :
		m_view(view), m_index(index), m_name(std::move(name)), m_has_instants(has_instants) {}

	bool execute() override;
	void undo() override;

	String description() const override { return "Add layer"; }

private:

	AnnotationView *m_view;
	intptr_t m_index;
	String m_name;
	bool m_has_instants;
};


// ─────────────────────────────────────────────────
//  RemoveLayerCommand
// ─────────────────────────────────────────────────

class RemoveLayerCommand : public Command
{
public:

	RemoveLayerCommand(AnnotationView *view, intptr_t index) :
		m_view(view), m_index(index) {}

	bool execute() override;
	void undo() override;

	String description() const override { return "Remove layer"; }

private:

	AnnotationView *m_view;
	intptr_t m_index;

	// Saved state for undo: the label and type of the removed layer.
	String m_saved_name;
	bool m_saved_has_instants = false;
};


// ─────────────────────────────────────────────────
//  AddAnchorCommand
// ─────────────────────────────────────────────────
// Recorded after an anchor has already been added by LayerWidget.
// execute()/redo() re-add the anchor; undo() removes it.

class AddAnchorCommand : public Command
{
public:

	AddAnchorCommand(AnnotationView *view, intptr_t layer_index, double time) :
		m_view(view), m_layer_index(layer_index), m_time(time) {}

	bool execute() override;
	void undo() override;

	String description() const override { return "Add anchor"; }

private:

	AnnotationView *m_view;
	intptr_t m_layer_index;
	double m_time;
};


// ─────────────────────────────────────────────────
//  RemoveAnchorCommand
// ─────────────────────────────────────────────────
// Recorded after an anchor has already been removed by LayerWidget.
// execute()/redo() remove the anchor; undo() re-adds it and restores event texts.
//
// For interval layers, removing an anchor merges two adjacent events.
// Undo must re-split the interval and restore both original texts.
// For instant layers, removing an anchor deletes the instant.
// Undo must re-insert the instant with its original text.

class RemoveAnchorCommand : public Command
{
public:

	// For interval layers: save the two texts that were merged.
	// left_text is the text of the event ending at the anchor;
	// right_text is the text of the event starting at the anchor.
	RemoveAnchorCommand(AnnotationView *view, intptr_t layer_index, double time,
	                    bool is_instant, String left_text, String right_text) :
		m_view(view), m_layer_index(layer_index), m_time(time),
		m_is_instant(is_instant), m_left_text(std::move(left_text)),
		m_right_text(std::move(right_text)) {}

	bool execute() override;
	void undo() override;

	String description() const override { return "Remove anchor"; }

private:

	AnnotationView *m_view;
	intptr_t m_layer_index;
	double m_time;
	bool m_is_instant;
	String m_left_text;   // text before anchor (intervals) or instant text (instants)
	String m_right_text;  // text after anchor (intervals); unused for instants
};


// ─────────────────────────────────────────────────
//  MoveAnchorCommand
// ─────────────────────────────────────────────────
// Recorded after an anchor has been dragged to a new position.
// execute()/redo() move from → to; undo() moves to → from.

class MoveAnchorCommand : public Command
{
public:

	MoveAnchorCommand(AnnotationView *view, intptr_t layer_index, double from, double to) :
		m_view(view), m_layer_index(layer_index), m_from(from), m_to(to) {}

	bool execute() override;
	void undo() override;

	String description() const override { return "Move anchor"; }

private:

	AnnotationView *m_view;
	intptr_t m_layer_index;
	double m_from;
	double m_to;
};


// ─────────────────────────────────────────────────
//  EditEventTextCommand
// ─────────────────────────────────────────────────
// Recorded after the inline editor commits a text change.
// execute()/redo() set new text; undo() restores old text.

class EditEventTextCommand : public Command
{
public:

	EditEventTextCommand(AnnotationView *view, intptr_t layer_index, intptr_t event_index,
	                     String old_text, String new_text) :
		m_view(view), m_layer_index(layer_index), m_event_index(event_index),
		m_old_text(std::move(old_text)), m_new_text(std::move(new_text)) {}

	bool execute() override;
	void undo() override;

	String description() const override { return "Edit event text"; }

private:

	AnnotationView *m_view;
	intptr_t m_layer_index;
	intptr_t m_event_index; // 1-based
	String m_old_text;
	String m_new_text;
};


// ─────────────────────────────────────────────────
//  RenameLayerCommand
// ─────────────────────────────────────────────────

class RenameLayerCommand : public Command
{
public:

	RenameLayerCommand(AnnotationView *view, intptr_t layer_index,
	                   String old_name, String new_name) :
		m_view(view), m_layer_index(layer_index),
		m_old_name(std::move(old_name)), m_new_name(std::move(new_name)) {}

	bool execute() override;
	void undo() override;

	String description() const override { return "Rename layer"; }

private:

	AnnotationView *m_view;
	intptr_t m_layer_index;
	String m_old_name;
	String m_new_name;
};


} // namespace phonometrica

#endif // PHONOMETRICA_ANNOTATION_COMMANDS_HPP
