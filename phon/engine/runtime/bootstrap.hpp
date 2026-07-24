// Phonometrica engine — runtime bootstrap.
// Copyright (C) 2019-2026 Julien Eychenne. GPLv3 (see LICENSE).
//
// Registers the builtin classes and their hooks. Idempotent and safe to call
// from any entry point (tests, embedding API). In M1 this wires the primitive
// classes plus String/List/Map/Set; later milestones extend it.

#ifndef PHON_RUNTIME_BOOTSTRAP_HPP
#define PHON_RUNTIME_BOOTSTRAP_HPP

namespace phonometrica {

// Register builtin classes if not already done. Call once before creating any
// engine objects (calling more than once is a no-op).
void bootstrap();

} // namespace phonometrica

#endif // PHON_RUNTIME_BOOTSTRAP_HPP
