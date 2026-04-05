# Phonometrica

**An open-source software platform for the annotation and analysis of speech corpora.**

Phonometrica provides an integrated environment for managing, annotating, querying, and statistically analyzing spoken language data. It is designed for phoneticians, phonologists, and anyone working with time-aligned speech corpora.

## Features

- **Project management**: organize files into projects with extensible metadata (typed properties).
- **Sound visualization**: waveforms, spectrograms, pitch tracks, formant tracks, intensity curves, and spectral slices.
- **Annotation**: create and edit multi-layer annotations based on annotation graphs; seamless import/export of Praat TextGrids.
- **Queries**: search for text patterns across annotation layers using simple or complex multi-constraint queries (alignment, dominance, precedence, and more); extract formant, pitch, and intensity measurements.
- **Concordance and dataset views**: browse, filter, recode, transform, and merge query results; toggle wide/long formats; perform set operations.
- **Statistical modeling**: fit linear, logistic, Poisson, and negative binomial regression models with fixed and random effects (LMM/GLMM); model comparison with LRT and information criteria; diagnostic plots and exploratory visualizations.
- **Scripting**: extend Phonometrica with a built-in scripting language and JSON-based plugins.
- **Cross-platform**: runs on Windows, macOS, and Linux.

## Download

Pre-built binaries are available from the [releases page](https://github.com/jeychenne/phonometrica/releases) and from the [project website](http://www.phonometrica-ling.org).

## Building from source

Phonometrica is written in C++ (C++17) and uses Qt 6 for its graphical interface. You need:

- A C++17-compliant compiler (GCC 10+, Clang 13+, or MSVC 2019+)
- CMake 3.21 or later
- Qt 6.2 or later (Widgets, Svg, PrintSupport modules)
- QScintilla for Qt 6
- [Boost](https://www.boost.org/) (for Boost.Math)
- [libsndfile](https://libsndfile.github.io/libsndfile/)

All other dependencies (Eigen, PCRE2, pocketfft, CppAD, utf8proc, etc.) are bundled in the source tree. See [BUILD.md](BUILD.md) for detailed instructions.

```
git clone https://github.com/jeychenne/phonometrica.git
cd phonometrica
mkdir build && cd build
cmake ..
cmake --build .
```

## Documentation

The documentation is available online at [phonometrica-ling.org](http://www.phonometrica-ling.org) and is also accessible from within the application via the Help buttons.

## How to cite

If you use Phonometrica in your research, please cite:

> Eychenne, Julien & Léa Courdès-Murphy (2025). Annotation et analyse de données sociophonologiques sur grands corpus : présentation de la plateforme Phonometrica. In Wim Remysen & Hélène Blondeau (eds.) *(Re)donner la parole aux corpus montréalais : Regards rétrospectifs et prospectifs*. Montreal: Presses Universitaires de Montréal, pp. 255–270.

## License

Phonometrica is free software distributed under the terms of the [GNU General Public License (version 3)](LICENSE).

## Contact

For questions, bug reports, or feature requests: phonometrica.dev@gmail.com
