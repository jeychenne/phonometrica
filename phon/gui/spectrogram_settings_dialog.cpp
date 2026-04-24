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
 * Created: 23/03/2026                                                                                                 *
 *                                                                                                                     *
 * Purpose: see header.                                                                                                *
 *                                                                                                                     *
 ***********************************************************************************************************************/

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGroupBox>
#include <QMessageBox>
#include <phon/gui/spectrogram_settings_dialog.hpp>
#include <phon/application/settings.hpp>

namespace phonometrica {

// Default values. Must match Settings::reset_spectrogram() in settings.cpp.
namespace {
constexpr double DEFAULT_WINDOW_SIZE  = 0.005;   // wide-band
constexpr int    DEFAULT_FREQ_RANGE   = 5500;    // Hz
constexpr int    DEFAULT_DYN_RANGE    = 70;      // dB
constexpr int    DEFAULT_PREEMPH      = 1000;    // Hz
const QString    DEFAULT_WINDOW_TYPE  = QStringLiteral("Gaussian");
} // namespace

SpectrogramSettingsDialog::SpectrogramSettingsDialog(QWidget *parent) :
	QDialog(parent)
{
	setWindowTitle(tr("Spectrogram settings"));
	setMinimumWidth(400);
	setWindowFlag(Qt::Tool);

	auto *main_layout = new QVBoxLayout(this);

	// ── Spectrogram type ──────────────────────────────
	auto *type_box = new QGroupBox(tr("Spectrogram type:"));
	auto *type_layout = new QVBoxLayout(type_box);

	m_wide_btn = new QRadioButton(tr("Wide-band (5 ms)"));
	m_narrow_btn = new QRadioButton(tr("Narrow-band (25 ms)"));
	m_custom_btn = new QRadioButton(tr("Custom window size (in ms):"));
	m_winlen_edit = new QLineEdit;

	type_layout->addWidget(m_wide_btn);
	type_layout->addWidget(m_narrow_btn);
	type_layout->addWidget(m_custom_btn);
	type_layout->addWidget(m_winlen_edit);

	main_layout->addWidget(type_box);

	// ── Frequency range ───────────────────────────────
	main_layout->addWidget(new QLabel(tr("Frequency range (Hz):")));
	m_bandwidth_edit = new QLineEdit;
	main_layout->addWidget(m_bandwidth_edit);

	// ── Window type ───────────────────────────────────
	main_layout->addWidget(new QLabel(tr("Window type:")));
	m_window_combo = new QComboBox;
	m_window_combo->addItems({"Bartlett", "Blackman", "Gaussian", "Hamming", "Hann", "Rectangular"});
	main_layout->addWidget(m_window_combo);

	// ── Dynamic range ─────────────────────────────────
	m_dyn_range_label = new QLabel;
	main_layout->addWidget(m_dyn_range_label);
	m_dyn_range_slider = new QSlider(Qt::Horizontal);
	m_dyn_range_slider->setRange(1, 255);
	main_layout->addWidget(m_dyn_range_slider);

	// ── Pre-emphasis threshold ────────────────────────
	main_layout->addWidget(new QLabel(tr("Pre-emphasis threshold (Hz):")));
	m_preemph_edit = new QLineEdit;
	main_layout->addWidget(m_preemph_edit);

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
	connect(m_wide_btn, &QRadioButton::toggled, this, &SpectrogramSettingsDialog::onBandTypeChanged);
	connect(m_narrow_btn, &QRadioButton::toggled, this, &SpectrogramSettingsDialog::onBandTypeChanged);
	connect(m_custom_btn, &QRadioButton::toggled, this, &SpectrogramSettingsDialog::onBandTypeChanged);
	connect(m_dyn_range_slider, &QSlider::valueChanged, this, &SpectrogramSettingsDialog::onDynamicRangeChanged);
	connect(reset_btn, &QPushButton::clicked, this, &SpectrogramSettingsDialog::onResetToDefaults);
	connect(m_apply_btn, &QPushButton::clicked, this, &SpectrogramSettingsDialog::onApply);
	connect(m_ok_btn, &QPushButton::clicked, this, &SpectrogramSettingsDialog::onOk);
	connect(cancel_btn, &QPushButton::clicked, this, &SpectrogramSettingsDialog::onCancel);

	snapshotSettings();
	displayCurrentValues();
}

void SpectrogramSettingsDialog::onApply()
{
	if (validateAndCommit()) {
		m_snapshot.applied_any = true;
		emit settingsApplied();
	}
}

void SpectrogramSettingsDialog::onOk()
{
	if (validateAndCommit()) {
		m_snapshot.applied_any = true;
		emit settingsApplied();
		accept();
	}
}

void SpectrogramSettingsDialog::onCancel()
{
	reject();
}

void SpectrogramSettingsDialog::onResetToDefaults()
{
	displayDefaultValues();
}

void SpectrogramSettingsDialog::reject()
{
	if (restoreSnapshot()) {
		emit settingsApplied();
	}
	QDialog::reject();
}

bool SpectrogramSettingsDialog::validateAndCommit()
{
	String category("spectrogram");
	bool ok;

	// ── Window size ──────────────────────────────────
	double window_size = 0.005;

	if (m_narrow_btn->isChecked())
	{
		window_size = 0.025;
	}
	else if (m_custom_btn->isChecked())
	{
		String sval(m_winlen_edit->text().toUtf8().constData());
		int value = sval.to_int(&ok);
		if (!ok || value <= 0)
		{
			QMessageBox::critical(this, tr("Invalid setting"), tr("Invalid window size"));
			m_winlen_edit->setFocus();
			m_winlen_edit->selectAll();
			return false;
		}
		window_size = double(value) / 1000;
	}

	// ── Frequency range ─────────────────────────────
	String value(m_bandwidth_edit->text().toUtf8().constData());
	auto bandwidth = value.to_int(&ok);
	if (!ok || bandwidth <= 0)
	{
		QMessageBox::critical(this, tr("Invalid setting"), tr("Invalid frequency range"));
		m_bandwidth_edit->setFocus();
		m_bandwidth_edit->selectAll();
		return false;
	}

	// ── Window type ─────────────────────────────────
	String window_type(m_window_combo->currentText().toUtf8().constData());
	if (window_type.empty())
	{
		QMessageBox::critical(this, tr("Invalid setting"), tr("Invalid window type"));
		return false;
	}

	// ── Pre-emphasis threshold ──────────────────────
	String preemph_text(m_preemph_edit->text().toUtf8().constData());
	auto threshold = preemph_text.to_int(&ok);
	if (!ok || threshold < 0)
	{
		QMessageBox::critical(this, tr("Invalid setting"), tr("Invalid pre-emphasis threshold"));
		m_preemph_edit->setFocus();
		m_preemph_edit->selectAll();
		return false;
	}

	Settings::set_value(category, "window_size", window_size);
	Settings::set_value(category, "frequency_range", bandwidth);
	Settings::set_value(category, "window_type", window_type);
	Settings::set_value(category, "dynamic_range", (intptr_t) m_dyn_range_slider->value());
	Settings::set_value(category, "preemphasis_threshold", threshold);

	return true;
}

void SpectrogramSettingsDialog::applyValuesToWidgets(double window_size, int freq_range,
	const QString &window_type, int dynamic_range, int preemph_threshold)
{
	m_dyn_range_slider->setValue(dynamic_range);
	setDynamicRangeLabel(dynamic_range);

	m_bandwidth_edit->setText(QString::number(freq_range));

	if (window_size == 0.005)
	{
		m_wide_btn->setChecked(true);
		enableCustomBand(false);
	}
	else if (window_size == 0.025)
	{
		m_narrow_btn->setChecked(true);
		enableCustomBand(false);
	}
	else
	{
		m_custom_btn->setChecked(true);
		enableCustomBand(true);
		m_winlen_edit->setText(QString::number(window_size * 1000, 'f', 4));
	}

	int idx = m_window_combo->findText(window_type);
	if (idx >= 0) {
		m_window_combo->setCurrentIndex(idx);
	}

	m_preemph_edit->setText(QString::number(preemph_threshold));
}

void SpectrogramSettingsDialog::displayCurrentValues()
{
	String category("spectrogram");

	auto dynamic_range     = (int) Settings::get_int(category, "dynamic_range");
	auto freq_range        = (int) Settings::get_int(category, "frequency_range");
	auto window_size       = Settings::get_number(category, "window_size");
	auto window_type       = Settings::get_string(category, "window_type");
	int  preemph_threshold = (int) Settings::get_int(category, "preemphasis_threshold");

	applyValuesToWidgets(window_size, freq_range,
	                     QString::fromUtf8(window_type.data(), (int) window_type.size()),
	                     dynamic_range, preemph_threshold);
}

void SpectrogramSettingsDialog::displayDefaultValues()
{
	applyValuesToWidgets(DEFAULT_WINDOW_SIZE, DEFAULT_FREQ_RANGE,
	                     DEFAULT_WINDOW_TYPE, DEFAULT_DYN_RANGE, DEFAULT_PREEMPH);
}

void SpectrogramSettingsDialog::snapshotSettings()
{
	String category("spectrogram");
	m_snapshot.window_size       = Settings::get_number(category, "window_size");
	m_snapshot.freq_range        = (int) Settings::get_int(category, "frequency_range");
	auto wt                      = Settings::get_string(category, "window_type");
	m_snapshot.window_type       = QString::fromUtf8(wt.data(), (int) wt.size());
	m_snapshot.dynamic_range     = (int) Settings::get_int(category, "dynamic_range");
	m_snapshot.preemph_threshold = (int) Settings::get_int(category, "preemphasis_threshold");
	m_snapshot.applied_any       = false;
}

bool SpectrogramSettingsDialog::restoreSnapshot()
{
	if (!m_snapshot.applied_any) return false;

	String category("spectrogram");
	Settings::set_value(category, "window_size",           m_snapshot.window_size);
	Settings::set_value(category, "frequency_range",       intptr_t(m_snapshot.freq_range));
	Settings::set_value(category, "window_type",           String(m_snapshot.window_type.toUtf8().constData()));
	Settings::set_value(category, "dynamic_range",         intptr_t(m_snapshot.dynamic_range));
	Settings::set_value(category, "preemphasis_threshold", intptr_t(m_snapshot.preemph_threshold));
	return true;
}

void SpectrogramSettingsDialog::onDynamicRangeChanged(int value)
{
	setDynamicRangeLabel(value);
}

void SpectrogramSettingsDialog::onBandTypeChanged()
{
	enableCustomBand(m_custom_btn->isChecked());
}

void SpectrogramSettingsDialog::enableCustomBand(bool value)
{
	m_winlen_edit->setEnabled(value);
	if (!value) {
		m_winlen_edit->clear();
	}
}

void SpectrogramSettingsDialog::setDynamicRangeLabel(int value)
{
	m_dyn_range_label->setText(tr("Dynamic range: %1 dB").arg(value));
}

} // namespace phonometrica
