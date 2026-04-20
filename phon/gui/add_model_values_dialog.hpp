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
 * Purpose: Dialog to select which per-observation model quantities (fitted values, residuals, scaled residuals) the   *
 *          user wants to append as new numeric columns to the source DataTable backing an Analysis. The user picks    *
 *          a column-name prefix (defaulting to the model label); final column names are "{prefix}_fitted",            *
 *          "{prefix}_resid", "{prefix}_scaled_resid". Existing column headers are checked for collisions; overwrite   *
 *          is not supported in this version.                                                                          *
 *                                                                                                                     *
 ***********************************************************************************************************************/

#ifndef PHONOMETRICA_ADD_MODEL_VALUES_DIALOG_HPP
#define PHONOMETRICA_ADD_MODEL_VALUES_DIALOG_HPP

#include <QDialog>
#include <QLineEdit>
#include <QCheckBox>
#include <QDialogButtonBox>
#include <QLabel>

namespace phonometrica {

class AddModelValuesDialog final : public QDialog
{
	Q_OBJECT

public:

	/// @param model_label        Display name of the model (or "Model N").
	/// @param source_name        Display name of the source DataTable.
	/// @param existing_headers   Current column headers in the source, used for collision checking.
	/// @param scaled_available   Whether scaled residuals can be computed for this model.
	AddModelValuesDialog(const QString &model_label,
	                     const QString &source_name,
	                     const QStringList &existing_headers,
	                     bool scaled_available,
	                     QWidget *parent = nullptr);

	// Accessors — only meaningful after exec() == Accepted.
	QString prefix() const;
	bool wantsFitted() const;
	bool wantsResiduals() const;
	bool wantsScaledResiduals() const;

	// Final column names, computed from the current prefix. Stable across
	// exec() and the post-accept readback.
	QString fittedColumnName() const;
	QString residualsColumnName() const;
	QString scaledResidualsColumnName() const;

private:

	void refreshValidation();

	// Turn a model label into a valid-ish column name: trim whitespace and
	// replace any run of characters that are not ASCII letters, digits, or
	// underscore with a single '_'. Leading/trailing underscores are stripped.
	// Never empty unless the input was empty to begin with; a last-resort
	// fallback is handled by the dialog itself.
	static QString sanitizePrefix(const QString &label);

	QLineEdit *m_prefix_edit = nullptr;
	QCheckBox *m_fitted_check = nullptr;
	QCheckBox *m_resid_check = nullptr;
	QCheckBox *m_scaled_check = nullptr;
	QLabel *m_fitted_preview = nullptr;
	QLabel *m_resid_preview = nullptr;
	QLabel *m_scaled_preview = nullptr;
	QLabel *m_error_label = nullptr;
	QDialogButtonBox *m_buttons = nullptr;

	QStringList m_existing_headers;
	bool m_scaled_available = true;
};

} // namespace phonometrica

#endif // PHONOMETRICA_ADD_MODEL_VALUES_DIALOG_HPP
