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
 * Created: 28/03/2026                                                                                                 *
 *                                                                                                                     *
 * Purpose: see header.                                                                                                *
 *                                                                                                                     *
 ***********************************************************************************************************************/

#include <QLabel>
#include <QPushButton>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <phon/gui/file_dialog.hpp>
#include <phon/gui/user_dialog.hpp>

namespace phonometrica {

// ---------------------------------------------------------
//  FilePicker
// ---------------------------------------------------------

FilePicker::FilePicker(const QString &title, const QString &filter, bool save, QWidget *parent) :
	QWidget(parent), m_title(title), m_filter(filter), m_save(save)
{
	auto *layout = new QHBoxLayout(this);
	layout->setContentsMargins(0, 0, 0, 0);
	m_edit = new QLineEdit;
	auto *btn = new QPushButton(tr("Browse..."));
	layout->addWidget(m_edit, 1);
	layout->addWidget(btn);

	connect(btn, &QPushButton::clicked, this, [this]() {
		QString path;
		if (m_save) {
			path = getSaveFileName(this, m_title, m_filter);
		} else {
			path = getOpenFileName(this, m_title, m_filter);
		}
		if (!path.isEmpty()) {
			m_edit->setText(path);
		}
	});
}

QString FilePicker::path() const { return m_edit->text(); }

void FilePicker::setPath(const QString &p) { m_edit->setText(p); }


// ---------------------------------------------------------
//  UserDialog
// ---------------------------------------------------------

UserDialog::UserDialog(QWidget *parent, Runtime &rt, const Json &js) :
	QDialog(parent, Qt::WindowTitleHint | Qt::WindowSystemMenuHint | Qt::WindowCloseButtonHint),
	m_runtime(rt)
{
	m_current_layout = new QVBoxLayout(this);
	m_current_layout->setContentsMargins(10, 10, 10, 10);
	bool yes_no = parse(js);
	m_current_layout->addStretch();
	m_current_layout->addSpacing(10);
	addButtons(yes_no);
}

UserDialog::UserDialog(QWidget *parent, Runtime &rt, const String &str) :
	UserDialog(parent, rt, rt.do_string(str))
{
}

void UserDialog::addButtons(bool yes_no)
{
	auto *layout = new QHBoxLayout;
	layout->addStretch();

	auto *cancel_btn = new QPushButton(yes_no ? tr("No") : tr("Cancel"));
	auto *ok_btn = new QPushButton(yes_no ? tr("Yes") : tr("OK"));
	ok_btn->setDefault(true);

	layout->addWidget(cancel_btn);
	layout->addSpacing(10);
	layout->addWidget(ok_btn);

	m_current_layout->addLayout(layout);

	connect(ok_btn, &QPushButton::clicked, this, &QDialog::accept);
	connect(cancel_btn, &QPushButton::clicked, this, &QDialog::reject);
}

Variant UserDialog::getResult() const
{
	Json::Object js;

	for (auto &[name, cb] : m_checkboxes) {
		js[name] = cb->isChecked();
	}

	for (auto &[name, box] : m_comboboxes) {
		js[name] = (intptr_t)(box->currentIndex() + 1);
	}

	for (auto &[name, line] : m_fields) {
		js[name] = String(line->text().toUtf8().constData());
	}

	for (auto &[name, list] : m_checklists) {
		Array<Variant> values;
		for (int i = 0; i < list->count(); i++) {
			if (list->item(i)->checkState() == Qt::Checked) {
				auto val = list->item(i)->data(Qt::UserRole).toString();
				values.append(String(val.toUtf8().constData()));
			}
		}
		js[name] = make_handle<List>(const_cast<Runtime *>(&m_runtime), std::move(values));
	}

	for (auto &[name, group] : m_radiogroups) {
		int checked = group->checkedId();
		js[name] = (intptr_t)(checked >= 0 ? checked + 1 : 0);
	}

	for (auto &[name, picker] : m_filepickers) {
		js[name] = String(picker->path().toUtf8().constData());
	}

	return Variant(make_handle<Table>(const_cast<Runtime *>(&m_runtime), std::move(js)));
}


// ---------------------------------------------------------
//  Parsing
// ---------------------------------------------------------

bool UserDialog::parse(const Json &js)
{
	if (!js.is_object()) {
		throw error("Invalid JSON object passed to create_dialog()");
	}
	bool yes_no = false;

	auto it = js.find("title");
	if (it != js.end()) {
		setWindowTitle(QString::fromUtf8(it.get_string().data(), (int) it.get_string().size()));
	}

	int width = -1, height = -1;
	it = js.find("width");
	if (it != js.end()) {
		width = (int) it.get_integer();
	}
	it = js.find("height");
	if (it != js.end()) {
		height = (int) it.get_integer();
	}
	if (width > 0 || height > 0) {
		resize(width > 0 ? width : this->width(), height > 0 ? height : this->height());
	}

	it = js.find("yes_no");
	if (it != js.end()) {
		yes_no = it.get_boolean();
	}

	it = js.find("items");
	if (it != js.end()) {
		for (auto &item : it.get_array()) {
			parseItem(item);
		}
	}

	return yes_no;
}

void UserDialog::parseItem(Json item)
{
	if (!item.is_object()) {
		throw error("User dialog items must be tables");
	}

	auto it = item.find("type");
	if (it == item.end()) {
		throw error("User dialog item has no \"type\" key");
	}
	auto type = it.get_string();

	if (type == "label") {
		addLabel(item);
	} else if (type == "button") {
		addButton(item);
	} else if (type == "check_box") {
		addCheckBox(item);
	} else if (type == "combo_box") {
		addComboBox(item);
	} else if (type == "field") {
		addLineEdit(item);
	} else if (type == "check_list") {
		addCheckList(item);
	} else if (type == "radio_buttons") {
		addRadioButtons(item);
	} else if (type == "file_selector") {
		addFileSelector(item);
	} else if (type == "container") {
		addContainer(item);
	} else if (type == "stretch") {
		m_current_layout->addStretch();
	} else if (type == "spacing") {
		addSpacing(item);
	} else {
		throw error("Unknown item type in user dialog: \"%\"", type);
	}
}

String UserDialog::getName(const Json &item)
{
	auto it = item.find("name");
	if (it == item.end()) {
		throw error("User dialog item has no \"name\" attribute");
	}
	return it.get_string();
}

void UserDialog::add(QWidget *widget)
{
	m_current_layout->addWidget(widget);
}


// ---------------------------------------------------------
//  Widget builders
// ---------------------------------------------------------

void UserDialog::addLabel(const Json &item)
{
	auto it = item.find("text");
	if (it == item.end()) {
		throw error("User dialog label has no \"text\" attribute");
	}
	auto text = it.get_string();
	add(new QLabel(QString::fromUtf8(text.data(), (int) text.size())));
}

void UserDialog::addButton(const Json &item)
{
	auto it = item.find("label");
	if (it == item.end()) {
		throw error("User dialog button has no \"label\" attribute");
	}
	auto label = it.get_string();
	auto *btn = new QPushButton(QString::fromUtf8(label.data(), (int) label.size()));

	it = item.find("action");
	if (it != item.end())
	{
		String action = it.get_string();
		connect(btn, &QPushButton::clicked, this, [this, action]() {
			m_runtime.do_string(action);
		});
	}

	String pos;
	it = item.find("position");
	if (it != item.end()) {
		pos = it.get_string();
	}

	auto *hl = new QHBoxLayout;
	if (pos == "right" || pos == "center") {
		hl->addStretch();
	}
	hl->addWidget(btn);
	if (pos == "left" || pos == "center") {
		hl->addStretch();
	}
	m_current_layout->addLayout(hl);
}

void UserDialog::addCheckBox(const Json &item)
{
	auto name = getName(item);
	String text;
	auto it = item.find("text");
	if (it != item.end()) {
		text = it.get_string();
	}

	auto *cb = new QCheckBox(QString::fromUtf8(text.data(), (int) text.size()));
	m_checkboxes[name] = cb;

	it = item.find("default");
	if (it != item.end()) {
		cb->setChecked(it.get_boolean());
	}

	add(cb);
}

void UserDialog::addComboBox(const Json &item)
{
	auto name = getName(item);

	auto it = item.find("values");
	if (it == item.end()) {
		throw error("User dialog combo box has no \"values\" attribute");
	}
	auto values = it.value();
	if (!values.is_array()) {
		throw error("Values should be a list");
	}

	auto *box = new QComboBox;
	for (auto &value : values.get_array()) {
		auto s = cast<String>(value);
		box->addItem(QString::fromUtf8(s.data(), (int) s.size()));
	}

	it = item.find("default");
	if (it != item.end()) {
		int idx = (int)(it.get_integer() - 1);
		if (idx >= 0 && idx < box->count()) {
			box->setCurrentIndex(idx);
		}
	}

	m_comboboxes[name] = box;
	add(box);
}

void UserDialog::addLineEdit(const Json &item)
{
	auto name = getName(item);
	auto *line = new QLineEdit;

	auto it = item.find("default");
	if (it != item.end()) {
		auto s = it.get_string();
		line->setText(QString::fromUtf8(s.data(), (int) s.size()));
	}

	m_fields[name] = line;
	add(line);
}

void UserDialog::addCheckList(const Json &item)
{
	auto name = getName(item);

	auto it = item.find("values");
	if (it == item.end()) {
		throw error("User dialog check list has no \"values\" attribute");
	}
	auto values = it.value();
	if (!values.is_array()) {
		throw error("\"values\" in checklist must be a list");
	}

	// Collect values (used as the return data for checked items).
	QStringList value_strings;
	for (auto &v : values.get_array()) {
		auto s = cast<String>(v);
		value_strings << QString::fromUtf8(s.data(), (int) s.size());
	}

	// Labels are optional; if absent, values are used as labels.
	QStringList label_strings;
	it = item.find("labels");
	if (it != item.end())
	{
		if (!it.value().is_array()) {
			throw error("\"labels\" in checklist must be a list");
		}
		for (auto &lbl : it.get_array()) {
			auto s = cast<String>(lbl);
			label_strings << QString::fromUtf8(s.data(), (int) s.size());
		}
	}
	else
	{
		label_strings = value_strings;
	}

	if (label_strings.size() != value_strings.size()) {
		throw error("Inconsistent number of labels and values in user dialog list box");
	}

	auto *list = new QListWidget;
	list->setMaximumHeight(150);
	for (int i = 0; i < label_strings.size(); i++)
	{
		auto *item_w = new QListWidgetItem(label_strings[i]);
		item_w->setFlags(item_w->flags() | Qt::ItemIsUserCheckable);
		item_w->setCheckState(Qt::Unchecked);
		item_w->setData(Qt::UserRole, value_strings[i]);
		if (label_strings[i] != value_strings[i]) {
			item_w->setToolTip(value_strings[i]);
		}
		list->addItem(item_w);
	}

	m_checklists[name] = list;
	add(list);
}

void UserDialog::addRadioButtons(const Json &item)
{
	auto name = getName(item);

	String title;
	auto it = item.find("title");
	if (it != item.end()) {
		title = it.get_string();
	}

	auto values_it = item.find("values");
	if (values_it == item.end()) {
		throw error("User dialog radio button group has no \"values\" attribute");
	}
	if (!values_it.value().is_array()) {
		throw error("\"values\" must be a list in radio buttons");
	}

	int sel = 0;
	it = item.find("default");
	if (it != item.end()) {
		sel = (int)(it.get_integer() - 1);
	}

	auto *group_box = new QGroupBox(QString::fromUtf8(title.data(), (int) title.size()));
	auto *layout = new QVBoxLayout(group_box);
	auto *button_group = new QButtonGroup(this);

	int idx = 0;
	for (auto &v : values_it.get_array())
	{
		auto s = cast<String>(v);
		auto *rb = new QRadioButton(QString::fromUtf8(s.data(), (int) s.size()));
		button_group->addButton(rb, idx);
		layout->addWidget(rb);
		if (idx == sel) {
			rb->setChecked(true);
		}
		idx++;
	}

	m_radiogroups[name] = button_group;
	add(group_box);
}

void UserDialog::addFileSelector(const Json &item)
{
	auto name = getName(item);

	auto it = item.find("title");
	if (it == item.end()) {
		throw error("User dialog file selector has no \"title\" attribute");
	}
	auto title = it.get_string();
	auto qtitle = QString::fromUtf8(title.data(), (int) title.size());

	QString text, filter;
	it = item.find("default");
	if (it != item.end()) {
		auto s = it.get_string();
		text = QString::fromUtf8(s.data(), (int) s.size());
	}
	it = item.find("filter");
	if (it != item.end()) {
		auto s = it.get_string();
		filter = QString::fromUtf8(s.data(), (int) s.size());
	}

	bool save = false;
	it = item.find("save");
	if (it != item.end()) {
		save = it.get_boolean();
	}

	auto *picker = new FilePicker(qtitle, filter, save);
	if (!text.isEmpty()) {
		picker->setPath(text);
	}

	m_filepickers[name] = picker;
	add(picker);
}

void UserDialog::addContainer(const Json &item)
{
	auto *previous_layout = m_current_layout;

	auto it = item.find("orientation");
	if (it != item.end() && it.get_string() == "vertical") {
		m_current_layout = new QVBoxLayout;
	} else {
		m_current_layout = new QHBoxLayout;
	}
	m_current_layout->setContentsMargins(0, 0, 0, 0);

	it = item.find("items");
	if (it == item.end()) {
		throw error("User dialog container has no \"items\" attribute");
	}
	for (auto &itm : it.get_array()) {
		parseItem(std::move(itm));
	}

	auto *nested = m_current_layout;
	m_current_layout = previous_layout;
	m_current_layout->addLayout(nested);
}

void UserDialog::addSpacing(const Json &item)
{
	auto it = item.find("size");
	if (it == item.end()) {
		throw error("User dialog spacing has no \"size\" attribute");
	}
	m_current_layout->addSpacing((int) it.get_integer());
}

} // namespace phonometrica
