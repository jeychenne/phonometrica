// Phonometrica engine — standard library registration (architecture §12).
// Copyright (C) 2019-2026 Julien Eychenne. GPLv3 (see LICENSE).
//
// One registration unit per module (mirroring the old engine's func_*.hpp split),
// each written against the typed registration front end (runtime/native_traits.hpp).
// init_runtime() calls these once at process start; they add methods to the shared
// builtin generics, so the functions are flat-global (no per-module namespace — the
// stdlib is always available, like the old engine's builtins).

#ifndef PHON_LIB_LIB_HPP
#define PHON_LIB_LIB_HPP

namespace phonometrica {

void register_math_lib();   // sin/cos/…, abs/round/min/max, PI/E
void register_string_lib(); // find/count/contains/…, in-place trim/append/replace/…
void register_list_lib();   // contains/find/join/…, in-place append/pop/reverse/…
void register_array_lib();  // zeros/ones, sum/mean/min/max, elementwise sqrt/sin/…

} // namespace phonometrica

#endif // PHON_LIB_LIB_HPP
