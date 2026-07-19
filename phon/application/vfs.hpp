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
 * Created: 28/02/2019                                                                                                 *
 *                                                                                                                     *
 * Purpose: Element (formerly VNode) is the base class for all the elements in a project's virtual file system. All    *
 * files displayed in the file manager and stored in a project are subclasses of this class.                           *
 * Note that some operations can be mutating or non-mutating, depending on where they are triggered. For instance,     *
 * when a project is loaded, files are appended to a folder silently, but if a user explicitly adds a file to a folder *
 * then the folder must register the mutation. This behavior is controlled by a Boolean flag.                          *
 *                                                                                                                     *
 ***********************************************************************************************************************/

#ifndef PHONOMETRICA_VFS_HPP
#define PHONOMETRICA_VFS_HPP

#include <vector>
#include <phon/runtime/typed_object.hpp>
#include <phon/runtime/class.hpp>
#include <phon/application/property.hpp>
#include <phon/utils/xml.hpp>
#include <phon/utils/signal.hpp>

namespace phonometrica {

enum class FileType
{
	Annotation = 1,
	Sound = 2,
	Query = 4,
	Script = 8,
	Dataset = 16,
	Note = 32,
	CorpusFile = Annotation|Sound,
	Any = Annotation|Sound|Query|Script|Dataset|Note
};


class Element;
class Directory;
class Document;
class DataTable;
class Dataset;
class Concordance;
class Annotation;
class Sound;
class Spectrum;
class Script;
class Note;
class Query;
class FormantQuery;
class PitchQuery;
class IntensityQuery;
class SpectralMomentsQuery;
class VoiceQualityQuery;
class Bookmark;
class TimeStamp;
class Analysis;

// The VFS hierarchy consists of plain C++ classes (no engine base); script exposure goes through
// the old runtime's poly boxing (TPolyObject in phon/runtime/typed_object.hpp). This block is the
// single cell-ness seam: it declares every class of the boxed hierarchy, and must list each one —
// a missing entry makes Handle<T> fall back to the non-polymorphic TObject<T> box.
// The specializations must precede any Handle<T> instantiation, which is why they all live here,
// before the class definitions.
namespace traits {
template<> struct hierarchy_root<Element> { using type = Element; };
template<> struct hierarchy_root<Directory> { using type = Element; };
template<> struct hierarchy_root<Document> { using type = Element; };
template<> struct hierarchy_root<DataTable> { using type = Element; };
template<> struct hierarchy_root<Dataset> { using type = Element; };
template<> struct hierarchy_root<Concordance> { using type = Element; };
template<> struct hierarchy_root<Annotation> { using type = Element; };
template<> struct hierarchy_root<Sound> { using type = Element; };
template<> struct hierarchy_root<Spectrum> { using type = Element; };
template<> struct hierarchy_root<Script> { using type = Element; };
template<> struct hierarchy_root<Note> { using type = Element; };
template<> struct hierarchy_root<Query> { using type = Element; };
template<> struct hierarchy_root<FormantQuery> { using type = Element; };
template<> struct hierarchy_root<PitchQuery> { using type = Element; };
template<> struct hierarchy_root<IntensityQuery> { using type = Element; };
template<> struct hierarchy_root<SpectralMomentsQuery> { using type = Element; };
template<> struct hierarchy_root<VoiceQualityQuery> { using type = Element; };
template<> struct hierarchy_root<Bookmark> { using type = Element; };
template<> struct hierarchy_root<TimeStamp> { using type = Element; };
template<> struct hierarchy_root<Analysis> { using type = Element; };
}


class Element
{
public:

	explicit Element(Directory *parent);

	virtual ~Element() = default;

	// Name the class was registered under with the scripting engine ("Annotation", "Sound", ...).
	// Used for project serialization: the returned strings are part of the project XML format.
	virtual String class_name() const = 0;

	void detach(bool mutate = true);

	void set_parent(Directory *parent, bool mutate = true);

	virtual String label() const = 0;

	virtual bool modified() const;

	template<typename T>
	bool is() const { return dynamic_cast<const T*>(this) != nullptr; }

	virtual void to_xml(xml_node root) = 0;

