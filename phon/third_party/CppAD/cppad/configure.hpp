// Phonometrica-specific CppAD configuration.
// This replaces the CMake-generated configure.hpp with compile-time
// preprocessor detection.  Only the core AD functionality is needed;
// optional backends (ADOLC, ColPack, IPOPT) are disabled.

#ifndef CPPAD_CONFIGURE_HPP
#define CPPAD_CONFIGURE_HPP

// ---------------------------------------------------------------------------
// Platform detection
// ---------------------------------------------------------------------------

#define CPPAD_LINK_FLAGS_HAS_M32 0

#if defined(__GNUC__) || defined(__clang__)
#  define CPPAD_COMPILER_HAS_CONVERSION_WARN 1
#  define CPPAD_C_COMPILER_GNU_FLAGS         1
#  define CPPAD_C_COMPILER_MSVC_FLAGS        0
#  define CPPAD_C_COMPILER_CMD               "cc"
#else
#  define CPPAD_COMPILER_HAS_CONVERSION_WARN 0
#  define CPPAD_C_COMPILER_GNU_FLAGS         0
#  if defined(_MSC_VER)
#    define CPPAD_C_COMPILER_MSVC_FLAGS      1
#    define CPPAD_C_COMPILER_CMD             "cl"
#  else
#    define CPPAD_C_COMPILER_MSVC_FLAGS      0
#    define CPPAD_C_COMPILER_CMD             "cc"
#  endif
#endif

// MSVC warnings
#define CPPAD_DISABLE_SOME_MICROSOFT_COMPILER_WARNINGS 1
#if _MSC_VER
#  pragma warning( disable : 4100 )
#  pragma warning( disable : 4127 )
#  pragma warning( disable : 4723 )
#endif
#undef CPPAD_DISABLE_SOME_MICROSOFT_COMPILER_WARNINGS

// ---------------------------------------------------------------------------
// C++ standard support (Phonometrica requires C++20)
// ---------------------------------------------------------------------------

#define CPPAD_USE_CPLUSPLUS_2011 1
#define CPPAD_USE_CPLUSPLUS_2017 1

// ---------------------------------------------------------------------------
// Debug
// ---------------------------------------------------------------------------

#define CPPAD_DEBUG_AND_RELEASE 0

// ---------------------------------------------------------------------------
// Optional dependencies — none used by Phonometrica
// ---------------------------------------------------------------------------

#define CPPAD_HAS_ADOLC   0
#define CPPAD_HAS_COLPACK 0
#define CPPAD_HAS_IPOPT   0

// Eigen IS available (Phonometrica vendors it)
#define CPPAD_HAS_EIGEN   1

// ---------------------------------------------------------------------------
// Test vector type — use std::vector
// ---------------------------------------------------------------------------

#define CPPAD_BOOSTVECTOR  0
#define CPPAD_CPPADVECTOR  0
#define CPPAD_EIGENVECTOR  0
#define CPPAD_STDVECTOR    1

// ---------------------------------------------------------------------------
// Version string
// ---------------------------------------------------------------------------

#define CPPAD_PACKAGE_STRING "cppad-20260103"

// ---------------------------------------------------------------------------
// Deprecated compatibility macros
// ---------------------------------------------------------------------------

#define CPPAD_DEPRECATED 0
#define CPPAD_NULL       nullptr
#define CPPAD_NOEXCEPT   noexcept

#ifdef NDEBUG
#  define CPPAD_NDEBUG_NOEXCEPT noexcept
#else
#  define CPPAD_NDEBUG_NOEXCEPT
#endif

// ---------------------------------------------------------------------------
// Tape configuration
// ---------------------------------------------------------------------------

// Address and ID types for the tape.  size_t is the safe default.
#define CPPAD_TAPE_ADDR_TYPE size_t
#define CPPAD_TAPE_ID_TYPE   size_t
#define CPPAD_IS_SAME_TAPE_ADDR_TYPE_SIZE_T 1

#ifndef CPPAD_MAX_NUM_THREADS
#  define CPPAD_MAX_NUM_THREADS 4
#endif

// ---------------------------------------------------------------------------
// Platform-specific system calls
// ---------------------------------------------------------------------------

#if defined(_MSC_VER)
#  define CPPAD_HAS_GETTIMEOFDAY  0
#  define CPPAD_HAS_MKSTEMP       0
#  define CPPAD_HAS_TMPNAM_S      1
#else
#  define CPPAD_HAS_GETTIMEOFDAY  1
#  define CPPAD_HAS_MKSTEMP       1
#  define CPPAD_HAS_TMPNAM_S      0
#endif

// ---------------------------------------------------------------------------
// unsigned int vs size_t
// ---------------------------------------------------------------------------

#if defined(__LP64__) || defined(_WIN64) || defined(__x86_64__) || defined(__aarch64__)
   // 64-bit: unsigned int (4 bytes) != size_t (8 bytes)
#  define CPPAD_IS_SAME_UNSIGNED_INT_SIZE_T 0
#else
   // 32-bit: both are typically 4 bytes
#  define CPPAD_IS_SAME_UNSIGNED_INT_SIZE_T 1
#endif

// ---------------------------------------------------------------------------
// block_t padding for thread_alloc
//
// block_t contains { size_t, size_t, void* }.
// On 64-bit: 24 bytes, already a multiple of sizeof(double) = 8.  No padding.
// On 32-bit: 12 bytes, needs 4 bytes of padding to reach 16.
// ---------------------------------------------------------------------------

#if defined(__LP64__) || defined(_WIN64) || defined(__x86_64__) || defined(__aarch64__)
#  define CPPAD_PADDING_BLOCK_T
#else
#  define CPPAD_PADDING_BLOCK_T void* padding_not_used_;
#endif

#endif // CPPAD_CONFIGURE_HPP
