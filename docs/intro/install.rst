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

::

    sudo apt install ~/Downloads/phonometrica-X.X.X.deb

replacing ``X.X.X`` with the correct version number.


Building from source
~~~~~~~~~~~~~~~~~~~~

Phonometrica is written in C++ (C++17) and uses Qt 6 for its graphical interface. To build from source,
you need:

- A C++17-compliant compiler (GCC 10+, Clang 13+, or MSVC 2019+)
- CMake 3.21 or later
- Qt 6.2 or later (Widgets, Svg, and PrintSupport modules)
- QScintilla for Qt 6

All other dependencies (Eigen, Boost.Math, PCRE2, pocketfft, CppAD, utf8proc, etc.) are bundled
in the source tree. See the ``BUILD.md`` file in the repository for detailed instructions.

The source code is available at https://github.com/jeychenne/phonometrica.
