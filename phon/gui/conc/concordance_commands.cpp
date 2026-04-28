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
 * Created: 04/04/2026                                                                                                 *
 *                                                                                                                     *
 * Purpose: see header.                                                                                                *
 *                                                                                                                     *
 ***********************************************************************************************************************/

#include <phon/gui/conc/concordance_commands.hpp>
#include <phon/gui/conc/concordance_view.hpp>

namespace phonometrica {

// ─────────────────────────────────────────────────
//  DeleteMatchesCommand
// ─────────────────────────────────────────────────

bool DeleteMatchesCommand::execute()
{
	// Called on redo. Remove the same rows again (in reverse order).
	auto *model = m_view->concModel();
	for (int i = (int) m_removed.size() - 1; i >= 0; i--)
		m_removed[i].data = model->removeMatch(m_removed[i].source_row);

	m_view->refreshAfterRowChange();
	return true;
}

void DeleteMatchesCommand::undo()
{
	// Restore rows in ascending order so that row indices stay valid.
	auto *model = m_view->concModel();
	for (auto &rm : m_removed)
		model->restoreMatch(rm.source_row, std::move(rm.data));

	m_view->refreshAfterRowChange();
}


// ─────────────────────────────────────────────────
//  AddConcAuxColumnCommand
// ─────────────────────────────────────────────────

bool AddConcAuxColumnCommand::execute()
{
	// Called on redo: restore the previously saved column.
	if (m_has_saved)
	{
		auto conc = m_view->concModel()->concordance();
		conc->restore_aux_column(conc->aux_stored_count() + 1, std::move(m_saved_col));
		m_has_saved = false;
		m_view->refreshAfterStructuralChange();
		// The restored column is now the last one in the model.
		int inserted_col = m_view->concModel()->columnCount() - 1;
		m_view->adjustFiltersAfterColumnInsert(inserted_col);
	}
	// On first call (via record()), the column was already added by the view code.
	return true;
}

void AddConcAuxColumnCommand::undo()
{
	auto conc = m_view->concModel()->concordance();
	intptr_t last = conc->aux_stored_count();
	// The column to remove maps to the last source-model column.
	int removed_col = m_view->concModel()->columnCount() - 1;
	m_saved_col = conc->extract_aux_column(last);
	m_has_saved = true;
	m_view->adjustFiltersAfterColumnRemove(removed_col);
	m_view->refreshAfterStructuralChange();
}


// ─────────────────────────────────────────────────
//  RenameConcColumnCommand
// ─────────────────────────────────────────────────

bool RenameConcColumnCommand::execute()
{
	// Called on first submit and on redo.
	m_view->concModel()->setHeaderData(m_section, Qt::Horizontal, m_new_name, Qt::EditRole);
	m_view->refreshAfterRowChange(); // lightweight refresh
	return true;
}

void RenameConcColumnCommand::undo()
{
	m_view->concModel()->setHeaderData(m_section, Qt::Horizontal, m_old_name, Qt::EditRole);
	m_view->refreshAfterRowChange();
}


// ─────────────────────────────────────────────────
//  EditCellCommand
// ─────────────────────────────────────────────────

bool EditCellCommand::execute()
{
	// Called on redo: re-apply the new text. On first invocation we go through
	// record() (not submit()), so the initial edit is already committed and
	// execute() is not called — only redo after an undo lands here.
	return m_view->concModel()->applyCellEdit(m_row, m_col, m_new_text);
}

void EditCellCommand::undo()
{
	m_view->concModel()->applyCellEdit(m_row, m_col, m_old_text);
}


} // namespace phonometrica
