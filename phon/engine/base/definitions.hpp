// Phonometrica engine — common definitions, assertions, and compiler intrinsics.
// Copyright (C) 2019-2026 Julien Eychenne. GPLv3 (see LICENSE).
//
// This header is the root of the base layer (layer 1). Everything below includes
// it; it includes nothing from the engine. All sizes and indices in the engine
// are intptr_t (see [INVARIANT] §16.2 of design/architecture.md).

#ifndef PHON_BASE_DEFINITIONS_HPP
#define PHON_BASE_DEFINITIONS_HPP

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>

// ---------------------------------------------------------------------------
// Compiler intrinsics
// ---------------------------------------------------------------------------

#if defined(__GNUC__) || defined(__clang__)
#	define PHON_LIKELY(x)   __builtin_expect(!!(x), 1)
#	define PHON_UNLIKELY(x) __builtin_expect(!!(x), 0)
#	define PHON_FORCE_INLINE inline __attribute__((always_inline))
#	define PHON_NOINLINE __attribute__((noinline))
#	define PHON_UNREACHABLE() __builtin_unreachable()
#elif defined(_MSC_VER)
#	define PHON_LIKELY(x)   (x)
#	define PHON_UNLIKELY(x) (x)
#	define PHON_FORCE_INLINE __forceinline
#	define PHON_NOINLINE __declspec(noinline)
#	define PHON_UNREACHABLE() __assume(0)
#else
#	define PHON_LIKELY(x)   (x)
#	define PHON_UNLIKELY(x) (x)
#	define PHON_FORCE_INLINE inline
#	define PHON_NOINLINE
#	define PHON_UNREACHABLE() ((void)0)
#endif

#define PHON_UNUSED(x) ((void)(x))

// Non-copyable helper mixin (private inheritance).
#define PHON_DISABLE_COPY(Class)          \
	Class(const Class &) = delete;        \
	Class &operator=(const Class &) = delete;

namespace phonometrica {

// ---------------------------------------------------------------------------
// Failure reporting
// ---------------------------------------------------------------------------

[[noreturn]] PHON_NOINLINE inline void
detail_fail(const char *kind, const char *file, int line, const char *expr, const char *msg)
{
	std::fprintf(stderr, "%s at %s:%d\n    %s\n", kind, file, line, expr);
	if (msg && *msg)
		std::fprintf(stderr, "    %s\n", msg);
	std::fflush(stderr);
	std::abort();
}

} // namespace phonometrica

// PHON_CHECK: always-on invariant with a message. Use for conditions that must
// hold in release builds (OOM, API misuse, corrupt state).
#define PHON_CHECK(cond, msg)                                                   \
	(PHON_LIKELY(cond) ? (void) 0                                               \
	                   : ::phonometrica::detail_fail("Check failed", __FILE__,  \
	                                                 __LINE__, #cond, (msg)))

// PHON_UNREACHABLE_MSG: an always-on assertion that a point is not reached.
#define PHON_UNREACHABLE_MSG(msg)                                               \
	::phonometrica::detail_fail("Unreachable", __FILE__, __LINE__, "", (msg))

// PHON_ASSERT: debug-only invariant check (compiled out under NDEBUG).
#ifndef NDEBUG
#	define PHON_ASSERT(cond)                                                    \
		(PHON_LIKELY(cond) ? (void) 0                                          \
		                   : ::phonometrica::detail_fail("Assertion failed",   \
		                                                 __FILE__, __LINE__,   \
		                                                 #cond, nullptr))
#	define PHON_ASSERT_MSG(cond, msg)                                           \
		(PHON_LIKELY(cond) ? (void) 0                                          \
		                   : ::phonometrica::detail_fail("Assertion failed",   \
		                                                 __FILE__, __LINE__,   \
		                                                 #cond, (msg)))
#	define PHON_DEBUG_BUILD 1
#else
#	define PHON_ASSERT(cond) ((void) 0)
#	define PHON_ASSERT_MSG(cond, msg) ((void) 0)
#	define PHON_DEBUG_BUILD 0
#endif

#endif // PHON_BASE_DEFINITIONS_HPP
