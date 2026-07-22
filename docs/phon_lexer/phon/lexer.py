# -*- coding: utf-8 -*-
"""
    pygments.lexers.phon
    ~~~~~~~~~~~~~~~~~~~~~~

    Lexer for the Phonometrica scripting language (new engine).

    :copyright: Copyright 2006-2015 by the Pygments team, see AUTHORS.
    :license: BSD, see LICENSE for details.
"""

from pygments.lexer import RegexLexer, include, words
from pygments.token import Text, Comment, Operator, Keyword, Name, String, \
    Number, Punctuation

__all__ = ['PhonLexer']


class PhonLexer(RegexLexer):
    name = 'Phonometrica'
    aliases = ['phon']
    filenames = ['*.phon']
    mimetypes = ['text/x-phon']

    tokens = {
        'root': [
            (r'[^\S\n]+', Text),
            # Comments: '#' to end of line, '#* ... *#' blocks.
            (r'#\*(.|\n)*?\*#', Comment.Multiline),
            (r'#.*?(?=\n|$)', Comment.Single),
            (r'\b(true|false|null)\b', Keyword.Constant),
            (words((
                'and', 'as', 'break', 'cast', 'catch', 'class', 'const',
                'continue', 'div', 'do', 'else', 'elsif', 'end', 'field',
                'finally', 'for', 'function', 'global', 'if', 'import', 'in',
                'is', 'local', 'method', 'mod', 'not', 'open', 'or', 'ref',
                'repeat', 'return', 'spawn', 'step', 'then', 'this', 'throw',
                'to', 'try', 'until', 'var', 'while'), prefix=r'\b', suffix=r'\b'),
             Keyword.Reserved),
            # Double-quoted strings interpolate with {expr}; single-quoted are raw.
            (r'"', String.Double, 'dqstring'),
            (r"'[^'\n]*'", String.Single),
            include('numbers'),
            (r'(\.\.\.|->|\+=|-=|\*=|/=|&=|==|!=|<=|>=|[+\-*/^&=<>@])', Operator),
            (r'([{}()\[\]:;,.])', Punctuation),
            (r'[A-Za-z_]\w*', Name),
            (r'\n+', Text),
        ],
        'dqstring': [
            (r'\\.', String.Escape),
            (r'\{', String.Interpol, 'interp'),
            (r'"', String.Double, '#pop'),
            (r'[^"\\{]+', String.Double),
        ],
        'interp': [
            (r'\}', String.Interpol, '#pop'),
            (r'[^}\n]+', String.Interpol),
        ],
        'numbers': [
            (r'0[xX][a-fA-F0-9]+', Number.Hex),
            (r'0[bB][01]+', Number.Bin),
            (r'[0-9][0-9_]*\.[0-9][0-9_]*([eE][+-]?[0-9]+)?', Number.Float),
            (r'[0-9][0-9_]*[eE][+-]?[0-9]+', Number.Float),
            (r'[0-9][0-9_]*', Number.Integer),
        ],
    }


def setup(sphinx):
  from sphinx.highlighting import lexers
  sphinx.add_lexer('phon', PhonLexer)
