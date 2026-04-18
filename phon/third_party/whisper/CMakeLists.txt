# ======================================================================================
# Vendored whisper.cpp (v1.7.4) for Phonometrica — CPU-only, no network deps.
#
# This file builds the minimum subset needed for speech-to-text inference:
#   * ggml-base : core tensor ops (ggml, ggml-alloc, ggml-backend, ggml-opt, ...)
#   * ggml-cpu  : CPU backend (includes aarch64 + quantization kernels)
#   * ggml      : backend registry (statically links ggml-cpu via GGML_USE_CPU)
#   * whisper   : whisper.cpp itself, links to ggml
#
# Only the CPU backend is included. Metal/CUDA/Vulkan/OpenCL/BLAS/SYCL/RPC/Kompute are
# intentionally omitted — re-enabling any of them means pulling additional source files
# and adjusting this file.
#
# Upstream source: https://github.com/ggerganov/whisper.cpp  (tag v1.7.4, MIT License)
# ======================================================================================

set(WHISPER_ROOT "${CMAKE_CURRENT_SOURCE_DIR}")

# POSIX visibility macros. Upstream applies these at ggml/src directory scope via
# add_compile_definitions, so every source file in every target (ggml-base, ggml-cpu,
# ggml) sees them. We do the same. The CPU backend specifically needs _GNU_SOURCE on
# Linux to expose CPU_ZERO / CPU_SET from <sched.h>.
if(CMAKE_SYSTEM_NAME MATCHES "OpenBSD")
    add_compile_definitions(_XOPEN_SOURCE=700)
else()
    add_compile_definitions(_XOPEN_SOURCE=600)
endif()
if(CMAKE_SYSTEM_NAME MATCHES "Linux|Android")
    add_compile_definitions(_GNU_SOURCE)
endif()
if(APPLE OR CMAKE_SYSTEM_NAME MATCHES "iOS|tvOS|DragonFly")
    add_compile_definitions(_DARWIN_C_SOURCE)
endif()

# ---- ggml-base -----------------------------------------------------------------------
# Core library: tensor ops, allocator, backend abstraction, quantization, optimizer.

add_library(ggml-base STATIC
    ${WHISPER_ROOT}/ggml/src/ggml.c
    ${WHISPER_ROOT}/ggml/src/ggml-alloc.c
    ${WHISPER_ROOT}/ggml/src/ggml-backend.cpp
    ${WHISPER_ROOT}/ggml/src/ggml-opt.cpp
    ${WHISPER_ROOT}/ggml/src/ggml-threading.cpp
    ${WHISPER_ROOT}/ggml/src/ggml-quants.c
)

set_property(TARGET ggml-base PROPERTY POSITION_INDEPENDENT_CODE ON)
target_compile_features(ggml-base PRIVATE c_std_11 cxx_std_17)

target_include_directories(ggml-base
    PUBLIC  ${WHISPER_ROOT}/ggml/include
    PRIVATE ${WHISPER_ROOT}/ggml/src
)

# pthreads + libm.
find_package(Threads REQUIRED)
target_link_libraries(ggml-base PUBLIC Threads::Threads)
find_library(MATH_LIBRARY m)
if(MATH_LIBRARY)
    target_link_libraries(ggml-base PRIVATE m)
endif()

# ---- ggml-cpu ------------------------------------------------------------------------
# CPU backend. The AMX subdir is always compiled but internally gated on __AMX_*__ macros,
# so it's effectively empty on non-Sapphire-Rapids machines.

add_library(ggml-cpu STATIC
    ${WHISPER_ROOT}/ggml/src/ggml-cpu/ggml-cpu.c
    ${WHISPER_ROOT}/ggml/src/ggml-cpu/ggml-cpu.cpp
    ${WHISPER_ROOT}/ggml/src/ggml-cpu/ggml-cpu-aarch64.cpp
    ${WHISPER_ROOT}/ggml/src/ggml-cpu/ggml-cpu-hbm.cpp
    ${WHISPER_ROOT}/ggml/src/ggml-cpu/ggml-cpu-quants.c
    ${WHISPER_ROOT}/ggml/src/ggml-cpu/ggml-cpu-traits.cpp
    ${WHISPER_ROOT}/ggml/src/ggml-cpu/amx/amx.cpp
    ${WHISPER_ROOT}/ggml/src/ggml-cpu/amx/mmq.cpp
)

