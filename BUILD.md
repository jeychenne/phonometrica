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
