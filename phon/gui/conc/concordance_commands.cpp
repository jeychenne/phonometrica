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

#include <algorithm>
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

std::vector<int> AddConcAuxColumnCommand::displayColumns(intptr_t aux) const
{
	// Aux columns are not the last columns of the model (property and description columns follow them), so look
	// them up rather than assuming they come last.
	auto conc = m_view->concModel()->concordance();
	std::vector<int> cols;
	for (intptr_t j = 0; j < conc->column_count(); j++) {
		if (conc->resolve_aux_column(j) == aux) cols.push_back(int(j));
	}
	return cols;
}

bool AddConcAuxColumnCommand::execute()
{
	// Called on redo: restore the previously saved columns.
	if (m_has_saved)
	{
		auto conc = m_view->concModel()->concordance();
		intptr_t first = conc->aux_stored_count();
		for (auto &col : m_saved_cols) {
			conc->restore_aux_column(conc->aux_stored_count(), std::move(col));
		}
		m_saved_cols.clear();
		m_has_saved = false;
		m_view->refreshAfterStructuralChange();

		// Shift filter rules past the inserted columns, lowest first, using their final positions.
		for (intptr_t c = first; c < conc->aux_stored_count(); c++) {
			for (int j : displayColumns(c)) m_view->adjustFiltersAfterColumnInsert(j);
		}
	}
	// On first call (via record()), the columns were already added by the view code.
	return true;
}

void AddConcAuxColumnCommand::undo()
{
	auto conc = m_view->concModel()->concordance();
	int count = std::min<int>(m_count, int(conc->aux_stored_count()));
	m_saved_cols.clear();

	for (int k = 0; k < count; k++)
	{
		intptr_t last = conc->aux_stored_count() - 1;
		auto cols = displayColumns(last);
		for (auto it = cols.rbegin(); it != cols.rend(); ++it) {
			m_view->adjustFiltersAfterColumnRemove(*it);
		}
		m_saved_cols.push_back(conc->extract_aux_column(last));
	}

	// Columns were extracted last first: put them back in their original order.
	std::reverse(m_saved_cols.begin(), m_saved_cols.end());
	m_has_saved = true;
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
