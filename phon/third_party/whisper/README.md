# Vendored whisper.cpp

**Upstream:** <https://github.com/ggerganov/whisper.cpp>
**Version:** v1.7.4
**License:** MIT (see `LICENSE`)

This directory contains a pruned, CPU-only subset of whisper.cpp used by Phonometrica's
speech-to-text feature (`phon/application/transcriber.{hpp,cpp}`).

## What's included

- `include/whisper.h`, `src/whisper.cpp` — the whisper inference library itself.
- `ggml/include/*.h` — six public ggml headers (core, allocator, backend, cpp wrappers,
  CPU backend, optimizer). The full upstream set has 15 headers — the ones not copied
  here (`ggml-cuda.h`, `ggml-metal.h`, `ggml-vulkan.h`, `ggml-sycl.h`, `ggml-blas.h`,
  `ggml-opencl.h`, `ggml-rpc.h`, `ggml-cann.h`, `ggml-kompute.h`) correspond to
  backends we don't enable, and their `#include` sites in the core are all guarded by
  `#ifdef GGML_USE_*` macros that we never define.
- `ggml/src/` — core sources: `ggml.c`, `ggml-alloc.c`, `ggml-backend.cpp`,
  `ggml-backend-reg.cpp`, `ggml-opt.cpp`, `ggml-threading.cpp`, `ggml-quants.c`,
  plus the internal headers they need.
- `ggml/src/ggml-cpu/` — CPU backend sources, including the AArch64 SIMD path and the
  quantization kernels. The `amx/` subdirectory is compiled but internally guarded on
  Intel AMX macros (`__AMX_INT8__`, `__AVX512VNNI__`), so it's effectively empty on
  anything that isn't a Sapphire-Rapids-class Intel CPU.

## What's NOT included

- GPU backends (CUDA, Metal, Vulkan, OpenCL, SYCL, Kompute, CANN, HIP).
- BLAS integration (`ggml-blas`).
- RPC support.
- `llamafile/sgemm.*` (the llamafile microkernels — opt-in upstream via `GGML_LLAMAFILE`).
- CoreML and OpenVINO encoder glue.
- Examples, tests, sample WAVs, language bindings, model-download scripts.
- Upstream's build system (CMakeLists for the discarded components).

Full clone: ~22 MB; vendored here: ~2.6 MB.

## How it's built

See `CMakeLists.txt` in this directory. Four static libraries are produced:

1. `ggml-base` — tensor ops, allocator, backend abstraction, optimizer, quantization.
2. `ggml-cpu`  — CPU backend (AArch64 + quantization kernels).
3. `ggml`      — backend registry; statically registers the CPU backend via `GGML_USE_CPU`.
4. `whisper`   — the inference entry point.

The top-level `phon-app` target links `whisper` when `WITH_WHISPER=ON` (the default).
Turn it off with `cmake -DWITH_WHISPER=OFF` to skip the compile entirely.

## Updating the vendored copy

To bump to a newer whisper.cpp tag:

1. Clone upstream: `git clone --depth 1 --branch vX.Y.Z https://github.com/ggerganov/whisper.cpp.git`
2. Copy the file set listed above into this directory.
3. Re-read `ggml/src/ggml-cpu/CMakeLists.txt` in the new upstream to check whether the
   CPU source list has changed (new files, removed files). Update our `CMakeLists.txt`
   accordingly.
4. Re-scan `#include "..."` directives across the vendored tree to confirm no new
   internal header is referenced that isn't already present or guarded.
5. Update this README with the new version number.
