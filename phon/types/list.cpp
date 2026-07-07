// Phonometrica engine — List implementation.
// Copyright (C) 2019-2026 Julien Eychenne. GPLv3 (see LICENSE).

#include <phon/types/list.hpp>

#include <phon/object/class.hpp>
#include <phon/object/value_ops.hpp>

#include <cstring>

namespace phonometrica {

namespace {

constexpr intptr_t LIST_HEADER = offsetof(ListCell, data);
constexpr intptr_t LIST_INITIAL_CAP = 8;

intptr_t list_capacity_for(intptr_t need)
{
	intptr_t cap = LIST_INITIAL_CAP;
	while (cap < need)
		cap = cap + cap / 2;
	return cap;
}

ListCell *list_alloc(intptr_t capacity)
{
	if (capacity < 1)
		capacity = 1;
	Cell *c = cell_alloc(CID_LIST, LIST_HEADER + capacity * static_cast<intptr_t>(sizeof(Value)));
	auto *l = reinterpret_cast<ListCell *>(c);
	l->size = 0;
	l->capacity = capacity;
	return l;
}

PHON_FORCE_INLINE void retain_value(Value v) noexcept
{
	if (v.owns_cell())
		retain(v.cell_ptr());
}
PHON_FORCE_INLINE void release_value(Value v) noexcept
{
	if (v.owns_cell())
		release(v.cell_ptr());
}

void list_finalize(Cell *c)
{
	auto *l = reinterpret_cast<ListCell *>(c);
	for (intptr_t i = 0; i < l->size; ++i)
		release_value(l->data[i]);
}

// Enumerate contained cells for the cycle collector (§8.2). The elements are inline,
// so there is no auxiliary buffer to free (no gc_free hook).
void list_trace(Cell *c, void (*visit)(Cell *))
{
	auto *l = reinterpret_cast<ListCell *>(c);
	for (intptr_t i = 0; i < l->size; ++i)
		if (l->data[i].owns_cell()) // an ordinary cell, or a reference box
			visit(l->data[i].cell_ptr());
}

bool list_equals_hook(const Cell *a, const Cell *b)
{
	auto *la = reinterpret_cast<const ListCell *>(a);
	auto *lb = reinterpret_cast<const ListCell *>(b);
	if (la->size != lb->size)
		return false;
	for (intptr_t i = 0; i < la->size; ++i)
		if (!value_equals(la->data[i], lb->data[i]))
			return false;
	return true;
}

// Clone a list cell with capacity `cap`, copying and retaining children.
ListCell *list_clone(ListCell *s, intptr_t cap)
{
	if (cap < s->size)
		cap = s->size;
	ListCell *c = list_alloc(cap);
	c->size = s->size;
	for (intptr_t i = 0; i < s->size; ++i)
	{
		Value v = s->data[i];
		retain_value(v);
		c->data[i] = v;
	}
	return c;
}

Class g_list_class;

} // namespace

void register_list_class()
{
	g_list_class.id = CID_LIST;
	g_list_class.name = "List";
	g_list_class.base = get_class(CID_OBJECT);
	// Lists can contain themselves -> NOT acyclic (cycle collector wired in M5).
	g_list_class.flags = CLASS_BUILTIN | CLASS_VALUE;
	g_list_class.instance_size = -1;
	g_list_class.finalize = &list_finalize;
	g_list_class.trace = &list_trace;
	g_list_class.equals = &list_equals_hook;
	register_class(&g_list_class);
}

// ---------------------------------------------------------------------------

List::List() : m_impl(Handle<ListCell>::adopt(list_alloc(LIST_INITIAL_CAP))) {}

List::List(intptr_t size) : m_impl(Handle<ListCell>::adopt(list_alloc(size > 0 ? size : 1)))
{
	ListCell *l = m_impl.get();
	Value null = Value::make_null();
	for (intptr_t i = 0; i < size; ++i)
		l->data[i] = null;
	l->size = size;
}

List::List(std::initializer_list<Variant> items)
    : m_impl(Handle<ListCell>::adopt(list_alloc(list_capacity_for(static_cast<intptr_t>(items.size())))))
{
	for (const Variant &v : items)
		append(v);
}

List List::from_value(Value v) noexcept
{
	PHON_ASSERT(v.is_cell() && v.as_cell()->class_id() == CID_LIST);
	return List(Handle<ListCell>(reinterpret_cast<ListCell *>(v.as_cell())));
}

intptr_t List::normalize(intptr_t i, bool allow_end) const
{
	intptr_t n = m_impl->size;
	intptr_t idx = i;
	if (i < 0)
		idx = n + i + 1; // -1 -> last
	--idx;               // to 0-based
	intptr_t upper = allow_end ? n : n - 1;
	PHON_CHECK(idx >= 0 && idx <= upper, "[Index error] List index out of range");
	return idx;
}

ListCell *List::detach_for_write(intptr_t need)
{
	ListCell *s = m_impl.get();
	bool shared = !m_impl.unique();
	bool too_small = s->capacity < need;
	if (!shared && !too_small)
		return s;
	if (!shared)
	{
		intptr_t newcap = list_capacity_for(need);
		Cell *moved =
		    cell_realloc(&s->header, LIST_HEADER + newcap * static_cast<intptr_t>(sizeof(Value)));
		auto *ns = reinterpret_cast<ListCell *>(moved);
		ns->capacity = newcap;
		m_impl.reset_reallocated(ns);
		return ns;
	}
	ListCell *clone = list_clone(s, list_capacity_for(need));
	m_impl = Handle<ListCell>::adopt(clone);
	return clone;
}

Variant List::get(intptr_t i) const
{
	return Variant(m_impl->data[normalize(i)]);
}

void List::set(intptr_t i, const Variant &v)
{
	intptr_t idx = normalize(i);
	ListCell *l = detach_for_write(m_impl->size);
	Value nv = v.value();
	retain_value(nv);
	release_value(l->data[idx]);
	l->data[idx] = nv;
}

Variant &List::ref(intptr_t i)
{
	intptr_t idx = normalize(i);
	ListCell *l = detach_for_write(m_impl->size);
	return *reinterpret_cast<Variant *>(&l->data[idx]);
}

void List::append(const Variant &v)
{
	ListCell *l = detach_for_write(m_impl->size + 1);
	Value nv = v.value();
	retain_value(nv);
	l->data[l->size++] = nv;
}

void List::prepend(const Variant &v)
{
	insert(1, v);
}

void List::insert(intptr_t i, const Variant &v)
{
	intptr_t idx = normalize(i, /*allow_end=*/true);
	ListCell *l = detach_for_write(m_impl->size + 1);
	std::memmove(&l->data[idx + 1], &l->data[idx],
	             static_cast<size_t>(l->size - idx) * sizeof(Value));
	Value nv = v.value();
	retain_value(nv);
	l->data[idx] = nv;
	++l->size;
}

Variant List::pop()
{
	PHON_CHECK(m_impl->size > 0, "[Value error] pop from empty List");
	ListCell *l = detach_for_write(m_impl->size);
	Value v = l->data[l->size - 1];
	Variant result(v);  // retains
	release_value(v);    // drop the list's reference
	--l->size;
	return result;
}

void List::remove_at(intptr_t i)
{
	intptr_t idx = normalize(i);
	ListCell *l = detach_for_write(m_impl->size);
	release_value(l->data[idx]);
	std::memmove(&l->data[idx], &l->data[idx + 1],
	             static_cast<size_t>(l->size - idx - 1) * sizeof(Value));
	--l->size;
}

void List::clear()
{
	ListCell *l = detach_for_write(m_impl->size);
	for (intptr_t i = 0; i < l->size; ++i)
		release_value(l->data[i]);
	l->size = 0;
}

void List::reserve(intptr_t n)
{
	if (n > m_impl->capacity || !m_impl.unique())
		detach_for_write(n);
}

intptr_t List::index_of(const Variant &v) const
{
	ListCell *l = m_impl.get();
	Value target = v.value();
	for (intptr_t i = 0; i < l->size; ++i)
		if (value_equals(l->data[i], target))
			return i + 1;
	return 0;
}

bool List::operator==(const List &o) const noexcept
{
	if (m_impl.get() == o.m_impl.get())
		return true;
	return list_equals_hook(&m_impl->header, &o.m_impl->header);
}

} // namespace phonometrica
