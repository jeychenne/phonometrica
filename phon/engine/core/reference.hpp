// Phonometrica engine — reading through a first-class reference.
// Copyright (C) 2019-2026 Julien Eychenne. GPLv3 (see LICENSE).
//
// `deref` is the single-branch ZVAL_DEREF of design/references.md §5: it turns a
// reference Value into the value it currently stands for. It lives in this low
// header (rather than vm/function.hpp) so every layer — dispatch, containers,
// value_ops — can read through a reference without depending on the VM. The box
// itself is vm/function.hpp's UpvalueCell; only its `slot` pointer is needed here,
// and that sits immediately after the Cell header (a static_assert in
// vm/function.hpp keeps the two layouts in agreement).

#ifndef PHON_CORE_REFERENCE_HPP
#define PHON_CORE_REFERENCE_HPP

#include <phon/engine/core/cell.hpp>
#include <phon/engine/core/value.hpp>

namespace phonometrica {

// Layout-compatible prefix of the reference box: the value it stands for is *slot.
struct RefBoxView
{
	Cell header;
	Value *slot;
};

// Read through a first-class reference (identity for a non-reference). One predicted
// branch; an indirection only for actual references.
PHON_FORCE_INLINE Value deref(Value v) noexcept
{
	return v.is_reference() ? *reinterpret_cast<const RefBoxView *>(v.as_reference_box())->slot
	                        : v;
}

} // namespace phonometrica

#endif // PHON_CORE_REFERENCE_HPP
