/***********************************************************************************************************************
 *                                                                                                                     *
 * Copyright (C) 2019-2026 Julien Eychenne                                                                             *
 *                                                                                                                     *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0. If a copy of the MPL was not   *
 * distributed with this file, You can obtain one at https://mozilla.org/MPL/2.0/.                                     *
 *                                                                                                                     *
 * Created: 26/03/2026                                                                                                 *
 *                                                                                                                     *
 * Purpose: see header.                                                                                                *
 *                                                                                                                     *
 ***********************************************************************************************************************/

#include <phon/gui/annotation_commands.hpp>
#include <phon/gui/annotation_view.hpp>

namespace phonometrica {

// ─────────────────────────────────────────────────
//  AddLayerCommand
// ─────────────────────────────────────────────────

bool AddLayerCommand::execute()
{
	return m_view->addLayer(m_index, m_name, m_has_instants);
}

void AddLayerCommand::undo()
{
	m_view->removeLayer(m_index);
}


// ─────────────────────────────────────────────────
//  RemoveLayerCommand
// ─────────────────────────────────────────────────

bool RemoveLayerCommand::execute()
{
	// Save the layer's metadata before removing it, so undo can recreate
	// an (empty) layer with the same name and type.
	auto annot = m_view->annotation();
	m_saved_name = annot->get_layer_label(m_index);
	m_saved_has_instants = annot->layer_has_instants(m_index);

	m_view->removeLayer(m_index);
	return true;
}

void RemoveLayerCommand::undo()
{
	// Re-create the layer. Note: this creates an empty layer — event content
	// is not preserved. This matches the wx version's behaviour.
	m_view->addLayer(m_index, m_saved_name, m_saved_has_instants);
}

} // namespace phonometrica
