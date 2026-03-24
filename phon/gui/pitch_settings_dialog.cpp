/***********************************************************************************************************************
 *                                                                                                                     *
 * Copyright (C) 2019-2026 Julien Eychenne                                                                             *
 *                                                                                                                     *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0. If a copy of the MPL was not   *
 * distributed with this file, You can obtain one at https://mozilla.org/MPL/2.0/.                                     *
 *                                                                                                                     *
 * Created: 24/03/2026                                                                                                 *
 *                                                                                                                     *
 * Purpose: see header.                                                                                                *
 *                                                                                                                     *
 ***********************************************************************************************************************/

#include <algorithm>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QDialogButtonBox>
#include <QMessageBox>
#include <phon/gui/pitch_settings_dialog.hpp>
#include <phon/application/settings.hpp>

namespace phonometrica {

// Voicing threshold ranges per method (from SPTK documentation):
//   RAPT:    -0.6 <= T <= 0.7   (default: 0.0)
//   SWIPE:    0.2 <= T <= 0.5   (default: 0.3)
//   REAPER:  -0.5 <= T <= 1.6   (default: 0.9)
//   Harvest:  0.0 <= T <= 0.2   (default: 0.01)
//   Praat:    0.0 <= T <= 1.0   (default: 0.45)

struct ThresholdInfo
{
	double min;
	double max;
	double default_value;
};

static ThresholdInfo getThresholdInfo(const QString &method)
{
	if (method == "rapt")    return { -0.6,  0.7, 0.0  };
	if (method == "swipe")   return {  0.2,  0.5, 0.3  };
	if (method == "harvest") return {  0.0,  0.2, 0.01 };
	if (method == "praat")   return {  0.0,  1.0, 0.45 };
	/* reaper (default) */   return { -0.5,  1.6, 0.9  };
}

PitchSettingsDialog::PitchSettingsDialog(QWidget *parent) :
	QDialog(parent)
{
	setWindowTitle(tr("Pitch settings"));
	setMinimumWidth(350);

	auto *main_layout = new QVBoxLayout(this);

	main_layout->addWidget(new QLabel(tr("Method:")));
	m_method_combo = new QComboBox;
	m_method_combo->addItem(tr("Reaper"), QStringLiteral("reaper"));
	m_method_combo->addItem(tr("Harvest"), QStringLiteral("harvest"));
	m_method_combo->addItem(tr("RAPT"), QStringLiteral("rapt"));
	m_method_combo->addItem(tr("Swipe"), QStringLiteral("swipe"));
	m_method_combo->addItem(tr("Praat"), QStringLiteral("praat"));
	main_layout->addWidget(m_method_combo);

	main_layout->addWidget(new QLabel(tr("Minimum pitch (Hz):")));
	m_min_edit = new QLineEdit;
	main_layout->addWidget(m_min_edit);

	main_layout->addWidget(new QLabel(tr("Maximum pitch (Hz):")));
	m_max_edit = new QLineEdit;
	main_layout->addWidget(m_max_edit);

	main_layout->addWidget(new QLabel(tr("Time step (s):")));
	m_step_edit = new QLineEdit;
	main_layout->addWidget(m_step_edit);

	main_layout->addWidget(new QLabel(tr("Voicing threshold:")));
	m_voicing_edit = new QLineEdit;
	main_layout->addWidget(m_voicing_edit);

	m_voicing_slider = new QSlider(Qt::Horizontal);
	m_voicing_slider->setTickPosition(QSlider::TicksBelow);
	m_voicing_slider->setTickInterval(10);
	main_layout->addWidget(m_voicing_slider);

	// Praat-specific fields (hidden unless Praat is selected).
	m_octave_label = new QLabel(tr("Octave jump cost:"));
	main_layout->addWidget(m_octave_label);
	m_octave_edit = new QLineEdit;
	main_layout->addWidget(m_octave_edit);

	m_voicing_cost_label = new QLabel(tr("Voicing cost:"));
	main_layout->addWidget(m_voicing_cost_label);
	m_voicing_cost_edit = new QLineEdit;
	main_layout->addWidget(m_voicing_cost_edit);

	// ── Buttons ──────────────────────────────────────
	main_layout->addSpacing(10);
	auto *button_layout = new QHBoxLayout;
	auto *reset_btn = new QPushButton(tr("Reset"));
	button_layout->addWidget(reset_btn);
	button_layout->addStretch();
	auto *button_box = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
	button_layout->addWidget(button_box);
	main_layout->addLayout(button_layout);

	// ── Connections ──────────────────────────────────
	connect(reset_btn, &QPushButton::clicked, this, &PitchSettingsDialog::onReset);
	connect(button_box, &QDialogButtonBox::accepted, this, &PitchSettingsDialog::onOk);
	connect(button_box, &QDialogButtonBox::rejected, this, &QDialog::reject);
	connect(m_method_combo, QOverload<int>::of(&QComboBox::currentIndexChanged),
	        this, &PitchSettingsDialog::onMethodChanged);
	connect(m_voicing_slider, &QSlider::valueChanged, this, &PitchSettingsDialog::onSliderMoved);
	connect(m_voicing_edit, &QLineEdit::editingFinished, this, &PitchSettingsDialog::onVoicingEdited);

	displayValues();
}

void PitchSettingsDialog::onOk()
{
	bool ok;
	String category("pitch_tracking");

	auto method = m_method_combo->currentData().toString();

	String text_min(m_min_edit->text().toUtf8().constData());
	auto min_pitch = text_min.to_int(&ok);
	if (!ok || min_pitch < 1)
	{
		QMessageBox::critical(this, tr("Invalid setting"), tr("Invalid minimum pitch"));
		return;
	}

	String text_max(m_max_edit->text().toUtf8().constData());
	auto max_pitch = text_max.to_int(&ok);
	if (!ok || max_pitch <= min_pitch)
	{
		QMessageBox::critical(this, tr("Invalid setting"), tr("Invalid maximum pitch"));
		return;
	}

	String text_step(m_step_edit->text().toUtf8().constData());
	auto step = text_step.to_float(&ok);
	if (!ok || step <= 0.0)
	{
		QMessageBox::critical(this, tr("Invalid setting"), tr("Invalid time step"));
		return;
	}

	String text_voicing(m_voicing_edit->text().toUtf8().constData());
	auto voicing = text_voicing.to_float(&ok);
	if (!ok)
	{
		QMessageBox::critical(this, tr("Invalid setting"), tr("Invalid voicing threshold"));
		return;
	}

	Settings::set_value(category, "method", String(method.toUtf8().constData()));
	Settings::set_value(category, "minimum_pitch", min_pitch);
	Settings::set_value(category, "maximum_pitch", max_pitch);
	Settings::set_value(category, "time_step", step);
	Settings::set_value(category, "voicing_threshold", voicing);

	// Save Praat-specific parameters.
	if (method == "praat")
	{
		String text_octave(m_octave_edit->text().toUtf8().constData());
		auto octave_cost = text_octave.to_float(&ok);
		if (!ok || octave_cost < 0.0)
		{
			QMessageBox::critical(this, tr("Invalid setting"), tr("Invalid octave jump cost"));
			return;
		}

		String text_vcost(m_voicing_cost_edit->text().toUtf8().constData());
		auto vcost = text_vcost.to_float(&ok);
		if (!ok || vcost < 0.0)
		{
			QMessageBox::critical(this, tr("Invalid setting"), tr("Invalid voicing cost"));
			return;
		}

		Settings::set_value(category, "octave_jump_cost", octave_cost);
		Settings::set_value(category, "voicing_cost", vcost);
	}

	accept();
}

void PitchSettingsDialog::onReset()
{
	Settings::reset_pitch_tracking();
	displayValues();
}

void PitchSettingsDialog::displayValues()
{
	m_updating = true;

	String category("pitch_tracking");

	auto method = Settings::get_string(category, "method");
	QString qmethod = QString::fromUtf8(method.data(), (int) method.size());
	int index = m_method_combo->findData(qmethod);
	if (index >= 0)
		m_method_combo->setCurrentIndex(index);

	auto min_pitch = (int) Settings::get_number(category, "minimum_pitch");
	m_min_edit->setText(QString::number(min_pitch));

	auto max_pitch = (int) Settings::get_number(category, "maximum_pitch");
	m_max_edit->setText(QString::number(max_pitch));

	auto step = Settings::get_number(category, "time_step");
	m_step_edit->setText(QString::number(step, 'g'));

	auto voicing = Settings::get_number(category, "voicing_threshold");
	m_voicing_edit->setText(QString::number(voicing, 'g'));

	updateSliderRange(qmethod);
	m_voicing_slider->setValue(thresholdToSlider(voicing));

	// Praat-specific fields.
	try {
		auto octave = Settings::get_number(category, "octave_jump_cost");
		m_octave_edit->setText(QString::number(octave, 'g'));
	} catch (...) {
		m_octave_edit->setText("0.35");
	}
	try {
		auto vcost = Settings::get_number(category, "voicing_cost");
		m_voicing_cost_edit->setText(QString::number(vcost, 'g'));
	} catch (...) {
		m_voicing_cost_edit->setText("0.45");
	}

	updatePraatFieldsVisibility(qmethod);

	m_updating = false;
}

void PitchSettingsDialog::onMethodChanged(int)
{
	if (m_updating) return;

	auto method = m_method_combo->currentData().toString();
	updateSliderRange(method);
	setVoicingDefault(method);
	updatePraatFieldsVisibility(method);
}

void PitchSettingsDialog::updateSliderRange(const QString &method)
{
	auto info = getThresholdInfo(method);
	m_slider_min = static_cast<int>(info.min * 100);
	m_slider_max = static_cast<int>(info.max * 100);
	m_voicing_slider->setRange(m_slider_min, m_slider_max);
}

void PitchSettingsDialog::setVoicingDefault(const QString &method)
{
	auto info = getThresholdInfo(method);

	m_updating = true;
	m_voicing_edit->setText(QString::number(info.default_value, 'g'));
	m_voicing_slider->setValue(thresholdToSlider(info.default_value));
	m_updating = false;
}

void PitchSettingsDialog::updatePraatFieldsVisibility(const QString &method)
{
	bool is_praat = (method == "praat");
	m_octave_label->setVisible(is_praat);
	m_octave_edit->setVisible(is_praat);
	m_voicing_cost_label->setVisible(is_praat);
	m_voicing_cost_edit->setVisible(is_praat);
}

void PitchSettingsDialog::onSliderMoved(int value)
{
	if (m_updating) return;

	m_updating = true;
	double threshold = sliderToThreshold(value);
	m_voicing_edit->setText(QString::number(threshold, 'f', 2));
	m_updating = false;
}

void PitchSettingsDialog::onVoicingEdited()
{
	if (m_updating) return;

	bool ok;
	double value = m_voicing_edit->text().toDouble(&ok);
	if (!ok) return;

	m_updating = true;
	int slider_val = thresholdToSlider(value);
	slider_val = std::clamp(slider_val, m_slider_min, m_slider_max);
	m_voicing_slider->setValue(slider_val);
	m_updating = false;
}

int PitchSettingsDialog::thresholdToSlider(double value) const
{
	return static_cast<int>(value * 100);
}

double PitchSettingsDialog::sliderToThreshold(int value) const
{
	return value / 100.0;
}

} // namespace phonometrica
