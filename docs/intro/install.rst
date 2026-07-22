Installation
------------

Windows
~~~~~~~

On Windows, Phonometrica is provided as a self-contained installer file.
Simply double-click on ``setup_phonometrica.exe`` and follow the instructions.

The procedure will install Phonometrica in your ``Program Files`` directory
and will create a shortcut in the start menu (and optionally on the
desktop).


macOS
~~~~~

On macOS, Phonometrica is provided as a standard DMG image disk. Mount the
image by double-clicking on it and drag ``Phonometrica`` into
your ``Applications`` folder. If you want Phonometrica to be able to interact
with Praat, you will need to install Praat in the ``Applications`` folder
as well.

macOS 11 (Big Sur) or later is required.


Linux (Debian/Ubuntu)
~~~~~~~~~~~~~~~~~~~~~

The official executable is provided for 64-bit architectures. Assuming the ``.deb`` package is in your
``Downloads`` directory, you should be able to install it with the following command:

.. code:: bash

    sudo apt install ~/Downloads/phonometrica-X.X.X.deb

replacing ``X.X.X`` with the correct version number.


Building from source
~~~~~~~~~~~~~~~~~~~~

Phonometrica is written in C++ (C++20) and uses Qt 6 for its graphical interface. To build from source,
you need:

- A C++20-compliant compiler (GCC 11+, Clang 14+, or MSVC 2022+)
- CMake 3.20 or later
- Qt 6.5 or later on Windows and Linux; Qt 6.11 or later on macOS (required for current Xcode SDKs).
  The Widgets, Svg, and PrintSupport modules are needed.
- QScintilla built against your Qt 6 installation
- Boost.Math headers (header-only; install via your system package manager or Boost release)

All other dependencies (Eigen, PCRE2, pocketfft, CppAD, LBFGSpp, utf8proc, etc.) are bundled
in the source tree. See the ``BUILD.md`` file in the repository for detailed instructions.

The source code is available at https://github.com/jeychenne/phonometrica.
