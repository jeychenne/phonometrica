#!/usr/bin/env python3
#
# Phonometrica documentation build configuration file.
#

import sys
import os

sys.path.append(os.path.abspath('phon_lexer'))

extensions = [
    'sphinx.ext.mathjax',
    'sphinx.ext.ifconfig',
    'phon.lexer',
]

highlight_language = "phon"

templates_path = ['_templates']
source_suffix = '.rst'
master_doc = 'index'

project = 'Phonometrica'
copyright = '2019-2026, Julien Eychenne & Léa Courdès-Murphy'
author = 'Julien Eychenne & Léa Courdès-Murphy'

version = '0.9'
release = '0.9.0'

language = 'en'
today_fmt = '%d %B %Y'
exclude_patterns = ['_build']

pygments_style = 'sphinx'
todo_include_todos = False


# -- Options for HTML output ----------------------------------------------

html_theme = 'bizstyle'
html_static_path = ['_static']
html_domain_indices = False
html_show_sourcelink = False

htmlhelp_basename = 'phonometrica_doc'


# -- Options for QtHelp output --------------------------------------------

# These settings are used by the qthelp builder (sphinx-build -b qthelp).
# The generated .qhp can be compiled into a .qch with qhelpgenerator,
# but we currently use the raw HTML output directly.

qthelp_basename = 'Phonometrica'
qthelp_namespace = 'org.phonometrica.help'
qthelp_theme = 'basic'


# -- Options for LaTeX output ---------------------------------------------

latex_elements = {
    'preamble': r'''
    \usepackage{kotex}
    \setcounter{tocdepth}{1}
    ''',
}

latex_documents = [
    (master_doc, 'phonometrica_manual.tex', 'Phonometrica Documentation',
     r'Julien Eychenne \& Léa Courdès-Murphy', 'manual'),
]


# -- Options for manual page output ---------------------------------------

man_pages = [
    (master_doc, 'phonometrica', 'Phonometrica Documentation',
     [author], 1)
]


# -- Options for Texinfo output -------------------------------------------

texinfo_documents = [
    (master_doc, 'Phonometrica', 'Phonometrica Documentation',
     author, 'Phonometrica', 'A program for the analysis of speech corpora',
     'Miscellaneous'),
]
