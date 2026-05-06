================
Acknowledgements
================

Phonometrica uses (parts of) the following open source libraries, sometimes with modifications:

* Boost (Boost license), see `boost.org <https://www.boost.org>`_
* CppAD, by Brad Bell and contributors (EPL-2.0 / GPL-2+), see `coin-or.github.io/CppAD <https://coin-or.github.io/CppAD/>`_
* Eigen, by Benoît Jacob, Gaël Guennebaud and contributors (MPL-2), see `eigen.tuxfamily.org <http://eigen.tuxfamily.org>`_
* FIR filter class, by Mike Perkins (BSD-3-Clause), see `cardinalpeak.com <https://www.cardinalpeak.com/blog/a-c-class-to-implement-low-pass-high-pass-and-band-pass-filters>`_
* LBFGS++, by Yixuan Qiu, based on work by Naoaki Okazaki (MIT), see `lbfgspp.statr.me <https://lbfgspp.statr.me/>`_
* Lucide icons, by Lucide icons and contributors (ISC License), see `lucide.dev <https://lucide.dev/>`_
* PCRE2, by Philip Hazel (BSD), see `github.com/PCRE2Project/pcre2 <https://github.com/PCRE2Project/pcre2>`_
* pocketfft, by Martin Reinecke (BSD-3-Clause), see `gitlab.mpcdf.mpg.de/mtr/pocketfft <https://gitlab.mpcdf.mpg.de/mtr/pocketfft>`_
* pugixml, by Arseny Kapoulkine (MIT), see `pugixml.org <https://pugixml.org>`_
* QScintilla, by Riverbank Computing (GPL-3), see `riverbankcomputing.com <https://www.riverbankcomputing.com/software/qscintilla/>`_
* Qt 6, by The Qt Company (LGPL-3 / GPL-2+), see `www.qt.io <https://www.qt.io/>`_
* r8brain-free-src, by Aleksey Vaneev (MIT), see `github.com/avaneev/r8brain-free-src <https://github.com/avaneev/r8brain-free-src>`_
* REAPER, by David Talkin at Google Inc. (Apache-2.0), see `github.com/google/reaper <https://github.com/google/reaper>`_
* RTAudio, by Gary P. Scavone (MIT), see `www.music.mcgill.ca/~gary/rtaudio <http://www.music.mcgill.ca/~gary/rtaudio/>`_
* sigslot, by Pierre-Antoine Lacaze (MIT) `github.com/palacaze/sigslot <https://github.com/palacaze/sigslot>`_
* Snack, by Jonas Beskow and Kåre Sjölander (BSD), see `www.speech.kth.se/snack/ <http://www.speech.kth.se/snack/>`_
* sol2, by Rapptz, ThePhD, and contributors, see `github.com/ThePhd/sol2 <https://github.com/ThePhd/sol2>`_
* SPTK, by Keiichi Tokuda, Keiichiro Oura, Takenori Yoshimura, Takato Fujimoto and contributors (Apache License 2.0), see `github.com/sp-nitech/SPTK <https://github.com/sp-nitech/SPTK>`_
* SWIPE, by Kyle Gorman (MIT), see `github.com/kylebgorman/swipe <https://github.com/kylebgorman/swipe>`_
* UTF8-CPP, by Nemanja Trifunovic (MIT), see `github.com/nemtrif/utfcpp <https://github.com/nemtrif/utfcpp>`_
* utf8proc, by JuliaStrings and the Public Software Group (MIT), see `juliastrings.github.io/utf8proc <https://juliastrings.github.io/utf8proc/>`_
* zip, by Kuba Podgórski, based on miniz, by Rich Geldreich (public domain), see `github.com/kuba--/zip <https://github.com/kuba--/zip>`_

Phonometrica uses `GitHub <https://github.com>`_ to host its source code. The source code is available `here <https://github.com/jeychenne/phonometrica>`_.

The implementation of Phonometrica's scripting engine was partly inspired by Robert Nystrom's excellent book `Crafting Interpreters <https://craftinginterpreters.com/>`_.

Portions of the statistical estimation logic — including negative binomial regression, mixed-effects models,
generalized additive models, DHARMa-style residual diagnostics, estimated marginal means, approximate
Bayesian inference (INLA-style), WAIC, and PSIS-LOO — were developed with the
assistance of Claude Opus 4.6 (Anthropic), based on published statistical literature and reference R
implementations. All AI-assisted logic was manually audited, refactored, and validated against reference R
packages (glmmTMB, mgcv, emmeans, MuMIn, DHARMa, loo, brms) to ensure mathematical accuracy and
implementation integrity.

We are also grateful to JetBrains for providing us with a non-commercial license of their `C++ editor <https://www.jetbrains.com/clion/>`_.

The development of coding protocols was originally developed as part of the following research project: *A corpus-based longitudinal study of the interphonological features of Japanese learners of French*. PI: Sylvain DETEY (Waseda University). This project was supported by the Japanese Society for the Promotion of Science (JSPS), Grant-in-Aid for Scientific Research (B) n°23320121 (2011-2014).