set_property(TARGET ggml-cpu PROPERTY POSITION_INDEPENDENT_CODE ON)
target_compile_features(ggml-cpu PRIVATE c_std_11 cxx_std_17)

target_include_directories(ggml-cpu
    PRIVATE ${WHISPER_ROOT}/ggml/include
            ${WHISPER_ROOT}/ggml/src
            ${WHISPER_ROOT}/ggml/src/ggml-cpu
)

# Runtime weight conversion for Q4_0 (improves ARM perf; upstream default).
target_compile_definitions(ggml-cpu PRIVATE GGML_USE_CPU_AARCH64)

target_link_libraries(ggml-cpu PRIVATE ggml-base)

# Architecture flags. We let the compiler target the host CPU by default; builds
# targeting older machines can override by passing -DGGML_NATIVE=OFF (not implemented
# here for simplicity — flip these flags manually if you need portable binaries).
if(NOT MSVC)
    include(CheckCXXCompilerFlag)
    check_cxx_compiler_flag(-mcpu=native HAS_MCPU_NATIVE)
    check_cxx_compiler_flag(-march=native HAS_MARCH_NATIVE)
    if(HAS_MCPU_NATIVE AND CMAKE_SYSTEM_PROCESSOR MATCHES "^(aarch64|arm.*|ARM64)$")
        target_compile_options(ggml-cpu PRIVATE -mcpu=native)
    elseif(HAS_MARCH_NATIVE)
        target_compile_options(ggml-cpu PRIVATE -march=native)
    endif()
endif()

# On Apple platforms, link Accelerate for the BLAS-adjacent routines ggml-cpu uses
# when GGML_USE_ACCELERATE is defined. Off by default; opt-in via WHISPER_ACCELERATE.
if(APPLE AND WHISPER_ACCELERATE)
    find_library(ACCELERATE_FRAMEWORK Accelerate)
    if(ACCELERATE_FRAMEWORK)
        target_compile_definitions(ggml-cpu PRIVATE GGML_USE_ACCELERATE
                                                     ACCELERATE_NEW_LAPACK
                                                     ACCELERATE_LAPACK_ILP64)
        target_link_libraries(ggml-cpu PRIVATE ${ACCELERATE_FRAMEWORK})
    endif()
endif()

# ---- ggml (backend registry) ---------------------------------------------------------

add_library(ggml STATIC
    ${WHISPER_ROOT}/ggml/src/ggml-backend-reg.cpp
)

set_property(TARGET ggml PROPERTY POSITION_INDEPENDENT_CODE ON)
target_compile_features(ggml PRIVATE cxx_std_17)

target_include_directories(ggml
    PUBLIC  ${WHISPER_ROOT}/ggml/include
    PRIVATE ${WHISPER_ROOT}/ggml/src
)

# GGML_USE_CPU tells ggml-backend-reg to compile in the CPU backend registration path.
target_compile_definitions(ggml PUBLIC GGML_USE_CPU)

target_link_libraries(ggml PUBLIC ggml-base ggml-cpu)

# ggml-backend-reg uses dlopen/dlclose when loading dynamic backends — but we have none,
# so we still need libdl on Linux for the symbols to resolve.
if(CMAKE_SYSTEM_NAME MATCHES "Linux")
    target_link_libraries(ggml PRIVATE dl)
endif()

# ---- whisper -------------------------------------------------------------------------

add_library(whisper STATIC
    ${WHISPER_ROOT}/src/whisper.cpp
)

set_property(TARGET whisper PROPERTY POSITION_INDEPENDENT_CODE ON)
target_compile_features(whisper PUBLIC cxx_std_11)

target_include_directories(whisper
    PUBLIC  ${WHISPER_ROOT}/include
    PRIVATE ${WHISPER_ROOT}/ggml/include
)

target_link_libraries(whisper PUBLIC ggml)
