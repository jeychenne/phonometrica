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
 * Purpose: see header. The dialog is built from a script Table specification (the new engine's Table/List values;    *
 * the pre-cutover implementation consumed the old engine's Json DOM). All values are read through the small           *
 * accessors below: Table::get is lenient (null when absent), so "required" keys check is_null and throw the same     *
 * user-facing errors as before.                                                                                       *
 *                                                                                                                     *
 ***********************************************************************************************************************/

#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <phon/error.hpp>
#include <phon/gui/file_dialog.hpp>
#include <phon/gui/user_dialog.hpp>

namespace phonometrica {

// ---------------------------------------------------------
//  Table access helpers
// ---------------------------------------------------------

static Variant tkey(const char *s)
{
	return Variant::make(String(s));
}

// Lenient fetch: null Variant when the key is absent.
static Variant getv(const Table &t, const char *k)
{
	return t.get(tkey(k));
}

static String require_string(const Table &t, const char *k, const char *what)
{
	auto v = getv(t, k);
	if (v.is_null()) {
		throw error("User dialog % has no \"%\" attribute", what, k);
	}
	return v.to<String>();
}

static String opt_string(const Table &t, const char *k)
{
	auto v = getv(t, k);
	return v.is_null() ? String() : v.to<String>();
}

static intptr_t opt_int(const Table &t, const char *k, intptr_t def)
{
	auto v = getv(t, k);
	return v.is_null() ? def : (intptr_t) v.to<int64_t>();
}

static bool opt_bool(const Table &t, const char *k, bool def)
{
	auto v = getv(t, k);
	return v.is_null() ? def : v.to<bool>();
}

static List require_list(const Table &t, const char *k, const char *what)
{
	auto v = getv(t, k);
	if (v.is_null()) {
		throw error("User dialog % has no \"%\" attribute", what, k);
	}
	if (!v.is<List>()) {
		throw error("\"%\" must be a list in user dialog %", k, what);
	}
	return v.to<List>();
}

// Evaluate a script string to a dialog specification.
static Table eval_spec(Runtime &rt, const String &str)
{
	auto v = rt.do_string(str);
	if (!v.is<Table>()) {
		throw error("create_dialog() expects a table specification");
	}
	return v.to<Table>();
}

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

UserDialog::UserDialog(QWidget *parent, Runtime &rt, const Table &spec) :
	QDialog(parent, Qt::WindowTitleHint | Qt::WindowSystemMenuHint | Qt::WindowCloseButtonHint),
	m_runtime(rt)
{
	m_current_layout = new QVBoxLayout(this);
	m_current_layout->setContentsMargins(10, 10, 10, 10);
	bool yes_no = parse(spec);
	m_current_layout->addStretch();
	m_current_layout->addSpacing(10);
	addButtons(yes_no);
}

UserDialog::UserDialog(QWidget *parent, Runtime &rt, const String &str) :
	UserDialog(parent, rt, eval_spec(rt, str))
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
	Table js;

	for (auto &[name, cb] : m_checkboxes) {
		js.set(Variant::make(name), Variant::make(cb->isChecked()));
	}

	for (auto &[name, box] : m_comboboxes) {
		js.set(Variant::make(name), Variant::make<int64_t>(box->currentIndex() + 1));
	}

	for (auto &[name, line] : m_fields) {
		js.set(Variant::make(name), Variant::make(String(line->text())));
	}

	for (auto &[name, list] : m_checklists) {
		List values;
		for (int i = 0; i < list->count(); i++) {
			if (list->item(i)->checkState() == Qt::Checked) {
				values.append(Variant::make(String(list->item(i)->data(Qt::UserRole).toString())));
			}
		}
		js.set(Variant::make(name), Variant::make(values));
	}

	for (auto &[name, group] : m_radiogroups) {
		int checked = group->checkedId();
		js.set(Variant::make(name), Variant::make<int64_t>(checked >= 0 ? checked + 1 : 0));
	}

	for (auto &[name, picker] : m_filepickers) {
		js.set(Variant::make(name), Variant::make(String(picker->path())));
	}

	return Variant::make(js);
}


// ---------------------------------------------------------
//  Parsing
// ---------------------------------------------------------

bool UserDialog::parse(const Table &spec)
{
	auto title = opt_string(spec, "title");
	if (!title.empty()) {
		setWindowTitle(title);
	}

	int width = (int) opt_int(spec, "width", -1);
	int height = (int) opt_int(spec, "height", -1);
	if (width > 0 || height > 0) {
		resize(width > 0 ? width : this->width(), height > 0 ? height : this->height());
	}

	bool yes_no = opt_bool(spec, "yes_no", false);

	auto items_v = getv(spec, "items");
	if (!items_v.is_null())
	{
		if (!items_v.is<List>()) {
			throw error("\"items\" must be a list in user dialog");
		}
		auto items = items_v.to<List>();
		for (intptr_t i = 1; i <= items.size(); i++)
		{
			auto item = items.get(i);
			if (!item.is<Table>()) {
				throw error("User dialog items must be tables");
			}
			parseItem(item.to<Table>());
		}
	}

	return yes_no;
}

void UserDialog::parseItem(const Table &item)
{
	auto type_v = getv(item, "type");
	if (type_v.is_null()) {
		throw error("User dialog item has no \"type\" key");
	}
	auto type = type_v.to<String>();

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

String UserDialog::getName(const Table &item)
{
	auto v = getv(item, "name");
	if (v.is_null()) {
		throw error("User dialog item has no \"name\" attribute");
	}
	return v.to<String>();
}

void UserDialog::add(QWidget *widget)
{
	m_current_layout->addWidget(widget);
}


// ---------------------------------------------------------
//  Widget builders
// ---------------------------------------------------------

void UserDialog::addLabel(const Table &item)
{
	add(new QLabel(QString(require_string(item, "text", "label"))));
}

void UserDialog::addButton(const Table &item)
{
	auto *btn = new QPushButton(QString(require_string(item, "label", "button")));

	auto action = opt_string(item, "action");
	if (!action.empty())
	{
		connect(btn, &QPushButton::clicked, this, [this, action]() {
			// A script error must not escape into the Qt event loop.
			try {
				m_runtime.do_string(action);
			}
			catch (std::exception &e) {
				QMessageBox::warning(this, tr("Script error"), QString::fromUtf8(e.what()));
			}
		});
	}

	auto pos = opt_string(item, "position");

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

void UserDialog::addCheckBox(const Table &item)
{
	auto name = getName(item);

	auto *cb = new QCheckBox(QString(opt_string(item, "text")));
	m_checkboxes[name] = cb;
	cb->setChecked(opt_bool(item, "default", false));

	add(cb);
}

void UserDialog::addComboBox(const Table &item)
{
	auto name = getName(item);
	auto values = require_list(item, "values", "combo box");

	auto *box = new QComboBox;
	for (intptr_t i = 1; i <= values.size(); i++) {
		box->addItem(QString(values.get(i).to<String>()));
	}

	int idx = (int) opt_int(item, "default", 0) - 1;
	if (idx >= 0 && idx < box->count()) {
		box->setCurrentIndex(idx);
	}

	m_comboboxes[name] = box;
	add(box);
}

void UserDialog::addLineEdit(const Table &item)
{
	auto name = getName(item);
	auto *line = new QLineEdit;

	auto text = opt_string(item, "default");
	if (!text.empty()) {
		line->setText(QString(text));
	}

	m_fields[name] = line;
	add(line);
}

void UserDialog::addCheckList(const Table &item)
{
	auto name = getName(item);
	auto values = require_list(item, "values", "check list");

	// Collect values (used as the return data for checked items).
	QStringList value_strings;
	for (intptr_t i = 1; i <= values.size(); i++) {
		value_strings << QString(values.get(i).to<String>());
	}

	// Labels are optional; if absent, values are used as labels.
	QStringList label_strings;
	auto labels_v = getv(item, "labels");
	if (!labels_v.is_null())
	{
		if (!labels_v.is<List>()) {
			throw error("\"labels\" must be a list in user dialog check list");
		}
		auto labels = labels_v.to<List>();
		for (intptr_t i = 1; i <= labels.size(); i++) {
			label_strings << QString(labels.get(i).to<String>());
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

void UserDialog::addRadioButtons(const Table &item)
{
	auto name = getName(item);
	auto values = require_list(item, "values", "radio button group");

	int sel = (int) opt_int(item, "default", 1) - 1;

	auto *group_box = new QGroupBox(QString(opt_string(item, "title")));
	auto *layout = new QVBoxLayout(group_box);
	auto *button_group = new QButtonGroup(this);

	for (intptr_t i = 1; i <= values.size(); i++)
	{
		int idx = (int) i - 1;
		auto *rb = new QRadioButton(QString(values.get(i).to<String>()));
		button_group->addButton(rb, idx);
		layout->addWidget(rb);
		if (idx == sel) {
			rb->setChecked(true);
		}
	}

	m_radiogroups[name] = button_group;
	add(group_box);
}

void UserDialog::addFileSelector(const Table &item)
{
	auto name = getName(item);
	auto qtitle = QString(require_string(item, "title", "file selector"));

	auto text = QString(opt_string(item, "default"));
	auto filter = QString(opt_string(item, "filter"));
	bool save = opt_bool(item, "save", false);

	auto *picker = new FilePicker(qtitle, filter, save);
	if (!text.isEmpty()) {
		picker->setPath(text);
	}

	m_filepickers[name] = picker;
	add(picker);
}

void UserDialog::addContainer(const Table &item)
{
	auto *previous_layout = m_current_layout;

	if (opt_string(item, "orientation") == "vertical") {
		m_current_layout = new QVBoxLayout;
	} else {
		m_current_layout = new QHBoxLayout;
	}
	m_current_layout->setContentsMargins(0, 0, 0, 0);

	auto items = require_list(item, "items", "container");
	for (intptr_t i = 1; i <= items.size(); i++)
	{
		auto itm = items.get(i);
		if (!itm.is<Table>()) {
			throw error("User dialog items must be tables");
		}
		parseItem(itm.to<Table>());
	}

	auto *nested = m_current_layout;
	m_current_layout = previous_layout;
	m_current_layout->addLayout(nested);
}

void UserDialog::addSpacing(const Table &item)
{
	auto v = getv(item, "size");
	if (v.is_null()) {
		throw error("User dialog spacing has no \"size\" attribute");
	}
	m_current_layout->addSpacing((int) v.to<int64_t>());
}

} // namespace phonometrica