	void move_to(Directory *parent, intptr_t pos);

	Directory *parent() const { return m_parent; }

	virtual const Directory *toplevel() const;

	virtual void discard_changes();

	void set_content_modified(bool value) { m_content_modified = value; }

	virtual bool contains(const Element *node) const { return false; }

	virtual bool quick_search(const String &text) const { return true; }

	static Signal<const String &, const String &, int> request_progress;

	static Signal<int> update_progress;

protected:

	// Non-owning reference to the parent.
	Directory *m_parent = nullptr;

	bool m_content_modified = false;

};

using ElementList = Array<Handle<Element>>;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

class Directory final : public Element
{
public:

	typedef ElementList::iterator iterator;
	typedef ElementList::const_iterator const_iterator;
	typedef ElementList::reverse_iterator reverse_iterator;
	typedef ElementList::const_reverse_iterator const_reverse_iterator;

	Directory(Directory *parent, String label);

	iterator begin() noexcept;
	const_iterator begin() const noexcept;

	iterator end() noexcept;
	const_iterator end() const noexcept;

	reverse_iterator rbegin() noexcept;
	const_reverse_iterator rbegin() const noexcept;

	reverse_iterator rend() noexcept;
	const_reverse_iterator rend() const noexcept;

	String label() const override;

	String class_name() const override { return "Directory"; }

	void set_label(String value);

	bool empty() const;

	Handle<Element> &get(intptr_t i);
	const Handle<Element> &get(intptr_t i) const;

	void append(Handle<Element> node, bool mutate = true);

	void insert(intptr_t pos, Handle<Element> node);

	void remove(const Handle<Element> &node, bool mutate = true);

	bool modified() const override;

	void discard_changes() override;

	void to_xml(xml_node root) override;

	intptr_t size() const;

	void save_content();

	bool expanded() const;

	void set_expanded(bool value);

	void clear(bool mutate = true);

    const Directory *toplevel() const override;

    void add_subfolder(const String &name);

	bool contains(const Element *node) const override;

	bool quick_search(const String &text) const override;

	void sort();

protected:

    void set_modified(bool value);

	String m_label;

	ElementList m_content;

	bool m_expanded = false;

};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

class Document : public Element
{
public:

	Document(Directory *parent, String path);

	String label() const override;

	// Label for the file browser (with extension when no custom label).
	virtual String browser_label() const;

	bool has_path() const;

	const String &path() const;

	virtual void set_path(String path, bool mutate);

	void open();

	void save();

	void reload();

	bool loaded() const;

	bool modified() const override;

	void add_property(Property p, bool mutate = true);

	bool remove_property(const Property &p);

	bool remove_property(const String &category);

	String get_property_value(const String &category) const;

	Property get_property(const String &category) const;

	const String &description() const;

	void set_description(String value, bool mutate = true);

	void to_xml(xml_node root) final;

	const std::set<Property> &properties() const;

	bool has_category(const String &category) const;

	void remove_property(const String &category, const String &value);

	bool has_properties() const;

	Array<String> property_list() const;

	bool quick_search(const String &text) const override;

	bool anchored() const;

	static void initialize(Runtime &rt);

	// Send this signal to notify the current view that the file has been modified
	static Signal<> file_modified;

	// Metadata serialization — public so that Project can call metadata_from_xml
	// when loading inline metadata from the project file.
	virtual void metadata_to_xml(xml_node meta_node);
	virtual void metadata_from_xml(xml_node meta_node);

	// Whether to_xml should emit a <Metadata> child node. The default checks for
	// properties or description; subclasses like Annotation override this to also
	// return true when a sound file is bound.
	virtual bool needs_metadata_node() const;

protected:

	virtual void load() = 0;

	virtual void write() = 0;

	virtual void save_metadata();

	virtual bool uses_external_metadata() const;

	virtual bool content_modified() const;

	Property parse_property(xml_node prop_node, const std::type_info &type);

	String m_path;

	std::set<Property> m_properties;

	String m_description;

	bool m_loaded = false;

	bool m_metadata_modified = false;
};

namespace traits {
}

using DocList = Array<Handle<Document>>;


} // namespace phonometrica

#endif // PHONOMETRICA_VFS_HPP
