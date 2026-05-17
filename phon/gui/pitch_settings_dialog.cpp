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
 * Created: 24/03/2026                                                                                                 *
 *                                                                                                                     *
 * Purpose: see header.                                                                                                *
 *                                                                                                                     *
 ***********************************************************************************************************************/

#include <algorithm>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QMessageBox>
#include <phon/gui/pitch_settings_dialog.hpp>
#include <phon/application/settings.hpp>

namespace phonometrica {

// Voicing threshold ranges per method (from SPTK / Praat documentation):
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

// Default values. Must match Settings::reset_pitch_tracking() in settings.cpp.
namespace {
const QString  DEFAULT_METHOD            = QStringLiteral("reaper");
constexpr int  DEFAULT_MIN_PITCH         = 70;
constexpr int  DEFAULT_MAX_PITCH         = 500;
constexpr double DEFAULT_TIME_STEP       = 0.01;
constexpr double DEFAULT_VOICING         = 0.9;   // reaper default
constexpr double DEFAULT_SILENCE         = 0.03;
constexpr double DEFAULT_OCTAVE_COST     = 0.01;
constexpr double DEFAULT_OCTAVE_JUMP     = 0.35;
constexpr double DEFAULT_VOICING_COST    = 0.14;
constexpr bool   DEFAULT_USE_GAUSSIAN    = false;
} // namespace

// Helper used in several places where a Praat-specific parameter may be
// missing from the Settings table (older config files that predate those
// keys) — returns the fallback rather than throwing.
static double readPraatParam(const String &category, const char *key, double fallback)
{
	try { return Settings::get_number(category, key); }
	catch (...) { return fallback; }
}

static bool readPraatBool(const String &category, const char *key, bool fallback)
{
	try { return Settings::get_boolean(category, key); }
	catch (...) { return fallback; }
}

PitchSettingsDialog::PitchSettingsDialog(QWidget *parent) :
	QDialog(parent)
{
	setWindowTitle(tr("Pitch settings"));
	setMinimumWidth(350);
	setWindowFlag(Qt::Tool);

	auto *main_layout = new QVBoxLayout(this);

	main_layout->addWidget(new QLabel(tr("Method:")));
	m_method_combo = new QComboBox;
	m_method_combo->addItem(tr("REAPER (default, robust)"), QStringLiteral("reaper"));
	m_method_combo->addItem(tr("Praat (autocorrelation)"),  QStringLiteral("praat"));
	m_method_combo->addItem(tr("SWIPE (octave-robust)"),    QStringLiteral("swipe"));
	m_method_combo->addItem(tr("Harvest (high accuracy)"),  QStringLiteral("harvest"));
	m_method_combo->addItem(tr("RAPT (classic)"),           QStringLiteral("rapt"));
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
	m_silence_label = new QLabel(tr("Silence threshold:"));
	main_layout->addWidget(m_silence_label);
	m_silence_edit = new QLineEdit;
	main_layout->addWidget(m_silence_edit);

	m_octave_cost_label = new QLabel(tr("Octave cost:"));
	main_layout->addWidget(m_octave_cost_label);
	m_octave_cost_edit = new QLineEdit;
	main_layout->addWidget(m_octave_cost_edit);

	m_octave_jump_label = new QLabel(tr("Octave-jump cost:"));
	main_layout->addWidget(m_octave_jump_label);
	m_octave_jump_edit = new QLineEdit;
	main_layout->addWidget(m_octave_jump_edit);

	m_voicing_cost_label = new QLabel(tr("Voiced/unvoiced cost:"));
	main_layout->addWidget(m_voicing_cost_label);
	m_voicing_cost_edit = new QLineEdit;
	main_layout->addWidget(m_voicing_cost_edit);

	m_gaussian_check = new QCheckBox(tr("Very accurate (Gaussian window)"));
	m_gaussian_check->setToolTip(tr(
		"If unchecked, a 3-period Hanning window is used (Praat's default).\n"
		"If checked, a 6-period Gaussian window is used — more accurate but twice as slow."));
	main_layout->addWidget(m_gaussian_check);

	// ── Buttons ──────────────────────────────────────
	main_layout->addSpacing(10);
	auto *button_layout = new QHBoxLayout;
	auto *reset_btn = new QPushButton(tr("Reset to defaults"));
	button_layout->addWidget(reset_btn);
	button_layout->addStretch();
	auto *cancel_btn = new QPushButton(tr("Cancel"));
	m_apply_btn = new QPushButton(tr("Apply"));
	m_ok_btn = new QPushButton(tr("OK"));
	m_ok_btn->setDefault(true);
	button_layout->addWidget(cancel_btn);
	button_layout->addWidget(m_apply_btn);
	button_layout->addWidget(m_ok_btn);
	main_layout->addLayout(button_layout);

	// ── Connections ──────────────────────────────────
	connect(reset_btn, &QPushButton::clicked, this, &PitchSettingsDialog::onResetToDefaults);
	connect(m_apply_btn, &QPushButton::clicked, this, &PitchSettingsDialog::onApply);
	connect(m_ok_btn, &QPushButton::clicked, this, &PitchSettingsDialog::onOk);
	connect(cancel_btn, &QPushButton::clicked, this, &PitchSettingsDialog::onCancel);
	connect(m_method_combo, QOverload<int>::of(&QComboBox::currentIndexChanged),
	        this, &PitchSettingsDialog::onMethodChanged);
	connect(m_voicing_slider, &QSlider::valueChanged, this, &PitchSettingsDialog::onSliderMoved);
	connect(m_voicing_edit, &QLineEdit::editingFinished, this, &PitchSettingsDialog::onVoicingEdited);

	snapshotSettings();
	displayCurrentValues();
}

void PitchSettingsDialog::onApply()
{
	if (validateAndCommit()) {
		m_snapshot.applied_any = true;
		emit settingsApplied();
	}
}

void PitchSettingsDialog::onOk()
{
	if (validateAndCommit()) {
		m_snapshot.applied_any = true;
		emit settingsApplied();
		accept();
	}
}

void PitchSettingsDialog::onCancel()
{
	reject();
}

void PitchSettingsDialog::onResetToDefaults()
{
	displayDefaultValues();
}

void PitchSettingsDialog::reject()
{
	if (restoreSnapshot()) {
		emit settingsApplied();
	}
	QDialog::reject();
}

bool PitchSettingsDialog::validateAndCommit()
{
	bool ok;
	String category("pitch_tracking");

	auto method = m_method_combo->currentData().toString();

	String text_min(m_min_edit->text().toUtf8().constData());
	auto min_pitch = text_min.to_int(&ok);
	if (!ok || min_pitch < 1)
	{
		QMessageBox::critical(this, tr("Invalid setting"), tr("Invalid minimum pitch"));
		m_min_edit->setFocus();
		m_min_edit->selectAll();
		return false;
	}

	String text_max(m_max_edit->text().toUtf8().constData());
	auto max_pitch = text_max.to_int(&ok);
	if (!ok || max_pitch <= min_pitch)
	{
		QMessageBox::critical(this, tr("Invalid setting"), tr("Invalid maximum pitch"));
		m_max_edit->setFocus();
		m_max_edit->selectAll();
		return false;
	}

	String text_step(m_step_edit->text().toUtf8().constData());
	auto step = text_step.to_float(&ok);
	if (!ok || step <= 0.0)
	{
		QMessageBox::critical(this, tr("Invalid setting"), tr("Invalid time step"));
		m_step_edit->setFocus();
		m_step_edit->selectAll();
		return false;
	}

	String text_voicing(m_voicing_edit->text().toUtf8().constData());
	auto voicing = text_voicing.to_float(&ok);
	if (!ok)
	{
		QMessageBox::critical(this, tr("Invalid setting"), tr("Invalid voicing threshold"));
		m_voicing_edit->setFocus();
		m_voicing_edit->selectAll();
		return false;
	}

	// Praat-specific parameters (validated only when the Praat method is
	// selected). Reading them before committing anything so we don't leave
	// the Settings table half-written on a Praat-field validation failure.
	double silence = 0, octave_cost = 0, octave_jump = 0, voicing_cost = 0;
	if (method == "praat")
	{
		String text_silence(m_silence_edit->text().toUtf8().constData());
		silence = text_silence.to_float(&ok);
		if (!ok || silence < 0.0)
		{
			QMessageBox::critical(this, tr("Invalid setting"), tr("Invalid silence threshold"));
			m_silence_edit->setFocus();
			m_silence_edit->selectAll();
			return false;
		}

		String text_oc(m_octave_cost_edit->text().toUtf8().constData());
		octave_cost = text_oc.to_float(&ok);
		if (!ok || octave_cost < 0.0)
		{
			QMessageBox::critical(this, tr("Invalid setting"), tr("Invalid octave cost"));
			m_octave_cost_edit->setFocus();
			m_octave_cost_edit->selectAll();
			return false;
		}

		String text_ojc(m_octave_jump_edit->text().toUtf8().constData());
		octave_jump = text_ojc.to_float(&ok);
		if (!ok || octave_jump < 0.0)
		{
			QMessageBox::critical(this, tr("Invalid setting"), tr("Invalid octave-jump cost"));
			m_octave_jump_edit->setFocus();
			m_octave_jump_edit->selectAll();
			return false;
		}

		String text_vcost(m_voicing_cost_edit->text().toUtf8().constData());
		voicing_cost = text_vcost.to_float(&ok);
		if (!ok || voicing_cost < 0.0)
		{
			QMessageBox::critical(this, tr("Invalid setting"), tr("Invalid voiced/unvoiced cost"));
			m_voicing_cost_edit->setFocus();
			m_voicing_cost_edit->selectAll();
			return false;
		}
	}

	// All validations passed — now commit.
	Settings::set_value(category, "method", String(method.toUtf8().constData()));
	Settings::set_value(category, "minimum_pitch", min_pitch);
	Settings::set_value(category, "maximum_pitch", max_pitch);
	Settings::set_value(category, "time_step", step);
	Settings::set_value(category, "voicing_threshold", voicing);

	if (method == "praat")
	{
		Settings::set_value(category, "silence_threshold", silence);
		Settings::set_value(category, "octave_cost", octave_cost);
		Settings::set_value(category, "octave_jump_cost", octave_jump);
		Settings::set_value(category, "voicing_cost", voicing_cost);
		Settings::set_value(category, "use_gaussian", m_gaussian_check->isChecked());
	}

	return true;
}

void PitchSettingsDialog::displayCurrentValues()
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

