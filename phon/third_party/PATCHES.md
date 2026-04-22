# Local patches to vendored third-party code

This file documents modifications made to vendored dependencies in
`phon/third_party/`. When upgrading any of these dependencies, check whether
the patches are still needed; if so, reapply them.

## whisper.cpp / ggml

### `ggml/src/ggml-cpu/ggml-cpu.cpp` — MSVC `RegQueryValueExA` vs UNICODE

**Symptom (MSVC, `-DUNICODE -D_UNICODE`):**

```
ggml-cpu.cpp(286): error C2664: 'LSTATUS RegQueryValueExA(...)':
  impossible de convertir l'argument 2 de 'const wchar_t [20]' en 'LPCSTR'
ggml-cpu.cpp(293): error C2664: same
```

**Cause:** ggml calls `RegQueryValueExA` (ANSI variant) but passes `TEXT(...)`
string literals. With `UNICODE` defined — as required by our Qt build —
`TEXT("...")` expands to `L"..."`, so a wide string is passed to a narrow-string
function. Build is fine without `UNICODE`; breaks with it.

**Fix:** On lines 286 and 293, change `RegQueryValueExA` → `RegQueryValueEx`.
Dropping the `A` lets the macro resolve to `RegQueryValueExW` (matching the
wide `TEXT(...)` argument) under `UNICODE`, and to `RegQueryValueExA` otherwise.

One-liner to reapply from repo root:

```bash
sed -i 's/RegQueryValueExA/RegQueryValueEx/g' \
  phon/third_party/whisper/ggml/src/ggml-cpu/ggml-cpu.cpp
```

**Upstream:** <https://github.com/ggml-org/llama.cpp/issues/11802>. Check whether
the upstream whisper.cpp commit being vendored includes the fix before
reapplying.

---

## Template for new entries

```
### `<path>` — <one-line summary>

**Symptom:** <compiler/linker/runtime error>
**Cause:** <why it happens>
**Fix:** <what changed, with a reapply command if possible>
**Upstream:** <link to issue/PR, or "none filed">
```
