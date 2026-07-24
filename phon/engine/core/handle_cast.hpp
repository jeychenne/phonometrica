// Phonometrica engine — handle_cast<D>(Handle<B>): safe up/down casts for boxed
// application classes (embedding, §11.5).
// Copyright (C) 2019-2026 Julien Eychenne. GPLv3 (see LICENSE).
//
// Casts a Handle between two classes related by single, non-virtual inheritance —
// the constraint every add_class<T> hierarchy satisfies (DEVIATIONS "Embedding
// transparency"; MIGRATION_NOTES step 5, roadmap E4/G5). This is the new-engine
// mirror of the old poly-box seam's Handle conversions
// (phon/runtime/typed_object.hpp) that the app cutover (A2) replaces `recast<T>`
// with.
//
//   * Upcast (D is a base of B, or D == B): always valid, no runtime check. The
//     boxed base subobject shares the payload address (offset 0) and the box
//     offset is alignment-only, so it is identical for B and D. Delegates to the
//     retaining converting/copy constructor.
//   * Downcast (B is a strict base of D): checked against the cell's *dynamic*
//     class through the engine class registry (is_a on the class id), not C++
//     RTTI — the registry is the source of truth for a boxed value's dynamic
//     class and works for non-polymorphic plain classes too. Returns an empty
//     Handle when the object is not actually a D (the safe replacement for the
//     old unchecked `recast<T>`, which silently produced a wrong-typed handle).
//
// A null input always yields a null result.

#ifndef PHON_CORE_HANDLE_CAST_HPP
#define PHON_CORE_HANDLE_CAST_HPP

#include <phon/engine/core/handle.hpp>
#include <phon/engine/object/class.hpp>
#include <type_traits>

namespace phonometrica {

template<class D, class B>
Handle<D> handle_cast(const Handle<B> &h) noexcept
{
	static_assert(std::is_base_of_v<D, B> || std::is_base_of_v<B, D>,
	              "handle_cast requires D and B to be related by inheritance");
	static_assert(box_value_offset<D>() == box_value_offset<B>(),
	              "handle_cast requires an identical box offset (equal alignment); the "
	              "hierarchy must use single, non-virtual inheritance");

	if constexpr (std::is_base_of_v<D, B>)
	{
		// Upcast (includes D == B): the retaining converting/copy constructor.
		return Handle<D>(h);
	}
	else
	{
		B *b = h.get();
		if (!b)
			return Handle<D>();
		Class *want = class_of<D>();
		Class *dyn = get_class(h.cell()->class_id());
		if (want && dyn && is_a(dyn, want))
			return Handle<D>(static_cast<D *>(b)); // address-identical; retaining ctor
		return Handle<D>();
	}
}

} // namespace phonometrica

#endif // PHON_CORE_HANDLE_CAST_HPP