	m_silence_edit->setText(QString::number(readPraatParam(category, "silence_threshold", DEFAULT_SILENCE), 'g'));
	m_octave_cost_edit->setText(QString::number(readPraatParam(category, "octave_cost", DEFAULT_OCTAVE_COST), 'g'));
	m_octave_jump_edit->setText(QString::number(readPraatParam(category, "octave_jump_cost", DEFAULT_OCTAVE_JUMP), 'g'));
	m_voicing_cost_edit->setText(QString::number(readPraatParam(category, "voicing_cost", DEFAULT_VOICING_COST), 'g'));
	m_gaussian_check->setChecked(readPraatBool(category, "use_gaussian", DEFAULT_USE_GAUSSIAN));

	updatePraatFieldsVisibility(qmethod);

	m_updating = false;
}

void PitchSettingsDialog::displayDefaultValues()
{
	m_updating = true;

	int index = m_method_combo->findData(DEFAULT_METHOD);
	if (index >= 0)
		m_method_combo->setCurrentIndex(index);

	m_min_edit->setText(QString::number(DEFAULT_MIN_PITCH));
	m_max_edit->setText(QString::number(DEFAULT_MAX_PITCH));
	m_step_edit->setText(QString::number(DEFAULT_TIME_STEP, 'g'));
	m_voicing_edit->setText(QString::number(DEFAULT_VOICING, 'g'));

	updateSliderRange(DEFAULT_METHOD);
	m_voicing_slider->setValue(thresholdToSlider(DEFAULT_VOICING));

	m_silence_edit->setText(QString::number(DEFAULT_SILENCE, 'g'));
	m_octave_cost_edit->setText(QString::number(DEFAULT_OCTAVE_COST, 'g'));
	m_octave_jump_edit->setText(QString::number(DEFAULT_OCTAVE_JUMP, 'g'));
	m_voicing_cost_edit->setText(QString::number(DEFAULT_VOICING_COST, 'g'));
	m_gaussian_check->setChecked(DEFAULT_USE_GAUSSIAN);

	updatePraatFieldsVisibility(DEFAULT_METHOD);

	m_updating = false;
}

