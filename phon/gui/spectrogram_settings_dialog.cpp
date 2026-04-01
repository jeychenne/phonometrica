/***********************************************************************************************************************
 *                                                                                                                     *
 * Copyright (C) 2019-2026 Julien Eychenne                                                                             *
 *                                                                                                                     *
 * This program is free software: you can redistribute it and/or modify it under the terms of the GNU General Public   *
 * License as published by the Free Software Foundation, either version 2 of the License, or (at your option) any      *
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
#include <QDialogButtonBox>
#include <QMessageBox>
#include <phon/gui/spectrogram_settings_dialog.hpp>
#include <phon/application/settings.hpp>

namespace phonometrica {

SpectrogramSettingsDialog::SpectrogramSettingsDialog(QWidget *parent) :
	QDialog(parent)
{
	setWindowTitle(tr("Spectrogram settings"));
	setMinimumWidth(400);

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
	auto *reset_btn = new QPushButton(tr("Reset"));
	button_layout->addWidget(reset_btn);
	button_layout->addStretch();
	auto *button_box = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
	button_layout->addWidget(button_box);
	main_layout->addLayout(button_layout);

	// ── Connections ──────────────────────────────────
	connect(m_wide_btn, &QRadioButton::toggled, this, &SpectrogramSettingsDialog::onBandTypeChanged);
	connect(m_narrow_btn, &QRadioButton::toggled, this, &SpectrogramSettingsDialog::onBandTypeChanged);
	connect(m_custom_btn, &QRadioButton::toggled, this, &SpectrogramSettingsDialog::onBandTypeChanged);
	connect(m_dyn_range_slider, &QSlider::valueChanged, this, &SpectrogramSettingsDialog::onDynamicRangeChanged);
	connect(reset_btn, &QPushButton::clicked, this, &SpectrogramSettingsDialog::onReset);
	connect(button_box, &QDialogButtonBox::accepted, this, &SpectrogramSettingsDialog::onOk);
	connect(button_box, &QDialogButtonBox::rejected, this, &QDialog::reject);

	displayValues();
}

void SpectrogramSettingsDialog::onOk()
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
			return;
		}
		window_size = double(value) / 1000;
	}

	// ── Frequency range ─────────────────────────────
	{
		String value(m_bandwidth_edit->text().toUtf8().constData());
		auto bandwidth = value.to_int(&ok);
		if (!ok || bandwidth <= 0)
		{
			QMessageBox::critical(this, tr("Invalid setting"), tr("Invalid frequency range"));
			return;
		}
		Settings::set_value(category, "frequency_range", bandwidth);
	}

	// ── Window type ─────────────────────────────────
	String window_type(m_window_combo->currentText().toUtf8().constData());
	if (window_type.empty())
	{
		QMessageBox::critical(this, tr("Invalid setting"), tr("Invalid window type"));
		return;
	}

	// ── Pre-emphasis threshold ──────────────────────
	{
		String value(m_preemph_edit->text().toUtf8().constData());
		auto threshold = value.to_int(&ok);
		if (!ok || threshold < 0)
		{
			QMessageBox::critical(this, tr("Invalid setting"), tr("Invalid pre-emphasis threshold"));
			return;
		}
		Settings::set_value(category, "preemphasis_threshold", threshold);
	}

	Settings::set_value(category, "window_size", window_size);
	Settings::set_value(category, "window_type", window_type);
	Settings::set_value(category, "dynamic_range", (intptr_t) m_dyn_range_slider->value());

	accept();
}

void SpectrogramSettingsDialog::onReset()
{
	Settings::reset_spectrogram();
	displayValues();
}

void SpectrogramSettingsDialog::onDynamicRangeChanged(int value)
{
	setDynamicRangeLabel(value);
}

void SpectrogramSettingsDialog::onBandTypeChanged()
{
	enableCustomBand(m_custom_btn->isChecked());
}

void SpectrogramSettingsDialog::displayValues()
{
	String category("spectrogram");

	auto range = (int) Settings::get_int(category, "dynamic_range");
	m_dyn_range_slider->setValue(range);
	setDynamicRangeLabel(range);

	auto bw = (int) Settings::get_int(category, "frequency_range");
	m_bandwidth_edit->setText(QString::number(bw));

	double window_size = Settings::get_number(category, "window_size");

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

	auto window_type = Settings::get_string(category, "window_type");
	int idx = m_window_combo->findText(QString::fromUtf8(window_type.data(), (int) window_type.size()));
	if (idx >= 0) {
		m_window_combo->setCurrentIndex(idx);
	}

	int threshold = (int) Settings::get_int(category, "preemphasis_threshold");
	m_preemph_edit->setText(QString::number(threshold));
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
