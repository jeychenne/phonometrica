/***********************************************************************************************************************
 *                                                                                                                     *
 * Copyright (C) 2019-2026 Julien Eychenne                                                                             *
 *                                                                                                                     *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0. If a copy of the MPL was not   *
 * distributed with this file, You can obtain one at https://mozilla.org/MPL/2.0/.                                     *
 *                                                                                                                     *
 * Created: 25/03/2026                                                                                                 *
 *                                                                                                                     *
 * Purpose: Dialog for creating a new annotation layer. Asks for the layer name, type (intervals or instants), and     *
 *          position (1-based index).                                                                                  *
 *                                                                                                                     *
 ***********************************************************************************************************************/

#ifndef PHONOMETRICA_NEW_LAYER_DIALOG_HPP
#define PHONOMETRICA_NEW_LAYER_DIALOG_HPP

#include <QDialog>
#include <QLineEdit>
#include <QSpinBox>
#include <QRadioButton>

namespace phonometrica {

class NewLayerDialog : public QDialog
{
	Q_OBJECT

public:

	NewLayerDialog(QWidget *parent, intptr_t current_layer_count);

	QString layerName() const;
	intptr_t layerIndex() const;
	bool hasInstants() const;

private:

	QLineEdit *m_name_edit;
	QSpinBox *m_index_spin;
	QRadioButton *m_interval_radio;
	QRadioButton *m_instant_radio;
};

} // namespace phonometrica

#endif // PHONOMETRICA_NEW_LAYER_DIALOG_HPP