void PitchSettingsDialog::snapshotSettings()
{
	String category("pitch_tracking");

	auto method_str = Settings::get_string(category, "method");
	m_snapshot.method = QString::fromUtf8(method_str.data(), (int) method_str.size());
	m_snapshot.min_pitch = (int) Settings::get_number(category, "minimum_pitch");
	m_snapshot.max_pitch = (int) Settings::get_number(category, "maximum_pitch");
	m_snapshot.time_step = Settings::get_number(category, "time_step");
	m_snapshot.voicing_threshold = Settings::get_number(category, "voicing_threshold");

	// Praat-specific keys may be missing on fresh installs or old config
	// files; use the documented defaults as fallbacks.
	m_snapshot.silence_threshold = readPraatParam(category, "silence_threshold", DEFAULT_SILENCE);
	m_snapshot.octave_cost       = readPraatParam(category, "octave_cost", DEFAULT_OCTAVE_COST);
	m_snapshot.octave_jump_cost  = readPraatParam(category, "octave_jump_cost", DEFAULT_OCTAVE_JUMP);
	m_snapshot.voicing_cost      = readPraatParam(category, "voicing_cost", DEFAULT_VOICING_COST);
	m_snapshot.use_gaussian      = readPraatBool(category, "use_gaussian", DEFAULT_USE_GAUSSIAN);

	m_snapshot.applied_any = false;
}

