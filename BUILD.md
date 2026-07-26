# Building Phonometrica

## macOS

### 1. Install Qt

Download and install Qt 6.11 or later from the [official Qt installer](https://www.qt.io/download).

### 2. Install QScintilla

QScintilla must be built from source against your Qt installation.

Download the source from:  
https://www.riverbankcomputing.com/software/qscintilla/download

Then build and install:

```bash
cd QScintilla-2.x.x/src
~/Qt/6.5.0/macos/bin/qmake qscintilla.pro

# Remove a deprecated macOS dependency that no longer ships with modern Xcode
sed -i '' 's/-framework AGL//g' Makefile

make -j$(sysctl -n hw.logicalcpu)
make install
```

> **Note:** `make install` places the library and headers directly into your Qt tree,
> where CMake will find them automatically.

### 3. Configure and Build

Pass the path to your Qt installation via `CMAKE_PREFIX_PATH`:

```bash
cmake -B build -DCMAKE_PREFIX_PATH=~/Qt/6.x.x/macos
cmake --build build -j$(sysctl -n hw.logicalcpu)
```

Replace `6.x.x` with your actual Qt version (e.g. `6.11.0`).

## Tests

Two suites, neither built by a plain `cmake --build`:

```bash
# C++ unit suite for the scripting engine (phon/engine/test/unit)
cmake --build build --target phon_unit_tests
./build/phon/engine/phon_unit_tests

# ctest also runs the two phon_repl acceptance cases
ctest --test-dir build --output-on-failure

# Script-level suite, through the application binary
./build/phonometrica -r test/engine/run_all.phon
```

The engine's targets are `EXCLUDE_FROM_ALL`, so if you change anything on its
include surface, build `phon_unit_tests`, `phon_repl` and `phon_bench` **by name** —
a plain application build will not reveal breakage in the latter two.

Offscreen GUI smokes, for behaviour that only exists once the window is up. Each
builds a throwaway profile, runs the app under `QT_QPA_PLATFORM=offscreen`, and
gates on a probe's result file:

```bash
test/gui/run_smoke_formants.sh build/phonometrica      # formant natives, 2-D shapes
test/gui/run_smoke_script_debug.sh build/phonometrica  # the debug preference
test/gui/run_smoke_settings.sh build/phonometrica      # settings write/read round trip
```

Statistics validation (slow; needs a Release build — Debug model fits take hours):

```bash
PHON_MODULE_PATH=$PWD/test/statistics/lib \
    ./build-rel/phonometrica -r test/statistics/frequentist/run_all.phon
```

See `phon/engine/README.md` for the engine's own options (`PHON_SANITIZE`,
`PHON_TSAN`, `PHON_WERROR`) and for configuring it without the application layer.