bool PitchSettingsDialog::restoreSnapshot()
{
	if (!m_snapshot.applied_any) return false;

	String category("pitch_tracking");
	Settings::set_value(category, "method", String(m_snapshot.method.toUtf8().constData()));
	Settings::set_value(category, "minimum_pitch",     intptr_t(m_snapshot.min_pitch));
	Settings::set_value(category, "maximum_pitch",     intptr_t(m_snapshot.max_pitch));
	Settings::set_value(category, "time_step",         m_snapshot.time_step);
	Settings::set_value(category, "voicing_threshold", m_snapshot.voicing_threshold);
	Settings::set_value(category, "silence_threshold", m_snapshot.silence_threshold);
	Settings::set_value(category, "octave_cost",       m_snapshot.octave_cost);
	Settings::set_value(category, "octave_jump_cost",  m_snapshot.octave_jump_cost);
	Settings::set_value(category, "voicing_cost",      m_snapshot.voicing_cost);
	Settings::set_value(category, "use_gaussian",      m_snapshot.use_gaussian);
	return true;
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
	m_silence_label->setVisible(is_praat);
	m_silence_edit->setVisible(is_praat);
	m_octave_cost_label->setVisible(is_praat);
	m_octave_cost_edit->setVisible(is_praat);
	m_octave_jump_label->setVisible(is_praat);
	m_octave_jump_edit->setVisible(is_praat);
	m_voicing_cost_label->setVisible(is_praat);
	m_voicing_cost_edit->setVisible(is_praat);
	m_gaussian_check->setVisible(is_praat);
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
